# GroundedStep terrain replay

`replay_step.py` walks this module's own step chooser over the shipped
heightmap grids, with no server, no database and no bot. It answers one
question: **when a character refuses to take a step, is that the terrain or is
that the code?**

## Why this exists

Two comments in this repository quote concrete distances:

- `src/mod_overseer.cpp:13759-13766`
- `src/overseer_decisions.h:7340-7346`

Both say the measurements came from "replaying GroundedStep stride for stride
against the shipped heightmap grids". The harness that produced them was
scratch tooling from the #316 session and was never committed, so it has since
been rebuilt from nothing twice. This directory is that harness, kept.

It is also the first thing in this repository that exercises the step chooser
against real map data rather than against hand-written fixtures. See #116,
which asks for a test harness for the pure decisions; this does not close it,
because this is a Python replay of the C++ rather than a test of the C++, and
because #116 is about `src/overseer_decisions.cpp` as a whole.

## What it measures

- The height of the terrain under any point on any map, read straight out of
  the extracted `.map` tiles with AzerothCore's own interpolation.
- `GroundHolds`: whether a straight line between two points is walkable,
  sampled every `TRAVEL_GROUND_SAMPLE_YARDS`, and how many yards of it held
  before it did not.
- `GroundedStep`: the five-bearing cone, the forward-progress rule, the
  proved-prefix rule and the vertical-gap rule, polled until a walk arrives or
  is refused.
