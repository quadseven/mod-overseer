#!/usr/bin/env python3
"""Project the per-zone creature level band into a checked-in table (#1518).

For each zone, the band is the 10th to 90th percentile of the levels of the
hostile creature spawns in it: the low end is the 10th percentile of the
spawns' minlevel, the top is the 90th percentile of their maxlevel. Hostile
means the creature's faction template is hostile to the side's player
template (Human for the Alliance, Orc for the Horde). A spawn's zone is
resolved from its map and position the way the core does it: the tile's area
grid in the extracted .map file, then AreaTable's parent area.

The module has no file read at run time, so the table is also projected into
a C++ header it compiles in (--header). That step reads only the checked-in
table and needs no game data.

Pure standard library. See README.md for the inputs and how to fetch them.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

GRID_SIZE = 533.33333
PERCENTILES = [10, 90]
MAX_LEVEL = (
    83  # the highest real level; a few templates carry placeholders far above it
)
# Player faction templates: Human and Orc.
SIDES = {"alliance": 1, "horde": 2}
# The checked-in table and the header projected from it, from the repo root.
TABLE_PATH = "data/zone_band.json"
HEADER_PATH = "src/overseer_zone_band.h"


def percentile(values, pct):
    """Nearest-rank percentile: the smallest value with pct percent at or below it."""
    if not values:
        raise ValueError("percentile of no values")
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100 * len(ordered)))
    return ordered[rank - 1]


def read_dbc(path):
    data = Path(path).read_bytes()
    magic, count, fields, size, string_size = struct.unpack_from("<4s4I", data, 0)
    if magic != b"WDBC":
        raise ValueError(f"{path}: not a WDBC file")
    records = [
        struct.unpack_from("<%dI" % fields, data, 20 + i * size) for i in range(count)
    ]
    strings = data[20 + count * size : 20 + count * size + string_size]
    return records, strings


def dbc_string(strings, offset):
    end = strings.find(b"\0", offset)
    return strings[offset:end].decode("utf-8", "replace")


def read_faction_templates(path):
    """{id: template}: our mask, hostile mask, enemy ids, friend ids."""
    templates = {}
    records, _ = read_dbc(path)
    for r in records:
        templates[r[0]] = {
            "id": r[0],
            "ours": r[3],
            "hostile": r[5],
            "enemies": [x for x in r[6:10] if x],
            "friends": [x for x in r[10:14] if x],
        }
    return templates


def hostile_to(creature, player):
    """The core's FactionTemplateEntry::IsHostileTo, creature against player."""
    if creature is None or player is None or creature["id"] == player["id"]:
        return False
    if player["id"] in creature["enemies"]:
        return True
    if player["id"] in creature["friends"]:
        return False
    return (creature["hostile"] & player["ours"]) != 0


def read_area_zones(path):
    """({area id: zone id}, {zone id: name}); a top level area is its own zone."""
    records, strings = read_dbc(path)
    zone_of = {}
    names = {}
    for r in records:
        zone_of[r[0]] = r[2] or r[0]
        names[r[0]] = dbc_string(strings, r[11])
    return zone_of, names


class AreaReader:
    """Area ids from the extracted .map tiles, read lazily and cached."""

    def __init__(self, maps_dir):
        self.maps_dir = Path(maps_dir)
        if not self.maps_dir.is_dir():
            raise SystemExit(f"--maps is not a directory: {self.maps_dir}")
        self._tiles = {}

    def _tile(self, map_id, tx, ty):
        key = (map_id, tx, ty)
        if key not in self._tiles:
            path = self.maps_dir / f"{map_id:03d}{tx:02d}{ty:02d}.map"
            tile = None
            if path.exists():
                data = path.read_bytes()
                offset = struct.unpack_from("<I", data, 12)[0]
                if data[offset : offset + 4] == b"AREA":
                    flags, grid_area = struct.unpack_from("<HH", data, offset + 4)
                    if flags & 1:
                        tile = (grid_area, None)
                    else:
                        tile = (
                            grid_area,
                            struct.unpack_from("<256H", data, offset + 8),
                        )
            self._tiles[key] = tile
        return self._tiles[key]

    def area_at(self, map_id, x, y):
        fu = 32 - x / GRID_SIZE
        fv = 32 - y / GRID_SIZE
        tx, ty = int(fu), int(fv)
        if not (0 <= tx < 64 and 0 <= ty < 64):
            return None
        tile = self._tile(map_id, tx, ty)
        if tile is None:
            return None
        grid_area, cells = tile
        if cells is None:
            return grid_area
        lx = min(15, int(16 * (fu - tx)))
        ly = min(15, int(16 * (fv - ty)))
        return cells[lx * 16 + ly]


