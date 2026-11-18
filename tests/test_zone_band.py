"""Pins the zone band tool and the checked-in band table (#1518, box 1).

The synthetic files below are built here, so the test needs no extracted game
data. The checked-in table is only parsed and checked for shape and coverage.
"""

import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/zone-band/band.py"
TABLE = ROOT / "data/zone_band.json"


def load_tool():
    spec = importlib.util.spec_from_file_location("zone_band_tool", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def map_file(area_value=None, cells=None):
    """A .map file holding only the header and the area section."""
    header_size = 44
    if cells is None:
        area = b"AREA" + struct.pack("<HH", 1, area_value)
    else:
        area = b"AREA" + struct.pack("<HH", 0, 0) + struct.pack("<256H", *cells)
    header = struct.pack("<4s10I", b"MAPS", 9, 0x4030, header_size, len(area), 0, 0, 0, 0, 0, 0)
    return header + area


def dbc(records, field_count, strings=b"\0"):
    body = b"".join(struct.pack("<%dI" % field_count, *record) for record in records)
    head = struct.pack("<4sIIII", b"WDBC", len(records), field_count, field_count * 4, len(strings))
    return head + body + strings


def area_table(rows):
    """rows: (area id, map, parent, name offset)."""
    records = []
    for area_id, map_id, parent, name in rows:
        record = [0] * 36
        record[0], record[1], record[2], record[11] = area_id, map_id, parent, name
        records.append(record)
    return dbc(records, 36, b"\0Elwynn\0Goldshire\0")


def faction_templates(rows):
    """rows: (id, our mask, hostile mask, enemies, friends)."""
    records = []
    for ident, ours, hostile, enemies, friends in rows:
        enemies = list(enemies) + [0] * (4 - len(enemies))
        friends = list(friends) + [0] * (4 - len(friends))
        records.append([ident, ident, 0, ours, 0, hostile, *enemies, *friends])
    return dbc(records, 14)


class PercentileTest(unittest.TestCase):
    def test_nearest_rank(self):
        tool = load_tool()
        values = list(range(1, 11))
        self.assertEqual(tool.percentile(values, 10), 1)
        self.assertEqual(tool.percentile(values, 90), 9)
        self.assertEqual(tool.percentile(values, 100), 10)
        self.assertEqual(tool.percentile([7], 90), 7)

    def test_unsorted_input_and_empty(self):
        tool = load_tool()
        self.assertEqual(tool.percentile([30, 10, 20], 90), 30)
        with self.assertRaises(ValueError):
            tool.percentile([], 90)


class HostileTest(unittest.TestCase):
    def setUp(self):
        tool = load_tool()
        data = faction_templates([
            (1, 0b0001, 0b1100, [], []),      # player, alliance side
            (14, 0b0100, 0b0001, [], []),     # a monster mask that hates the player
            (35, 0b0010, 0b0000, [], []),     # a neutral critter
            (7, 0b0100, 0b0001, [], [1]),     # hostile mask, but names the player a friend
            (8, 0b0100, 0b0000, [1], []),     # no mask, but names the player an enemy
        ])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "FactionTemplate.dbc"
            path.write_bytes(data)
            self.templates = tool.read_faction_templates(path)
        self.tool = tool

    def test_masks_decide_hostility(self):
        self.assertTrue(self.tool.hostile_to(self.templates[14], self.templates[1]))
        self.assertFalse(self.tool.hostile_to(self.templates[35], self.templates[1]))

    def test_named_friend_and_enemy_beat_the_mask(self):
        self.assertFalse(self.tool.hostile_to(self.templates[7], self.templates[1]))
        self.assertTrue(self.tool.hostile_to(self.templates[8], self.templates[1]))

    def test_unknown_template_is_not_hostile(self):
        self.assertFalse(self.tool.hostile_to(None, self.templates[1]))


class AreaLookupTest(unittest.TestCase):
    def test_a_missing_maps_directory_is_refused(self):
        tool = load_tool()
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                tool.AreaReader(Path(tmp) / "absent")

    def test_single_value_tile_and_grid_tile(self):
        tool = load_tool()
        cells = list(range(256))
        with tempfile.TemporaryDirectory() as tmp:
            maps = Path(tmp)
            # world 0,0 is the corner of tile 32,32; x = 250 is a little over half way into tile 31.
            (maps / "0003232.map").write_bytes(map_file(area_value=12))
            (maps / "0003132.map").write_bytes(map_file(cells=cells))
            reader = tool.AreaReader(maps)
            self.assertEqual(reader.area_at(0, 0.0, 0.0), 12)
            # in-tile x cell int(16 * 0.5) = 8, y cell 0, laid out x * 16 + y
            self.assertEqual(reader.area_at(0, 250.0, 0.0), 8 * 16 + 0)
            self.assertIsNone(reader.area_at(0, 99999.0, 0.0))
            self.assertIsNone(reader.area_at(5, 0.0, 0.0))


class BuildTest(unittest.TestCase):
    def fixture(self, tmp):
        tmp = Path(tmp)
        maps = tmp / "maps"
        dbcs = tmp / "dbc"
        maps.mkdir()
        dbcs.mkdir()
        (maps / "0003232.map").write_bytes(map_file(area_value=24))     # Goldshire, in Elwynn
        (maps / "0003231.map").write_bytes(map_file(area_value=12))     # Elwynn itself
        (dbcs / "AreaTable.dbc").write_bytes(area_table([(12, 0, 0, 1), (24, 0, 12, 8)]))
        (dbcs / "FactionTemplate.dbc").write_bytes(faction_templates([
            (1, 0b0001, 0b1100, [], []),
            (2, 0b0010, 0b1100, [], []),
            (14, 0b0100, 0b0011, [], []),
            (35, 0b1000, 0b0000, [], []),
        ]))
        rows = []
        # ten hostile spawns in Goldshire (tile 32,32) rising from level 1 to 10
        for level in range(1, 11):
            rows.append((0, 0.0, 0.0, level, level, 14))
        rows.append((0, 0.0, 0.0, 1, 1, 35))               # a critter is not hostile
        rows.append((0, 0.0, 0.0, 60, 60, 99))              # unknown faction template
        rows.append((0, 0.0, 100.0, 5, 6, 14))              # tile 32,31: Elwynn itself
        spawns = tmp / "spawns.tsv"
        spawns.write_text("\n".join("\t".join(str(v) for v in row) for row in rows) + "\n")
        roster = tmp / "roster.tsv"
        roster.write_text("0\t12\n0\t9999\n")
        return maps, dbcs, spawns, roster

    def test_band_is_zone_resolved_and_hostile_only(self):
        tool = load_tool()
        with tempfile.TemporaryDirectory() as tmp:
            maps, dbcs, spawns, roster = self.fixture(tmp)
            table = tool.build_table(maps, dbcs, spawns, roster)
        self.assertEqual(table["percentiles"], [10, 90])
        elwynn = table["zones"]["0:12"]
        # Goldshire's area resolves to its parent zone 12, so 11 hostile spawns land
        # there: minlevels 1..10 and 5 for the low end, maxlevels 1..10 and 6 for the top.
        self.assertEqual(elwynn["alliance"], {"lo": 2, "hi": 9, "spawns": 11})
        self.assertEqual(elwynn["horde"], {"lo": 2, "hi": 9, "spawns": 11})
        self.assertEqual(elwynn["name"], "Elwynn")

    def test_roster_zone_without_hostiles_is_listed_with_no_band(self):
        tool = load_tool()
        with tempfile.TemporaryDirectory() as tmp:
            maps, dbcs, spawns, roster = self.fixture(tmp)
            table = tool.build_table(maps, dbcs, spawns, roster)
        self.assertEqual(table["families_stand_in"], ["0:12", "0:9999"])
        self.assertIsNone(table["zones"]["0:9999"]["alliance"])
        self.assertIsNone(table["zones"]["0:9999"]["horde"])

    def test_cli_writes_the_same_table(self):
        tool = load_tool()
        with tempfile.TemporaryDirectory() as tmp:
            maps, dbcs, spawns, roster = self.fixture(tmp)
            out = Path(tmp) / "out.json"
            result = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--maps", str(maps), "--dbc", str(dbcs),
                 "--spawns", str(spawns), "--roster-zones", str(roster), "--out", str(out)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(out.read_text()), tool.build_table(maps, dbcs, spawns, roster))

    def test_cli_refuses_a_missing_input_with_a_named_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--maps", tmp, "--dbc", tmp,
                 "--spawns", str(Path(tmp) / "none.tsv"), "--out", str(Path(tmp) / "o.json")],
                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AreaTable.dbc", result.stderr)


class CheckedInTableTest(unittest.TestCase):
    def setUp(self):
        self.table = json.loads(TABLE.read_text())

    def test_every_band_parses(self):
        zones = self.table["zones"]
        self.assertGreater(len(zones), 30)
        for key, entry in zones.items():
            map_id, zone_id = key.split(":")
            self.assertTrue(map_id.isdigit() and zone_id.isdigit(), key)
            for side in ("alliance", "horde"):
                band = entry[side]
                if band is None:
                    continue
                self.assertLessEqual(1, band["lo"], key)
                self.assertLessEqual(band["lo"], band["hi"], key)
                self.assertLessEqual(band["hi"], 83, key)
                self.assertGreater(band["spawns"], 0, key)

    def test_every_zone_the_families_stand_in_is_covered(self):
        stand_in = self.table["families_stand_in"]
        self.assertGreater(len(stand_in), 0)
        for key in stand_in:
            self.assertIn(key, self.table["zones"], key)

    def test_the_table_records_how_it_was_made(self):
        self.assertEqual(self.table["percentiles"], [10, 90])
        self.assertIn("source", self.table)


if __name__ == "__main__":
    unittest.main()
