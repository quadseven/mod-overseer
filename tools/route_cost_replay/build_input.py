"""Build the route replay input JSON from tab-separated read-only exports.

The exports are plain `mysql -B -N` output, one file per query (see README.md),
plus the core's FactionTemplate.dbc. Nothing here connects to a database.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
from typing import Sequence

LINK_KINDS = {1: "walk", 2: "portal", 3: "transport", 4: "flight"}
UNIT_FLAGS_CANNOT_FIGHT = 0x00000002 | 0x00000100 | 0x02000000
FLAG_EXTRA_CIVILIAN = 0x00000002
FLAG_EXTRA_TRIGGER = 0x00000080


def _rows(path: Path, width: int) -> list[list[str]]:
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != width:
            raise ValueError(f"{path.name} line {number}: want {width} columns")
        rows.append(cells)
    return rows


def read_factions(path: Path) -> list[dict[str, object]]:
    """Parse FactionTemplate.dbc (14 uint32 fields per record)."""
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("FactionTemplate.dbc is truncated")
    magic, count, fields, record_size, _ = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields != 14 or record_size != 56:
        raise ValueError("FactionTemplate.dbc has an unexpected layout")
    if len(data) < 20 + count * record_size:
        raise ValueError("FactionTemplate.dbc is truncated")
    out = []
    for index in range(count):
        row = struct.unpack_from("<14I", data, 20 + index * record_size)
        out.append(
            {
                "id": row[0],
                "faction": row[1],
                "our_mask": row[3],
                "hostile_mask": row[5],
                "enemy_factions": list(row[6:10]),
                "friend_factions": list(row[10:14]),
            }
        )
    return out


def build(
    nodes: Path, links: Path, paths: Path, spawns: Path, factions: Path
) -> dict[str, object]:
    node_rows = [
        {"id": int(a), "map_id": int(b), "x": float(c), "y": float(d), "z": float(e)}
        for a, b, c, d, e in _rows(nodes, 5)
    ]
    node_ids = {row["id"] for row in node_rows}
    points: dict[tuple[int, int], list[list[float]]] = {}
    for a, b, x, y, z in _rows(paths, 5):
        points.setdefault((int(a), int(b)), []).append([float(x), float(y), float(z)])
    link_rows = []
    for a, b, kind, distance, extra in _rows(links, 5):
        source, target = int(a), int(b)
        if source not in node_ids or target not in node_ids:
            continue
        link_rows.append(
            {
                "source": source,
                "target": target,
                "kind": LINK_KINDS.get(int(kind), "none"),
                "distance": float(distance),
                "extra_cost": float(extra),
                "path": points.get((source, target), []),
            }
        )
    # A spawn can sit in several game events: keep the first row per guid, and
    # treat an event-linked spawn as not standing (no event is assumed running).
    seen: set[str] = set()
    spawn_rows = []
    for (
        guid,
        map_id,
        x,
        y,
        z,
        level,
        rank,
        detect,
        faction,
        uflags,
        xflags,
        event,
    ) in _rows(spawns, 12):
        if guid in seen:
            continue
        seen.add(guid)
        extra = int(xflags)
        spawn_rows.append(
            {
                "map_id": int(map_id),
                "x": float(x),
                "y": float(y),
                "z": float(z),
                "max_level": int(level),
                "rank": int(rank),
                "detection_range": float(detect),
                "faction_template_id": int(faction),
                "can_be_fought": (int(uflags) & UNIT_FLAGS_CANNOT_FIGHT) == 0,
                "trigger": (extra & FLAG_EXTRA_TRIGGER) != 0,
                "civilian": (extra & FLAG_EXTRA_CIVILIAN) != 0,
                "active": int(event) == 0,
            }
        )
    return {
        "nodes": node_rows,
        "links": link_rows,
        "spawns": spawn_rows,
        "factions": read_factions(factions),
        "player_templates": {"alliance": 1, "horde": 2},
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("nodes", "links", "paths", "spawns", "factions"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        built = build(args.nodes, args.links, args.paths, args.spawns, args.factions)
    except (OSError, ValueError) as exc:
        print(f"cannot build input: {exc}", file=sys.stderr)
        return 2
    args.output.write_text(json.dumps(built), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