def read_spawns(path):
    """Rows: map, x, y, minlevel, maxlevel, faction template."""
    rows = []
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        f = line.split("\t")
        rows.append(
            (int(f[0]), float(f[1]), float(f[2]), int(f[3]), int(f[4]), int(f[5]))
        )
    return rows


def build_table(maps_dir, dbc_dir, spawns_path, roster_zones_path=None):
    dbc_dir = Path(dbc_dir)
    zone_of, names = read_area_zones(dbc_dir / "AreaTable.dbc")
    templates = read_faction_templates(dbc_dir / "FactionTemplate.dbc")
    reader = AreaReader(maps_dir)
    players = {side: templates.get(tid) for side, tid in SIDES.items()}
    if any(p is None for p in players.values()):
        raise ValueError("FactionTemplate.dbc has no player template 1 or 2")

    levels = {}  # "map:zone" -> side -> ([minlevels], [maxlevels])
    for map_id, x, y, lo, hi, faction in read_spawns(spawns_path):
        if lo < 1 or hi < lo or hi > MAX_LEVEL:
            continue  # level 0 or a placeholder above the cap says nothing about the zone
        area = reader.area_at(map_id, x, y)
        if not area:
            continue
        zone = zone_of.get(area, area)
        creature = templates.get(faction)
        for side, player in players.items():
            if hostile_to(creature, player):
                mins, maxes = levels.setdefault(f"{map_id}:{zone}", {}).setdefault(
                    side, ([], [])
                )
                mins.append(lo)
                maxes.append(hi)

    stand_in = []
    if roster_zones_path:
        for line in Path(roster_zones_path).read_text().splitlines():
            if line.strip():
                m, z = line.split("\t")[:2]
                stand_in.append(f"{int(m)}:{int(z)}")
    stand_in = sorted(set(stand_in), key=lambda k: tuple(int(p) for p in k.split(":")))

    zones = {}
    for key in sorted(
        set(levels) | set(stand_in), key=lambda k: tuple(int(p) for p in k.split(":"))
    ):
        entry = {"name": names.get(int(key.split(":")[1]), "")}
        for side in SIDES:
            pair = levels.get(key, {}).get(side)
            if pair is None:
                entry[side] = None
            else:
                entry[side] = {
                    "lo": percentile(pair[0], PERCENTILES[0]),
                    "hi": percentile(pair[1], PERCENTILES[1]),
                    "spawns": len(pair[0]),
                }
        zones[key] = entry

    return {
        "source": "creature spawns joined to creature_template, zone from the .map area grid "
        "and AreaTable parent; hostile by FactionTemplate against player templates "
        "1 (alliance) and 2 (horde); see tools/zone-band/README.md",
        "percentiles": PERCENTILES,
        "families_stand_in": stand_in,
        "zones": zones,
    }


HEADER_HEAD = """\
#ifndef OVERSEER_ZONE_BAND_H
#define OVERSEER_ZONE_BAND_H

// GENERATED by tools/zone-band/band.py from data/zone_band.json. Do not edit
// by hand: change the table, then run
//
//     python3 tools/zone-band/band.py --header src/overseer_zone_band.h
//
// tests/test_zone_band.py fails when this file and the table disagree.
//
// THE TOP OF EACH ZONE'S BAND, PER SIDE (#1518): the 90th percentile of the
// levels of the hostile creature spawns in the zone. Zero means the zone has
// no hostile spawn for that side, which is no band and no grant. The module
// reads no file at run time, so the table is compiled in like its other ones.
//
// No AzerothCore include: tests/test_zone_band_header.cpp compiles this alone.

#include <cstdint>

namespace OverseerDecisions
{

struct ZoneBandRow
{
    std::uint32_t mapId;
    std::uint32_t zoneId;
    std::uint32_t allianceTop;
    std::uint32_t hordeTop;
};

constexpr ZoneBandRow ZONE_BANDS[] = {
"""

