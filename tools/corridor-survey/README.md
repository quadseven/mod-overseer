# Mmap corridor survey

`survey.py` reads extracted AzerothCore `.mmtile` files and reports a grounded
walk from a reachable point to a dungeon door. Coordinates use the world's
`x y z` order. The tool does not edit the tiles or write extracted data.

```sh
python3 tools/corridor-survey/survey.py \
  --maps /path/to/extracted/maps \
  --map-id 1 \
  --start -1050 -3664.8 24.36 \
  --door -733.7 -2214.9 17.3 \
  --spacing-yards 192
```

The default spacing is 192 yards, the longest horizontal spacing in the
committed Wailing Caverns corridor. Every emitted horizontal leg is at most
that long; turns found by the polygon funnel remain as route points. The
default output is a C++-style `RoutePoint` initializer and a table of horizontal
and walked leg lengths. Add
`--json` for machine-readable `points` and `legs`; every point carries its
map/tile/polygon identifier and a height read from that polygon.

## Route and approach rule

The core's nearest-polygon query accepts points within 3 yards on each
horizontal axis and 50 yards vertically. The survey projects the start and
door onto the closest ground polygon inside those bounds. It builds adjacency
from internal Detour neighbors and from the overlapping, climbable portions of
external portals between neighboring tiles, following the pinned Detour
connection rules. Off-mesh connections are ignored. A shortest polygon
corridor is selected with centroid-distance edge costs; the funnel then pulls
a walk through that corridor. The route is resampled to the requested maximum
spacing, with each sample's ground height interpolated from the polygon's
detail triangles (or its base polygon when no detail mesh is present).

The parser accepts the pinned AzerothCore/MMAP format used by this module:
MMap generator version 20 and Detour tile version 7. It validates both headers,
tile coordinates, array bounds and file length. A bad, old or truncated tile is
reported by filename. A disconnected start and door fail with the named reason
`no polygon path links start and door`.

## Getting tiles

Use the `maps/` directory under the `DataDir` of an existing worldserver, or
extract the maps with the matching AzerothCore `mapextractor`. Copy only the
needed `.mmtile` files to a local directory and pass that directory with
`--maps`. The files contain extracted game data and must not be committed.
Worldserver access is not needed by this tool.

For a real run, preserve the command, output, and tile set's extraction version
as manual evidence on the change. The synthetic test below does not claim to
reproduce any extracted tile.

## Tests

```sh
python3 -B tests/test_corridor_survey.py
```

The test script writes small synthetic Detour tiles into a temporary directory.
It checks a cross-tile route and named failures, then builds synthetic corridor
tiles from the expected Wailing Caverns and Ragefire rows and checks the survey
output against the committed rows within 0.05 yards. No extracted data is used.