- **Which of the two refusals it got.** `GroundedStep` can say no because the
  aim is inside one step and more than `TRAVEL_STEP_VERTICAL_YARDS` above or
  below (the vertical gap, #203), or because all five bearings in the cone were
  refused (the cone exhausted, #312). These are different findings and the
  self-test asserts which one occurred, because "refused" on its own is also
  what a harness with no map data underneath it reports.
- The 36-bearing ring from #316: how many directions out of a full circle hold
  ground, against how many of the five the cone would actually try.

## What it deliberately does not

**Terrain only. No vmaps, no mmaps, no liquid.** This caveat is load bearing
and must not be dropped from any summary of what this tool says.

| Omission | Which way it cuts |
| --- | --- |
| **vmaps** | Nothing standing *on* the ground is modelled, so `NothingInTheWay` (`mod_overseer.cpp:14881`) is never replayed. Every wall, tree, building and railing is invisible. **More permissive than the module.** |
| **liquid** | `SurfaceAt` asks `Map::GetWaterOrGroundLevel`, which answers with a water surface where there is water. Here a river reads as its bed. **Usually stricter, and never a reason to trust a walk.** |
| **mmaps** | The navmesh branch at the top of `GroundedStep` (`mod_overseer.cpp:14785`) is skipped, so every step falls through to the footing check. The real module often never reaches that check. **Stricter than the module.** |

So a refusal this tool reports is a **lower bound** on the module's strictness
over open ground. Where it refuses on a rise or a drop, the module refuses too,
because it is reading the same heightmap through the same arithmetic. Where it
*walks*, the module might still refuse, because something this tool cannot see
is standing in the way.

**Nothing here proves a route is walkable. It proves a route is not.**

## Getting the map tiles

The `.map` tiles are extracted game data and are **not in this repository and
must not be**. `.gitignore` in this directory refuses them and the `maps/`
directory they belong in.

The self-test reads 11 tiles of map 001 (Kalimdor), about 0.7 MB. The survey
reads a wider set. The working set extracted for #316 was 356 tiles, 26 MB
(54 MB counting the tar archives it arrived in).

Two ways to get them:

1. **Out of a worldserver you already run.** A worldserver has the extracted
   tiles in the directory its `DataDir` setting points at, under `maps/`. Copy
   them out read only, never writing anything into the server:

   ```bash
   mkdir -p tools/groundedstep-replay/maps
   kubectl exec -n <namespace> <worldserver-pod> -- \
     tar cf - -C <datadir>/maps . \
     | tar xf - -C tools/groundedstep-replay/maps
   ```

   `tar cf -` to stdout is the whole point: it reads, it does not mount, it
   does not write, and it leaves no artifact in the container. Restrict it to
   the tiles you need by naming them instead of `.` if 26 MB over an exec
   channel is unwelcome. For a plain Docker host the same thing is
   `docker cp <container>:<datadir>/maps tools/groundedstep-replay/maps`.

2. **From a game client.** AzerothCore ships `mapextractor` in its tools build.
   Run it against a client of the right version and it writes the same tiles.
   This is the route to use if you do not have a server to copy from.

Either way, point the script at them:

```bash
python3 replay_step.py --maps /path/to/maps
export OVERSEER_REPLAY_MAPS=/path/to/maps   # or this
```

With neither, it defaults to `maps/` beside the script, and exits 2 with an
explanation if that does not exist.

## Running it

```bash
python3 tools/groundedstep-replay/replay_step.py                # the self-test
python3 tools/groundedstep-replay/replay_step.py --survey       # the report
python3 tools/groundedstep-replay/replay_step.py --fault-drill  # prove it can fail
```

No dependencies beyond the Python standard library. Python 3.9 or newer.

Exit codes: `0` pass, `1` a check did not hold, `2` no map directory.

The self-test is the gate. The survey is the exploratory half: the thing to run
when a character has stopped somewhere new and the question is what the ground
under it looks like. The fault drill is below.

## The two validations

Both are in the self-test, so they run rather than being asserted in a README.

### 1. Reader validation: the terrain matches where characters actually stand

Five roster characters standing still, their live `characters.position_z`
against the terrain this reader computes under the same x and y. Five positions
across three tiles, plus a fourth tile under the errand's aim. If the triangle
selection, the integer decoding or the grid indexing were wrong, these would
not agree to a fraction of a yard on one tile, let alone four.

| | tile | live `position_z` | terrain | delta |
| --- | --- | --- | --- | --- |
| Grug | `0014636.map` | -189.400 | -189.931 | 0.531 |
| Bork | `0014536.map` | -184.400 | -184.452 | 0.052 |
| Ugga | `0014536.map` | -184.300 | -184.340 | 0.040 |
| Grog | `0014736.map` | -152.700 | -152.634 | 0.066 |
| Og | `0014736.map` | -174.300 | -174.376 | 0.076 |
| the aim | `0014539.map` | +8.600 | +8.401 | 0.199 |

Grug's half yard is a character standing on a slope, where half a yard of x
buys half a yard of z. The tolerance is 0.60.

Each row is asserted twice: once that the computed height still reproduces the
recorded one to within 0.01 (a drift check on this file), and once that it
agrees with the live z to within 0.60 (the validity check on the reader).

### 2. Replay validation: it reproduces the numbers already in the source

The aim for both walks is the Wailing Caverns approach terrace,
`(-705, -2045)`, from `mod_overseer.cpp:21019`. The two start points are travel
nodes 2959 and 138, whose coordinates `tests/test_route_around.cpp:88` and `:93`
state were read out of the realm's own `playerbots_travelnode` table.

**Sishir Canyon, node 2959.** `mod_overseer.cpp:13760` and
`overseer_decisions.h:7341` both say the walk is "refused every bearing after
EIGHTY-FOUR YARDS". This replay stops at **85.75 yards**, with the cone
exhausted, 2835.8 yards from the terrace. Same finding, to within one stride of
the four yard sampler.

**Ratchet spirithealer, node 138.** The same two comments say this one
"ARRIVES" over 1465 yards of flat Barrens. This replay walks **1440.0 of the
1480.5 yards**, in twenty-four full sixty-yard strides with nothing refused,
and then stops 40.5 yards out.

That last 40 yards is worth being precise about, because it is the one place
where the replay and the published claim disagree:

- It does **not** stop on the footing check. The last forty yards fall 41.7
  yards into the Wailing Caverns sinkhole at about four yards per four yard
  stride, which `GroundHolds` is perfectly happy with.
- It stops on `StepMayBridgeGap`. Once the aim is inside one step, a vertical
  gap over `TRAVEL_STEP_VERTICAL_YARDS` is refused outright (#203).
- In the module, that final step would have been answered by the navmesh branch
  long before `StepMayBridgeGap` was reached. This replay has no navmesh.

So the published "ARRIVES" is not contradicted here. It is unreproducible here,
in exactly the place where a terrain-only replay is stricter than the module
rather than looser. What the comments are actually drawing, and what the
self-test asserts, survives intact: **85.75 yards before the terrain says no,
against 1440.0 yards with the terrain saying nothing at all.** A factor of 16.8.

## Why the self-test counts its checks

A check that can only report good news is not a check, and this repository has
shipped four of those. Two devices guard against a fifth here.

**The success branch is a positive assertion.** The run passes only when the
number of checks performed equals a hard coded `EXPECTED_CHECKS`, *and* none of
them failed, *and* every map tile touched was actually on disk. A check that
was skipped leaves a hole the count can see; a bare `assert` would simply not
have fired. This earned its keep on the first run of the file, which asserted
23 checks and performed 24 because the author miscounted.

**The missing-tile check is not paranoia.** With no tiles on disk every height
reads `INVALID_HEIGHT`, every stride fails to find ground, and both control
walks report `REFUSED`. That is the *correct* verdict for Sishir and the wrong
one for Ratchet, and nothing about the word "REFUSED" distinguishes them. Run
`--fault-drill` and it says so out loud: with the map directory emptied, 4 of
the 24 checks still read green.

**`--fault-drill` proves the failure branch fires** rather than assuming it.
It injects two faults and passes only if the self-test failed under both, and
only if both were running a full length self-test while they did:

```
  ok   no map data at all                       self-test failed as it should (20 of 24 checks failed)
  ok   every measurement shifted by 5 yards     self-test failed as it should (17 of 24 checks failed)

  PASS: 2 of 2 faults produced a failing self-test.
```

## Where every rule came from

Each transcribed rule names its source line in the code. AzerothCore's, at the
core SHA pinned in `.github/workflows/build.yml`:

| | |
| --- | --- |
| `GridTerrainData.cpp:237-320` | `getHeightFromFloat`, the v9/v8 triangle interpolation |
| `GridTerrainData.cpp:322-460` | `getHeightFromUint8` / `getHeightFromUint16` |
| `GridTerrainData.cpp:462-475` | `isHole` |
| `Map.cpp:1118-1136` | `Map::GetWaterOrGroundLevel` and `Z_OFFSET_FIND_HEIGHT` |
| `Map.cpp:1166-1198` | `Map::GetHeight` and its `z >= gridHeight - 0.05` gate |
| `GridDefines.h:186-190` | `Acore::ComputeGridCoord` |

And this repository's own:

| | |
| --- | --- |
| `mod_overseer.cpp:13479-13493` | `SurfaceAt` |
| `mod_overseer.cpp:13561-13650` | `GroundHolds` |
| `mod_overseer.cpp:14759-14885` | `GroundedStep` |
| `overseer_decisions.cpp:444` | `StepMayBridgeGap` |
| `overseer_decisions.cpp:453` | `FootingSampleHolds` |
| `overseer_decisions.cpp:464` | `ProvenStepIsWorthTaking` |

Every module constant in the script carries the `mod_overseer.cpp` line it is
declared on. If one of them changes in the C++ and not here, the distances the
self-test asserts move and the run says so.

## This is not in CI

Deliberately, for now. The gate would need 0.7 MB of extracted game data in a
runner, and this repository does not ship game data or have anywhere to put it.
Run it by hand when a travel refusal needs explaining, or wire it up in a
consuming repo that already has a `DataDir`.
