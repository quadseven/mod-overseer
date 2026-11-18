"""Tests for the route replay input builder (issue #502)."""

import json
import pathlib
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "route_cost_replay"))

from build_input import build, main as build_main, read_factions  # noqa: E402
from replay_routes import main as replay_main  # noqa: E402


def _dbc(records):
    body = b"".join(struct.pack("<14I", *row) for row in records)
    return struct.pack("<4s4I", b"WDBC", len(records), 14, 56, 1) + body + b"\0"


class BuildInput(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        (self.dir / "nodes.tsv").write_text(
            "1\t0\t0\t0\t0\n2\t0\t300\t0\t0\n3\t5\t0\t0\t0\n"
        )
        (self.dir / "links.tsv").write_text(
            "1\t2\t1\t300\t0\n2\t1\t1\t300\t0\n1\t3\t1\t10\t0\n1\t2\t2\t1\t0\n"
        )
        (self.dir / "paths.tsv").write_text(
            "1\t2\t0\t0\t0\n1\t2\t150\t5\t0\n1\t2\t300\t0\t0\n"
        )
        # guid 7 sits in two events: it must be read once and as not standing.
        (self.dir / "spawns.tsv").write_text(
            "5\t0\t150\t5\t0\t70\t1\t20\t3\t0\t0\t0\n"
            "6\t0\t150\t5\t0\t70\t0\t20\t3\t2\t128\t0\n"
            "7\t0\t0\t0\t0\t10\t0\t20\t3\t0\t2\t4\n"
            "7\t0\t0\t0\t0\t10\t0\t20\t3\t0\t2\t9\n"
        )
        (self.dir / "f.dbc").write_bytes(
            _dbc(
                [
                    (1, 1, 72, 3, 2, 12) + (0,) * 8,
                    (2, 2, 72, 5, 4, 10) + (0,) * 8,
                    (3, 3, 72, 8, 0, 3) + (0,) * 8,
                ]
            )
        )

    def built(self):
        d = self.dir
        return build(
            d / "nodes.tsv",
            d / "links.tsv",
            d / "paths.tsv",
            d / "spawns.tsv",
            d / "f.dbc",
        )

    def test_links_keep_kind_path_and_skip_unknown_nodes(self):
        out = self.built()
        kinds = sorted((r["source"], r["target"], r["kind"]) for r in out["links"])
        self.assertEqual(
            kinds, [(1, 2, "portal"), (1, 2, "walk"), (1, 3, "walk"), (2, 1, "walk")]
        )
        walk = next(r for r in out["links"] if r["kind"] == "walk" and r["source"] == 1)
        self.assertEqual(len(walk["path"]), 3)

    def test_spawn_flags_are_core_derived(self):
        spawns = self.built()["spawns"]
        self.assertEqual(len(spawns), 3)
        self.assertTrue(spawns[0]["can_be_fought"])
        self.assertFalse(spawns[1]["can_be_fought"])
        self.assertTrue(spawns[1]["trigger"])
        self.assertTrue(spawns[2]["civilian"])
        self.assertFalse(spawns[2]["active"])

    def test_factions_and_players(self):
        out = self.built()
        self.assertEqual(out["factions"][1]["our_mask"], 5)
        self.assertEqual(out["factions"][2]["hostile_mask"], 3)
        self.assertEqual(out["player_templates"], {"alliance": 1, "horde": 2})

    def test_bad_dbc_is_refused(self):
        bad = self.dir / "bad.dbc"
        bad.write_bytes(b"XXXX" + b"\0" * 40)
        with self.assertRaises(ValueError):
            read_factions(bad)

    def test_short_row_is_refused(self):
        (self.dir / "nodes.tsv").write_text("1\t0\t0\n")
        with self.assertRaises(ValueError):
            self.built()

    def test_built_input_replays_end_to_end(self):
        d = self.dir
        out = d / "input.json"
        code = build_main(
            [
                "--nodes",
                str(d / "nodes.tsv"),
                "--links",
                str(d / "links.tsv"),
                "--paths",
                str(d / "paths.tsv"),
                "--spawns",
                str(d / "spawns.tsv"),
                "--factions",
                str(d / "f.dbc"),
                "--output",
                str(out),
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("spawns", json.loads(out.read_text()))
        buf = StringIO()
        with redirect_stdout(buf):
            replay_main(["--input", str(out), "--level", "60"])
        self.assertIn("scenario=b_only", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