HEADER_TAIL = """\
};

// What the table says of one zone for one side.
struct ZoneBandTop
{
    bool hasBand{false};
    std::uint32_t top{0};
};

// The band top of the zone for the side, or no band for a zone the table does
// not hold and for a side with no hostile spawn in it.
inline ZoneBandTop ZoneBandTopFor(std::uint32_t mapId, std::uint32_t zoneId, bool horde)
{
    ZoneBandTop band;
    for (ZoneBandRow const& row : ZONE_BANDS)
    {
        if (row.mapId != mapId || row.zoneId != zoneId)
            continue;
        band.top = horde ? row.hordeTop : row.allianceTop;
        band.hasBand = band.top != 0;
        break;
    }
    return band;
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_ZONE_BAND_H
"""


def header_text(table):
    """The C++ header for a table: one row per zone, in the table's order."""
    rows = []
    for key, entry in table["zones"].items():
        map_id, zone_id = (int(part) for part in key.split(":"))
        tops = [entry[side]["hi"] if entry[side] else 0 for side in ("alliance", "horde")]
        if any(top < 0 for top in tops):
            raise ValueError(f"zone {key}: a band top below zero")
        # The name is a comment only; anything that could end it early is dropped.
        name = "".join(ch for ch in entry.get("name", "") if " " <= ch <= "~" and ch != "\\")
        row = f"    {{{map_id}, {zone_id}, {tops[0]}, {tops[1]}}},"
        rows.append(f"{row}  // {name}" if name else row)
    if not rows:
        raise ValueError("the table holds no zone")
    return HEADER_HEAD + "\n".join(rows) + "\n" + HEADER_TAIL


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--maps", help="directory of extracted .map tiles")
    ap.add_argument("--dbc", help="directory with AreaTable.dbc and FactionTemplate.dbc")
    ap.add_argument("--spawns", help="TSV: map, x, y, minlevel, maxlevel, faction")
    ap.add_argument("--roster-zones", help="TSV: map, zone of each family member")
    ap.add_argument("--out", help="write the table here; needs --maps, --dbc and --spawns")
    ap.add_argument(
        "--header",
        help="write the C++ header here, projected from --table; needs no game data",
    )
    ap.add_argument(
        "--table",
        help=f"the table --header reads (default: --out when given, else {TABLE_PATH})",
    )
    args = ap.parse_args(argv)
    if not args.out and not args.header:
        ap.error("nothing to do: give --out, --header or both")
    if args.out:
        needed = (("--maps", args.maps), ("--dbc", args.dbc), ("--spawns", args.spawns))
        missing = [flag for flag, value in needed if not value]
        if missing:
            ap.error("--out needs " + ", ".join(missing))
        for label, path in (
            ("AreaTable.dbc", Path(args.dbc) / "AreaTable.dbc"),
            ("FactionTemplate.dbc", Path(args.dbc) / "FactionTemplate.dbc"),
            ("spawns file", Path(args.spawns)),
        ):
            if not path.exists():
                print(f"zone-band: {label} not found at {path}", file=sys.stderr)
                return 2
        try:
            table = build_table(args.maps, args.dbc, args.spawns, args.roster_zones)
        except ValueError as err:
            print(f"zone-band: {err}", file=sys.stderr)
            return 2
        Path(args.out).write_text(json.dumps(table, indent=1, sort_keys=False) + "\n")
    if args.header:
        table_path = Path(args.table or args.out or TABLE_PATH)
        if not table_path.exists():
            print(f"zone-band: table not found at {table_path}", file=sys.stderr)
            return 2
        try:
            text = header_text(json.loads(table_path.read_text()))
        except (ValueError, KeyError, TypeError) as err:
            print(f"zone-band: {table_path} is not a band table: {err!r}", file=sys.stderr)
            return 2
        Path(args.header).write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
