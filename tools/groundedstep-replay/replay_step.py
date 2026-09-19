#!/usr/bin/env python3
"""Replay this module's GroundedStep stride for stride over the shipped
heightmap grids (AzerothCore .map files, version 9), with no server running.

WHAT THIS IS FOR. Two comments in this repository quote concrete distances
that were measured with a replay like this one:

  src/mod_overseer.cpp:13759-13766      "refused every bearing after
  src/overseer_decisions.h:7340-7346     EIGHTY-FOUR YARDS ... the same replay
                                         starting at Ratchet ... ARRIVES"

The harness that produced them was scratch tooling and was never committed, so
it has now been rebuilt from nothing twice. This file is that harness, kept, so
the next person who wants to know whether a refusal is the terrain or the code
can re-measure in a second instead of a day. It is also the first thing in this
repository that exercises the step chooser against real map data rather than
against hand-written fixtures (see quadseven/mod-overseer#116).

TERRAIN ONLY. NO VMAPS, NO MMAPS, NO LIQUID. This is the load-bearing caveat
and it must not be dropped from any summary of what this tool says:

  * No vmaps. Nothing standing ON the ground is modelled, so NothingInTheWay
    (mod_overseer.cpp:14881) is not replayed at all. Every wall, tree, building
    and bridge railing the real module would refuse to walk through is invisible
    here. That makes this replay MORE permissive than the module.
  * No liquid. SurfaceAt asks Map::GetWaterOrGroundLevel, which answers with a
    water SURFACE where there is water (mod_overseer.cpp:13470-13478). Here a
    river reads as its bed, which is a drop the module would not have seen.
  * No mmaps. The navmesh branch at the top of GroundedStep
    (mod_overseer.cpp:14785, `if (NavmeshRoutes(...))`) is skipped entirely, so
    every step here falls through to the footing check. The real module often
    never reaches that check at all. This is the one omission that makes the
    replay STRICTER than the module rather than looser, and it is why a refusal
    in the last few yards of a walk (see the Ratchet control below) is not
    evidence that the module would refuse there.

So: a refusal this tool reports is a LOWER BOUND on the module's strictness
over open ground. Where it refuses on a rise or a drop, the module refuses too,
because it is reading the same heightmap through the same arithmetic. Where it
WALKS, the module might still refuse, because something this tool cannot see is
standing in the way. Nothing here proves a route is walkable. It proves a route
is not.

EVERY RULE BELOW IS TRANSCRIBED FROM A NAMED SOURCE LINE, so the reading can be
checked against the code rather than trusted:

  GridTerrainData.cpp:237-320   getHeightFromFloat (v9/v8 triangle interpolation)
  GridTerrainData.cpp:322-460   getHeightFromUint8 / getHeightFromUint16
  GridTerrainData.cpp:462-475   isHole
  Map.cpp:1118-1136             Map::GetWaterOrGroundLevel (+Z_OFFSET_FIND_HEIGHT)
  Map.cpp:1166-1198             Map::GetHeight (the z >= gridHeight - 0.05 gate)
  GridDefines.h:186-190         Acore::ComputeGridCoord
  mod_overseer.cpp:13479-13493  SurfaceAt
  mod_overseer.cpp:13561-13650  GroundHolds
  mod_overseer.cpp:14759-14885  GroundedStep
  overseer_decisions.cpp:444    StepMayBridgeGap
  overseer_decisions.cpp:453    FootingSampleHolds
  overseer_decisions.cpp:464    ProvenStepIsWorthTaking

Those first six are AzerothCore's, at the core SHA pinned in
.github/workflows/build.yml. The last six are this repository's own.

USAGE. See README.md in this directory. The short version:

  python3 replay_step.py                 run the self-test (this is the gate)
  python3 replay_step.py --survey        the exploratory report
  python3 replay_step.py --fault-drill   prove the self-test can actually fail

The .map tiles are NOT in this repository and must not be: they are extracted
game data, 54 MB for the two zones the checks below touch. README.md says where
to get them. Point this at them with --maps DIR or OVERSEER_REPLAY_MAPS.
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys
import tempfile

# --------------------------------------------------------------------------
# THE MAP FORMAT. AzerothCore's own constants, not this module's.
# --------------------------------------------------------------------------

# GridDefines.h:41-46. A grid is 533 and a third yards square and is sampled on
# a 128 by 128 lattice of cells, each cell carrying a centre sample as well as
# its corners, which is where the 129 by 129 v9 array and the 128 by 128 v8
# array come from.
SIZE_OF_GRIDS = 533.3333
MAP_RESOLUTION = 128

# GridTerrainData.h:27. Both this and the vmap's own "nothing here" value are
# far below any floor in the world, so one comparison catches both, exactly as
# SurfaceAt does it (mod_overseer.cpp:13488).
INVALID_HEIGHT = -100000.0

# Map.cpp:1176. GetHeight refuses to answer with a terrain height for a point
# that is BELOW the terrain, with a five hundredths of a yard of slack so that
# standing exactly on the ground is not "below" it.
GROUND_HEIGHT_TOLERANCE = 0.05

# Map.cpp:1122. GetWaterOrGroundLevel looks for the ground from two yards above
# the z it was handed rather than from the z itself.
Z_OFFSET_FIND_HEIGHT = 2.0

# MHGT flags, GridTerrainData.cpp:118-124 (MAP_HEIGHT_NO_HEIGHT,
# MAP_HEIGHT_AS_INT16, MAP_HEIGHT_AS_INT8). A tile stores its heights as
# float32, uint16 or uint8 depending on how much relief it actually has, and a
# perfectly flat tile stores none at all.
MAP_HEIGHT_NO_HEIGHT = 0x0001
MAP_HEIGHT_AS_INT16 = 0x0002
MAP_HEIGHT_AS_INT8 = 0x0004

# GridTerrainData.cpp:462-475. The hole mask is 16 by 16 cells of 16 bits, and
# a bit is addressed by intersecting a column mask with a row mask.
HOLETAB_H = (0x1111, 0x2222, 0x4444, 0x8888)
HOLETAB_V = (0x000F, 0x00F0, 0x0F00, 0xF000)

# --------------------------------------------------------------------------
# THE MODULE'S OWN CONSTANTS, each at the line it is declared on in
# src/mod_overseer.cpp. If one of these ever changes in the C++ and not here,
# the self-test's distances move and the run says so.
# --------------------------------------------------------------------------

TRAVEL_STEP_YARDS = 60.0            # mod_overseer.cpp:1422
TRAVEL_GROUND_SAMPLE_YARDS = 4.0    # mod_overseer.cpp:1428
TRAVEL_GROUND_DROP_YARDS = 10.0     # mod_overseer.cpp:1445
TRAVEL_GROUND_RISE_YARDS = 8.0      # mod_overseer.cpp:1457
TRAVEL_GROUND_SNAP_YARDS = 5.0      # mod_overseer.cpp:1466
TRAVEL_GROUND_UPHILL_YARDS = 50.0   # mod_overseer.cpp:1474
TRAVEL_STEP_SIDE_FRACTION = 0.6     # mod_overseer.cpp:1481
TRAVEL_STEP_MIN_YARDS = TRAVEL_GROUND_SAMPLE_YARDS * 2.0   # mod_overseer.cpp:1490
TRAVEL_STEP_VERTICAL_YARDS = 20.0   # mod_overseer.cpp:1520

# mod_overseer.cpp:1283. An AIMED POSITION is arrived at within five yards.
# This is deliberately not TRAVEL_ARRIVED_YARDS (12, mod_overseer.cpp:1203),
# which is the tolerance for a CREATURE, whose spawn point is not where it is
# standing. The two control walks below both aim at a position, so five is the
# number that applies to them.
TRAVEL_ARRIVED_POSITION_YARDS = 5.0

# mod_overseer.cpp:14829. The five bearings, in radians, straight line first and
# then either side of it, nearest first. Thirty and sixty degrees. Ninety is
# deliberately absent; see that line's comment for why it never did anything.
FAN = (0.0, 0.5236, -0.5236, 1.0472, -1.0472)

# --------------------------------------------------------------------------
# WHERE THE TILES ARE. Not in this repository, on purpose - see the docstring.
# --------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAP_DIR = os.path.join(HERE, "maps")
MAP_DIR_ENV = "OVERSEER_REPLAY_MAPS"

_map_dir = os.environ.get(MAP_DIR_ENV) or DEFAULT_MAP_DIR
_tiles: dict = {}
MISSING: set = set()


def set_map_dir(path: str) -> None:
    """Point the loader somewhere else and drop everything already read.

    The cache has to go with the directory. A stale tile answering for a
    directory that does not contain it is the exact shape of a check that
    cannot fail, and --fault-drill below swaps the directory on purpose.
    """
    global _map_dir
    _map_dir = path
    _tiles.clear()
    MISSING.clear()


def map_dir() -> str:
    return _map_dir


# --------------------------------------------------------------------------
# THE TILE READER
# --------------------------------------------------------------------------


class Tile:
    """One extracted .map tile, version 9.

    The header is eleven little-endian uint32 (map_magic, version_magic,
    build_magic, then an offset and a size for each of the area, height, liquid
    and hole chunks). Only the height chunk and the hole mask are read here;
    area ids and liquid are not modelled.
    """

    __slots__ = ("kind", "v9", "v8", "grid_height", "mult", "holes", "path")

    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, "rb") as f:
            blob = f.read()
        (_magic, version, _build, _area_off, _area_sz, h_off, _h_sz,
         _l_off, _l_sz, holes_off, holes_sz) = struct.unpack_from("<11I", blob, 0)
        # Version 9 is what the pinned core extracts and what this reader was
        # written against. Refusing anything else is the point: silently
        # misreading a v8 or v10 tile would produce plausible heights and wrong
        # answers, which is worse than not reading it.
        if blob[0:4] != b"MAPS" or version != 9:
            raise ValueError("%s: not a version 9 MAPS tile" % path)

        self.kind = "flat"
        self.v9 = None
        self.v8 = None
        self.grid_height = 0.0
        self.mult = 0.0
        self.holes = None

        if h_off:
            fourcc, flags, grid_height, grid_max = struct.unpack_from(
                "<IIff", blob, h_off)
            if fourcc != int.from_bytes(b"MHGT", "little"):
                raise ValueError("%s: height chunk is not MHGT" % path)
            self.grid_height = grid_height
            p = h_off + 16
            if flags & MAP_HEIGHT_NO_HEIGHT:
                # A flat tile stores one number for the whole 533 yards.
                self.kind = "flat"
            elif flags & MAP_HEIGHT_AS_INT16:
                self.kind = "u16"
                self.v9 = struct.unpack_from("<%dH" % (129 * 129), blob, p)
                p += 2 * 129 * 129
                self.v8 = struct.unpack_from("<%dH" % (128 * 128), blob, p)
                self.mult = (grid_max - grid_height) / 65535.0
            elif flags & MAP_HEIGHT_AS_INT8:
                self.kind = "u8"
                self.v9 = struct.unpack_from("<%dB" % (129 * 129), blob, p)
                p += 129 * 129
                self.v8 = struct.unpack_from("<%dB" % (128 * 128), blob, p)
                self.mult = (grid_max - grid_height) / 255.0
            else:
                self.kind = "f32"
                self.v9 = struct.unpack_from("<%df" % (129 * 129), blob, p)
                p += 4 * 129 * 129
                self.v8 = struct.unpack_from("<%df" % (128 * 128), blob, p)

        if holes_sz:
            self.holes = struct.unpack_from("<%dH" % (16 * 16), blob, holes_off)

    def is_hole(self, row: int, col: int) -> bool:
        """GridTerrainData.cpp:462-475. A hole is ground that is not there."""
        if self.holes is None:
            return False
        cell_row = row // 8
        cell_col = col // 8
        hole_row = (row % 8) // 2
        hole_col = (col - cell_col * 8) // 2
        hole = self.holes[cell_row * 16 + cell_col]
        return (hole & HOLETAB_H[hole_col] & HOLETAB_V[hole_row]) != 0

    def height(self, wx: float, wy: float) -> float:
        """GridTerrainData.cpp:237-460, the v9/v8 triangle interpolation.

        Each cell is four corner samples from v9 plus one centre sample from
        v8, which divides it into four triangles. Which triangle a point falls
        in is decided by the two comparisons below, and the plane through that
        triangle's three samples is what answers. The centre sample is stored
        halved in the integer encodings, hence the 2 * v8 on h5.
        """
        if self.kind == "flat":
            return self.grid_height
        x = MAP_RESOLUTION * (32 - wx / SIZE_OF_GRIDS)
        y = MAP_RESOLUTION * (32 - wy / SIZE_OF_GRIDS)
        x_int = int(x)
        y_int = int(y)
        x -= x_int
        y -= y_int
        x_int &= MAP_RESOLUTION - 1
        y_int &= MAP_RESOLUTION - 1
        if self.is_hole(x_int, y_int):
            return INVALID_HEIGHT
        v9 = self.v9
        v8 = self.v8
        base = x_int * 129 + y_int
        h5 = 2 * v8[x_int * 128 + y_int]
        if x + y < 1:
            if x > y:
                h1 = v9[base]
                h2 = v9[base + 129]
                a, b, c = h2 - h1, h5 - h1 - h2, h1
            else:
                h1 = v9[base]
                h3 = v9[base + 1]
                a, b, c = h5 - h1 - h3, h3 - h1, h1
        else:
            if x > y:
                h2 = v9[base + 129]
                h4 = v9[base + 130]
                a, b, c = h2 + h4 - h5, h4 - h2, h5 - h4
            else:
                h3 = v9[base + 1]
                h4 = v9[base + 130]
                a, b, c = h4 - h3, h3 + h4 - h5, h5 - h4
        raw = a * x + b * y + c
        if self.kind == "f32":
            return raw
        return raw * self.mult + self.grid_height


def tile_name(mapid: int, x: float, y: float) -> str:
    """GridDefines.h:186-190, Acore::ComputeGridCoord, and the %03u%02u%02u
    filename Map::GetGridTerrainData builds from it (Map.cpp:1090)."""
    gx = max(0, int(32 - x / SIZE_OF_GRIDS))
    gy = max(0, int(32 - y / SIZE_OF_GRIDS))
    return "%03d%02d%02d.map" % (mapid, gx, gy)


def tile_for(mapid: int, x: float, y: float):
    """The tile under a point, or None when it is not on disk.

    A MISSING tile is recorded rather than raised, because a replay that walks
    off the edge of what was extracted is a normal thing to do. It is also the
    most dangerous silence in this whole file: a missing tile makes every
    height INVALID_HEIGHT, which makes every step refuse, which looks exactly
    like a terrain refusal. Nothing may report a pass while MISSING is
    non-empty. See check_no_missing_tiles().
    """
    gx = max(0, int(32 - x / SIZE_OF_GRIDS))
    gy = max(0, int(32 - y / SIZE_OF_GRIDS))
    key = (gx, gy)
    if key in _tiles:
        return _tiles[key]
    path = os.path.join(_map_dir, "%03d%02d%02d.map" % (mapid, gx, gy))
    try:
        t = Tile(path)
    except (FileNotFoundError, NotADirectoryError):
        MISSING.add(key)
        t = None
    _tiles[key] = t
    return t


def grid_height(mapid: int, x: float, y: float) -> float:
    t = tile_for(mapid, x, y)
    if t is None:
        return INVALID_HEIGHT
    return t.height(x, y)


# --------------------------------------------------------------------------
# THE MODULE'S RULES, TRANSCRIBED
# --------------------------------------------------------------------------


def map_get_height(mapid: int, x: float, y: float, z: float) -> float:
    """Map.cpp:1166-1198, the terrain branch only (no vmaps are loaded).

    The real function returns the HIGHER of the vmap height and the terrain
    height, and answers with terrain only when the caller is at or above it.
    That gate is what is kept here: a point below the ground gets no answer.
    """
    gh = grid_height(mapid, x, y)
    if z >= gh - GROUND_HEIGHT_TOLERANCE:
        return gh
    return INVALID_HEIGHT


def surface_at(mapid: int, x: float, y: float, frm: float):
    """mod_overseer.cpp:13479 SurfaceAt, which asks Map::GetWaterOrGroundLevel
    (Map.cpp:1118). Liquid is not modelled, so this is the ground branch: a
    river reads as its bed here and as its surface in the module."""
    h = map_get_height(mapid, x, y, frm + Z_OFFSET_FIND_HEIGHT)
    if h <= INVALID_HEIGHT:
        return False, 0.0
    return True, h


def footing_sample_holds(from_z: float, to_z: float, max_drop: float,
                         max_rise: float) -> bool:
    """overseer_decisions.cpp:453. The SMALLER bound, applied to the magnitude.
    A stride is walkable exactly when the stride back is, so there is one
    number rather than two."""
    bound = max_drop if max_drop < max_rise else max_rise
    return abs(to_z - from_z) <= bound


def proven_step_is_worth_taking(whole_held: bool, proved: float,
                                min_yards: float) -> bool:
    """overseer_decisions.cpp:464. Nothing was truncated, so there is nothing
    for the floor to judge; otherwise the prefix has to be long enough to be
    worth walking."""
    if whole_held:
        return True
    if min_yards < 0.0:
        return False
    return proved >= min_yards


def step_may_bridge_gap(span: float, vertical_gap: float, step_yards: float,
                        max_gap: float) -> bool:
    """overseer_decisions.cpp:444. A normal walking step cannot bridge a large
    vertical gap (#203). Measured against the AIM, so it bounds a step only
    when the aim is within one step; a far aim's height is not this step's."""
    if span > step_yards:
        return True
    return abs(vertical_gap) <= max_gap


def ground_holds(mapid: int, from_x: float, from_y: float, from_z: float,
                 to_x: float, to_y: float):
    """mod_overseer.cpp:13561 GroundHolds. Returns (held, footing, proved).

    `proved` is the length of the line the check actually WALKED before it
    answered: the whole span on a true, and the prefix that held on a false.
    On a false, `footing` is left holding the surface at that proved prefix.
    """
    dx = to_x - from_x
    dy = to_y - from_y
    span = math.sqrt(dx * dx + dy * dy)
    footing = from_z
    proved = 0.0
    if span < TRAVEL_GROUND_SAMPLE_YARDS:
        return True, footing, span
    samples = int(span / TRAVEL_GROUND_SAMPLE_YARDS) + 1
    for i in range(1, samples + 1):
        t = i / samples
        sx = from_x + dx * t
        sy = from_y + dy * t
        # Looked for from one drop above the current footing, and then, if
        # nothing was found there, from much higher up: a stride onto a ledge
        # is not a stride into nothing, and the second ask is what tells them
        # apart (mod_overseer.cpp:13600-13618).
        ok, nxt = surface_at(mapid, sx, sy, footing + TRAVEL_GROUND_DROP_YARDS)
        if not ok:
            ok, nxt = surface_at(mapid, sx, sy, footing + TRAVEL_GROUND_UPHILL_YARDS)
            if not ok:
                return False, footing, proved
        if not footing_sample_holds(footing, nxt, TRAVEL_GROUND_DROP_YARDS,
                                    TRAVEL_GROUND_RISE_YARDS):
            return False, footing, proved
        footing = nxt
        proved = span * i / samples
    return True, footing, proved


# The two ways GroundedStep can answer no. They are different findings and the
# self-test asserts which one it got, because "refused" on its own is the one
# verdict a harness with no map data underneath it also produces.
REFUSED_VERTICAL_GAP = "vertical gap"
REFUSED_CONE_EXHAUSTED = "cone exhausted"


def grounded_step(mapid: int, bx: float, by: float, bz: float,
                  wx: float, wy: float, wz: float):
    """mod_overseer.cpp:14759 GroundedStep, without the navmesh branch (no
    mmaps here, see the module docstring) and without NothingInTheWay (no
    vmaps here). Returns (ok, tx, ty, footing, delta, reason)."""
    span = math.hypot(wx - bx, wy - by)

    # THE AIM'S OWN Z IS CORRECTED ONTO THE SURFACE UNDER IT, and only when the
    # character is near enough for that to be cheap (mod_overseer.cpp:14770).
    if span <= TRAVEL_STEP_YARDS:
        ok, surface = surface_at(mapid, wx, wy, wz + TRAVEL_GROUND_SNAP_YARDS)
        if ok and abs(surface - wz) <= TRAVEL_GROUND_SNAP_YARDS:
            wz = surface

    # Standing on it already. In the module this sits below the navmesh branch;
    # with no navmesh it is the first thing that can answer.
    if span < 1.0:
        return True, wx, wy, wz, 0.0, None

    if not step_may_bridge_gap(span, wz - bz, TRAVEL_STEP_YARDS,
                               TRAVEL_STEP_VERTICAL_YARDS):
        return False, bx, by, bz, None, REFUSED_VERTICAL_GAP

    bearing = math.atan2(wy - by, wx - bx)
    for delta in FAN:
        # An angled probe walks less than the way home, or it cannot be a step
        # forward either (mod_overseer.cpp:14843).
        if delta == 0.0:
            reach = min(span, TRAVEL_STEP_YARDS)
        else:
            reach = min(span * TRAVEL_STEP_SIDE_FRACTION, TRAVEL_STEP_YARDS)
        angle = bearing + delta
        sx = bx + math.cos(angle) * reach
        sy = by + math.sin(angle) * reach
        # A step to the side has to be a step FORWARD as well, or a character
        # at a dead end walks in a circle around it forever.
        if delta != 0.0 and math.hypot(wx - sx, wy - sy) >= span:
            continue
        held, footing, proved = ground_holds(mapid, bx, by, bz, sx, sy)
        # TAKE THE GROUND THAT WAS PROVED (#312). A bearing that broke on its
        # twelfth stride still walked eleven.
        walk_yards = reach if held else proved
        if not proven_step_is_worth_taking(held, proved, TRAVEL_STEP_MIN_YARDS):
            continue
        tx = bx + math.cos(angle) * walk_yards
        ty = by + math.sin(angle) * walk_yards
        return True, tx, ty, footing, delta, None
    return False, bx, by, bz, None, REFUSED_CONE_EXHAUSTED


def walk(mapid: int, name: str, sx: float, sy: float, sz: float,
         ax: float, ay: float, az: float, max_polls: int = 4000) -> dict:
    """Poll GroundedStep the way DriveTravel does until it arrives or stops.

    Arrival is TRAVEL_ARRIVED_POSITION_YARDS because both control walks aim at
    a position rather than at a creature. Two outcomes other than ARRIVED and
    REFUSED exist so that neither can be mistaken for the other: STALLED is a
    step that was granted but moved nothing, and MAXPOLLS is a walk that never
    resolved.
    """
    x, y, z = sx, sy, sz
    travelled = 0.0
    for poll in range(max_polls):
        remaining = math.hypot(ax - x, ay - y)
        if remaining <= TRAVEL_ARRIVED_POSITION_YARDS:
            return dict(name=name, outcome="ARRIVED", reason=None, polls=poll,
                        travelled=travelled, x=x, y=y, z=z, remaining=remaining)
        ok, tx, ty, tz, _delta, reason = grounded_step(mapid, x, y, z, ax, ay, az)
        if not ok:
            return dict(name=name, outcome="REFUSED", reason=reason, polls=poll,
                        travelled=travelled, x=x, y=y, z=z, remaining=remaining)
        moved = math.hypot(tx - x, ty - y)
        if moved < 1e-4:
            return dict(name=name, outcome="STALLED", reason=None, polls=poll,
                        travelled=travelled, x=x, y=y, z=z, remaining=remaining)
        travelled += moved
        x, y, z = tx, ty, tz
    return dict(name=name, outcome="MAXPOLLS", reason=None, polls=max_polls,
                travelled=travelled, x=x, y=y, z=z,
                remaining=math.hypot(ax - x, ay - y))


def bearing_ring(mapid: int, x: float, y: float, z: float, aim_x: float,
                 aim_y: float, reach: float):
    """The 36-bearing ring quoted in #316: how many directions out of a full
    circle hold ground, and how many of the five GroundedStep would actually
    try. A place where 21 of 36 hold and 0 of 5 in the cone do is a character
    that is not stuck and is not being misread; it is aimed into a wall."""
    held = []
    refused = []
    aim_bearing = math.atan2(aim_y - y, aim_x - x)
    for i in range(36):
        ang = math.radians(i * 10.0)
        tx = x + math.cos(ang) * reach
        ty = y + math.sin(ang) * reach
        ok, _f, _p = ground_holds(mapid, x, y, z, tx, ty)
        (held if ok else refused).append(i * 10)
    cone = []
    for delta in FAN:
        ang = aim_bearing + delta
        tx = x + math.cos(ang) * reach
        ty = y + math.sin(ang) * reach
        ok, _f, _p = ground_holds(mapid, x, y, z, tx, ty)
        cone.append((round(math.degrees(delta)), ok))
    return held, refused, cone


# --------------------------------------------------------------------------
# THE FIXTURES. Every number below was measured once and is now an expectation.
# --------------------------------------------------------------------------

KALIMDOR = 1

# READER VALIDATION. Five roster characters standing still, their live
# `characters.position_z` on the left and the terrain this reader computes
# under the same x and y on the right. Five positions across three tiles
# (0014636, 0014536, 0014736), plus a fourth (0014539) under the aim. If the
# triangle selection, the integer decoding or the grid indexing were wrong,
# these would not agree to a fraction of a yard on any tile, let alone four.
#
#   name, x, y, live position_z, terrain this reader computes
FAMILY = (
    ("Grug", -7656.7, -2346.4, -189.400, -189.931),
    ("Bork", -7455.1, -2487.6, -184.400, -184.452),
    ("Ugga", -7455.1, -2487.7, -184.300, -184.340),
    ("Grog", -8005.2, -2210.1, -152.700, -152.634),
    ("Og",   -8004.8, -2178.1, -174.300, -174.376),
)

# The Un'Goro errand those five were carrying when the refusals were logged.
AIM = (-7203.1, -3821.1, 8.6)
AIM_TERRAIN = 8.401

# A terrain reading may differ from a live position_z by this much and still
# count as the same ground. The worst of the five is Grug at 0.531, which is a
# character standing on a slope where half a yard of x buys half a yard of z.
READER_DELTA_TOLERANCE = 0.60

# A recomputed terrain height must reproduce the recorded one this closely.
# This is the drift check rather than the validity check: it is not asking
# whether the reader is right, it is asking whether it still does what it did.
READER_DRIFT_TOLERANCE = 0.01

# REPLAY VALIDATION. The Wailing Caverns approach terrace, from
# mod_overseer.cpp:21019 and tests/test_route_around.cpp:79. The two start
# points are travel nodes 2959 and 138, whose coordinates that test file states
# were read out of the realm's own playerbots_travelnode table
# (tests/test_route_around.cpp:88 and :93).
DOOR = (-705.0, -2045.0)
DOOR_TERRAIN = 110.718

SISHIR = ("Sishir Canyon (node 2959)", 509.9, 606.9, 73.2)
RATCHET = ("Ratchet spirithealer (node 138)", -1073.0, -3479.0, 63.0)

# What the published comments say, and what this replay reproduces.
#
# SISHIR. mod_overseer.cpp:13760 and overseer_decisions.h:7341 both say the
# walk is "refused every bearing after EIGHTY-FOUR YARDS". This replay stops at
# 85.75, which is the same finding to within one stride of the four-yard
# sampler, and it stops with the cone exhausted, which is the "refused every
# bearing" the comments describe.
SISHIR_TRAVELLED = 85.75
SISHIR_PUBLISHED = 84.0
SISHIR_PUBLISHED_TOLERANCE = 2.0

# RATCHET. mod_overseer.cpp:13765 and overseer_decisions.h:7346 say the same
# replay from Ratchet "ARRIVES" over 1465 yards of flat Barrens. This replay
# walks 1440.0 of the 1480.5 yards, in twenty-four full sixty-yard strides with
# nothing refused, and then stops 40.5 yards out.
#
# IT STOPS FOR A REASON THAT IS NOT THE FOOTING CHECK, and the difference
# matters. The last forty yards fall 41.7 yards into the Wailing Caverns
# sinkhole at about four yards per four-yard stride, which GroundHolds is
# perfectly happy with. What refuses is StepMayBridgeGap: once the aim is
# inside one step, a vertical gap over TRAVEL_STEP_VERTICAL_YARDS is refused
# outright (#203). In the module that last step would have been answered by the
# navmesh branch long before StepMayBridgeGap was reached, and this replay has
# no navmesh. So the published "ARRIVES" is not contradicted by this number; it
# is unreproducible here, in the one place where a terrain-only replay is
# STRICTER than the module rather than looser.
#
# The contrast the comments are actually drawing survives intact and is what
# the self-test asserts: 85.75 yards before the terrain says no, against 1440.0
# yards with the terrain saying nothing at all.
RATCHET_TRAVELLED = 1440.0
RATCHET_REMAINING = 40.47
RATCHET_PUBLISHED = 1465.0

# The published contrast, stated as the one inequality that carries it.
CONTRAST_RATIO = 15.0

DISTANCE_TOLERANCE = 0.5

# THE POSITIVE ASSERTION. A run that does not perform exactly this many checks
# has skipped something, and a skipped check is the false green this repository
# has already been bitten by four times. Change this number in the same commit
# that adds or removes a check, deliberately, or the self-test fails and says
# how many it actually ran. It has already earned its keep once: the first run
# of this file asserted 23 and performed 24, because the author miscounted.
EXPECTED_CHECKS = 24


# --------------------------------------------------------------------------
# THE SELF-TEST
# --------------------------------------------------------------------------


class SelfTest:
    """A recorder, not an assert helper.

    Every comparison is appended whether it passed or failed, so the run can
    assert on the COUNT as well as on the verdicts. A check that never ran
    leaves a hole this can see; a bare assert would just not have fired.
    """

    def __init__(self, inject_drift: float = 0.0) -> None:
        self.checks: list = []
        # --fault-drill sets this to shift every measured number before it is
        # compared, which is how the failure branch gets exercised on demand.
        self.inject_drift = inject_drift

    def close(self, name: str, measured: float, expected: float,
              tolerance: float, unit: str = "yards") -> bool:
        measured = measured + self.inject_drift
        delta = abs(measured - expected)
        ok = delta <= tolerance
        self.checks.append((ok, name,
                            "measured %+.3f %s, expected %+.3f +/- %.3f (off by %.3f)"
                            % (measured, unit, expected, tolerance, delta)))
        return ok

    def at_least(self, name: str, measured: float, floor: float,
                 unit: str = "yards") -> bool:
        measured = measured + self.inject_drift
        ok = measured >= floor
        self.checks.append((ok, name, "measured %.3f %s, wanted at least %.3f %s"
                            % (measured, unit, floor, unit)))
        return ok

    def same(self, name: str, measured, expected) -> bool:
        ok = measured == expected
        self.checks.append((ok, name, "got %r, wanted %r" % (measured, expected)))
        return ok

    @property
    def failures(self) -> list:
        return [c for c in self.checks if not c[0]]

    def report(self, stream=sys.stdout) -> None:
        for ok, name, detail in self.checks:
            stream.write("  %-4s %-52s %s\n" % ("ok" if ok else "FAIL", name, detail))


def check_no_missing_tiles(t: SelfTest) -> None:
    """The check that stops absent data reading as a pass.

    With no tiles on disk every height is INVALID_HEIGHT, every stride fails to
    find ground, and both control walks report REFUSED with the cone exhausted.
    That is the correct verdict for Sishir and the wrong one for Ratchet, and
    nothing about the word "REFUSED" distinguishes the two cases. The distances
    do, and so does this.
    """
    t.same("map data: every tile touched was on disk",
           sorted("%03d%02d%02d.map" % (KALIMDOR, gx, gy) for gx, gy in MISSING),
           [])


def run_self_test(t: SelfTest, stream=sys.stdout) -> bool:
    stream.write("=" * 78 + "\n")
    stream.write("SELF-TEST  (terrain only: no vmaps, no mmaps, no liquid)\n")
    stream.write("maps: %s\n" % map_dir())
    if t.inject_drift:
        stream.write("FAULT INJECTED: every measured number shifted by %+.3f\n"
                     % t.inject_drift)
    stream.write("=" * 78 + "\n")

    # ---- reader validation: 12 checks -------------------------------------
    stream.write("\nreader validation: terrain under a known position vs its live z\n")
    for name, x, y, live_z, recorded in FAMILY:
        h = grid_height(KALIMDOR, x, y)
        t.close("reader drift: %s on %s" % (name, tile_name(KALIMDOR, x, y)),
                h, recorded, READER_DRIFT_TOLERANCE)
        t.close("reader vs live position_z: %s" % name,
                abs(h - live_z), 0.0, READER_DELTA_TOLERANCE)
    ax, ay, az = AIM
    ah = grid_height(KALIMDOR, ax, ay)
    t.close("reader drift: the aim on %s" % tile_name(KALIMDOR, ax, ay),
            ah, AIM_TERRAIN, READER_DRIFT_TOLERANCE)
    t.close("reader vs the aim's own z", abs(ah - az), 0.0, READER_DELTA_TOLERANCE)

    # ---- replay validation: 11 checks -------------------------------------
    stream.write("replay validation: the two walks the source comments quote\n")
    door_h = grid_height(KALIMDOR, *DOOR)
    t.close("terrain under the Wailing Caverns terrace", door_h, DOOR_TERRAIN,
            READER_DRIFT_TOLERANCE)

    name, sx, sy, sz = SISHIR
    sishir = walk(KALIMDOR, name, sx, sy, sz, DOOR[0], DOOR[1], door_h)
    t.same("Sishir: outcome", sishir["outcome"], "REFUSED")
    t.same("Sishir: refused how", sishir["reason"], REFUSED_CONE_EXHAUSTED)
    t.close("Sishir: yards walked before the refusal",
            sishir["travelled"], SISHIR_TRAVELLED, DISTANCE_TOLERANCE)
    t.close("Sishir: agrees with the published 84 yards",
            sishir["travelled"], SISHIR_PUBLISHED, SISHIR_PUBLISHED_TOLERANCE)

    name, rx, ry, rz = RATCHET
    ratchet = walk(KALIMDOR, name, rx, ry, rz, DOOR[0], DOOR[1], door_h)
    t.same("Ratchet: outcome", ratchet["outcome"], "REFUSED")
    t.same("Ratchet: refused how", ratchet["reason"], REFUSED_VERTICAL_GAP)
    t.close("Ratchet: yards walked", ratchet["travelled"], RATCHET_TRAVELLED,
            DISTANCE_TOLERANCE)
    t.close("Ratchet: yards short of the terrace", ratchet["remaining"],
            RATCHET_REMAINING, DISTANCE_TOLERANCE)
    t.at_least("Ratchet: within reach of the published 1465",
               ratchet["travelled"] + ratchet["remaining"], RATCHET_PUBLISHED)
    # The published contrast, as one number. Sishir is stopped by terrain in the
    # first hundred yards; Ratchet crosses the Barrens untroubled.
    t.at_least("the contrast: Ratchet walks this many times Sishir's yards",
               ratchet["travelled"] / max(sishir["travelled"], 1e-6),
               CONTRAST_RATIO, unit="x")

    # ---- data integrity: 1 check ------------------------------------------
    check_no_missing_tiles(t)

    stream.write("\n")
    t.report(stream)
    stream.write("\n")

    # THE SUCCESS BRANCH IS A POSITIVE ASSERTION, not the absence of a failure.
    # Three separate things have to be true, and the count is one of them.
    ran = len(t.checks)
    failed = len(t.failures)
    counted = ran == EXPECTED_CHECKS
    passed = counted and failed == 0

    if not counted:
        stream.write("FAIL: ran %d checks, expected exactly %d. A check was "
                     "skipped or added without updating EXPECTED_CHECKS.\n"
                     % (ran, EXPECTED_CHECKS))
    if failed:
        stream.write("FAIL: %d of %d checks did not hold.\n" % (failed, ran))
    if MISSING:
        stream.write("\nThe map tiles below were not found under %s:\n  %s\n"
                     "Nothing in this run means anything without them. "
                     "README.md says where to get them.\n"
                     % (map_dir(),
                        " ".join(sorted("%03d%02d%02d.map" % (KALIMDOR, gx, gy)
                                        for gx, gy in MISSING))))
    if passed:
        stream.write("PASS: %d of %d checks held, and all %d ran.\n"
                     % (ran - failed, ran, EXPECTED_CHECKS))
    return passed


# --------------------------------------------------------------------------
# THE FAULT DRILL. A check that can only report good news is not a check, and
# this repository has shipped four of those. So the failure branch is exercised
# on demand rather than assumed: two faults are injected, and the drill only
# passes if the self-test FAILED under both.
# --------------------------------------------------------------------------


def run_fault_drill(real_map_dir: str, stream=sys.stdout) -> bool:
    class Sink:
        def write(self, _s):
            return None

    sink = Sink()
    results = []

    stream.write("=" * 78 + "\n")
    stream.write("FAULT DRILL: the self-test must fail when it should\n")
    stream.write("=" * 78 + "\n")

    # FAULT 1: no map data at all. This is the important one. Without tiles the
    # walks still answer REFUSED, which is the right word for the wrong reason,
    # and only the distances and the missing-tile check catch it.
    with tempfile.TemporaryDirectory() as empty:
        set_map_dir(empty)
        drill = SelfTest()
        fired = not run_self_test(drill, sink)
        results.append(("no map data at all", fired, len(drill.failures),
                        len(drill.checks)))

    # FAULT 2: the tiles are there and the numbers have moved. Five yards is
    # larger than every tolerance in the fixtures, so every numeric check has
    # to notice.
    set_map_dir(real_map_dir)
    drill = SelfTest(inject_drift=5.0)
    fired = not run_self_test(drill, sink)
    results.append(("every measurement shifted by 5 yards", fired,
                    len(drill.failures), len(drill.checks)))

    for label, fired, failed, ran in results:
        stream.write("  %-4s %-40s self-test %s (%d of %d checks failed)\n"
                     % ("ok" if fired else "FAIL", label,
                        "failed as it should" if fired else "WRONGLY PASSED",
                        failed, ran))

    # The drill is itself a positive assertion: both faults must have fired,
    # and both must have been running a full-length self-test while they did.
    all_fired = all(r[1] for r in results)
    all_full = all(r[3] == EXPECTED_CHECKS for r in results)
    if not all_full:
        stream.write("\nFAIL: a drill ran a short self-test, so what it proved "
                     "is not what the gate runs.\n")
    if not all_fired:
        stream.write("\nFAIL: a fault did not fire. The self-test cannot be "
                     "trusted to report bad news.\n")
    if all_fired and all_full:
        stream.write("\nPASS: %d of %d faults produced a failing self-test.\n"
                     % (len(results), len(results)))
    # Leave the loader pointing back at the real tiles for anything after this.
    set_map_dir(real_map_dir)
    return all_fired and all_full


# --------------------------------------------------------------------------
# THE SURVEY. Not a gate. This is the exploratory half: the thing to run when a
# character is stopped somewhere new and the question is what the ground under
# it looks like.
# --------------------------------------------------------------------------


def run_survey(stream=sys.stdout) -> None:
    ax, ay, az = AIM
    stream.write("=" * 78 + "\n")
    stream.write("SURVEY  (terrain only: no vmaps, no mmaps, no liquid, so a\n")
    stream.write("refusal here is a lower bound on the module's strictness and\n")
    stream.write("a walk here is not a promise that the module could walk it)\n")
    stream.write("maps: %s\n" % map_dir())
    stream.write("=" * 78 + "\n")

    stream.write("\nthe errand each character was carrying, replayed\n")
    stops = []
    for name, x, y, live_z, _recorded in FAMILY:
        straight = math.hypot(ax - x, ay - y)
        r = walk(KALIMDOR, name, x, y, live_z, ax, ay, az)
        stops.append(r)
        stream.write("  %-5s %7.1f yards out -> %s%s after %d polls, %.1f walked\n"
                     % (name, straight, r["outcome"],
                        "" if not r["reason"] else " (%s)" % r["reason"],
                        r["polls"], r["travelled"]))
        stream.write("        stopped at (%.1f, %.1f, %.1f), still %.1f out\n"
                     % (r["x"], r["y"], r["z"], r["remaining"]))

    stream.write("\nthe 36-bearing ring where each walk stopped, at two strides\n")
    for r in stops:
        held, _refused, cone = bearing_ring(KALIMDOR, r["x"], r["y"], r["z"],
                                            ax, ay, TRAVEL_GROUND_SAMPLE_YARDS * 2)
        cone_held = sum(1 for _d, ok in cone if ok)
        stream.write("  %-5s %2d of 36 bearings hold, %d of the 5 in the cone; "
                     "aim bears %.1f deg\n"
                     % (r["name"], len(held), cone_held,
                        math.degrees(math.atan2(ay - r["y"], ax - r["x"])) % 360.0))
        stream.write("        holding: %s\n" % (held,))

    gname, gx0, gy0, gz0, _recorded = FAMILY[0]
    stream.write("\nthe same ring at %s's start, widening the reach\n" % gname)
    for reach in (8.0, 12.0, 20.0, 30.0, 40.0, 60.0):
        held, _refused, cone = bearing_ring(KALIMDOR, gx0, gy0, gz0,
                                            ax, ay, reach)
        cone_held = sum(1 for _d, ok in cone if ok)
        stream.write("  reach %4.0f yards: %2d of 36 hold, %d of 5 in the cone\n"
                     % (reach, len(held), cone_held))

    stream.write("\nterrain along %s's straight line to the aim, every %.0f yards\n"
                 % (gname, TRAVEL_GROUND_SAMPLE_YARDS))
    dx = ax - gx0
    dy = ay - gy0
    tot = math.hypot(dx, dy)
    prev = None
    worst = (0.0, 0.0)
    for i in range(0, 101):
        d = i * TRAVEL_GROUND_SAMPLE_YARDS
        px = gx0 + dx * d / tot
        py = gy0 + dy * d / tot
        h = grid_height(KALIMDOR, px, py)
        if prev is not None and h - prev > worst[0]:
            worst = (h - prev, d)
        if i % 5 == 0:
            flag = ""
            if prev is not None and abs(h - prev) > TRAVEL_GROUND_RISE_YARDS:
                flag = "   <-- over the %.0f yard stride bound" % TRAVEL_GROUND_RISE_YARDS
            stream.write("    %5.0f yards out: terrain %+9.2f%s\n" % (d, h, flag))
        prev = h
    stream.write("    steepest single %.0f yard stride in the first %.0f yards: "
                 "%.2f of rise, %.0f yards out\n"
                 % (TRAVEL_GROUND_SAMPLE_YARDS, 100 * TRAVEL_GROUND_SAMPLE_YARDS,
                    worst[0], worst[1]))

    if MISSING:
        stream.write("\nWARNING: %d tile(s) under this survey were not on disk, "
                     "so some of it is reading empty space rather than terrain: "
                     "%s\n" % (len(MISSING),
                               " ".join(sorted("%03d%02d%02d.map" % (KALIMDOR, gx, gy)
                                               for gx, gy in MISSING))))


# --------------------------------------------------------------------------


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Replay GroundedStep over the shipped heightmap grids. "
                    "Terrain only: no vmaps, no mmaps, no liquid.")
    parser.add_argument("--maps", default=None,
                        help="directory holding the extracted .map tiles "
                             "(default: $%s, else ./maps beside this script)"
                             % MAP_DIR_ENV)
    parser.add_argument("--survey", action="store_true",
                        help="print the exploratory report instead of the self-test")
    parser.add_argument("--fault-drill", action="store_true",
                        help="prove the self-test fails when it should, then exit")
    args = parser.parse_args(argv)

    if args.maps:
        set_map_dir(args.maps)

    if not os.path.isdir(map_dir()):
        sys.stderr.write(
            "no map directory at %s\n"
            "The .map tiles are extracted game data and are deliberately not in "
            "this repository. See README.md in this directory for how to get "
            "them, then pass --maps DIR or set %s.\n" % (map_dir(), MAP_DIR_ENV))
        return 2

    if args.fault_drill:
        return 0 if run_fault_drill(map_dir()) else 1

    if args.survey:
        run_survey()
        # A survey is not a gate, but it must not report cheerfully over
        # missing data either.
        return 1 if MISSING else 0

    return 0 if run_self_test(SelfTest()) else 1


if __name__ == "__main__":
    sys.exit(main())
