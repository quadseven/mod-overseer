# Route cost replay

This tool estimates how often `PlanFootRoute` enters its second Dijkstra search
under the current guard rule, B alone, and A at density thresholds 2 and 3. It
does not connect to a realm or change route settings. It reads one normalized
JSON snapshot containing survey rows, path points, creature spawns, and core
faction templates.

Run it with:

```powershell
python tools/route_cost_replay/replay_routes.py --input route-input.json --level 60
```

The input object has these fields:

- `nodes`: objects with `id`, `map_id`, `x`, `y`, and `z` from
  `playerbots_travelnode`.
- `links`: directed objects with `source`, `target`, `kind`, `distance`, and
  optional `extra_cost` from `playerbots_travelnode_link`. Include ordered
  `path` points (`[x, y, z]`) from `playerbots_travelnode_path` when present.
- `spawns`: creature spawn objects with `map_id`, `x`, `y`, `z`, `max_level`,
  `rank`, `detection_range`, and `faction_template_id`. Also include
  `can_be_fought`, `trigger`, `civilian`, and `active`, derived using the pinned
  core's `CanBeFought`, trigger/civilian, and spawn-active rules.
- `factions`: faction-template objects with `id`, `faction`, `our_mask`,
  `hostile_mask`, and all four `enemy_factions` and `friend_factions` slots.
- `player_templates`: the faction-template IDs for `alliance` and `horde`.

Use one consistent snapshot for all tables. Include every directed link and
every creature spawn for the measured maps. Missing maps, spawns, faction rows,
player templates, or required spawn flags produce `NOT MEASURED` output.

## What the percentages mean

The workload is every reachable ordered pair of survey nodes on a continent
whose straight-line progress is at least 200 yards, matching the planner's
minimum-gain gate. It is a graph-wide route workload, not a count of observed
live requests. A route enters the second-search count when its baseline
shortest path contains a marked leg and rounding is allowed.

For each continent and player side, the report gives:

- marked directed walk legs divided by all eligible directed walk legs;
- second searches divided by all eligible ordered node-pair routes.

`today` mirrors the current other-side skull rule. `b_only` adds the rank 1 or
2, non-grey hostile rule. `a_n2` and `a_n3` add lone-walker density at the
given count, using each spawn's detection range plus 5 yards around either leg
endpoint. They include the B rule as well.

## Live replay percentages (open)

| Result | Status |
|---|---|
| Real replay percentages by continent and player side | **OPEN - NOT MEASURED** |

To fill this box, take one consistent, sanitized snapshot for the measured maps:
all relevant rows from `playerbots_travelnode`, every directed link and ordered
path point from `playerbots_travelnode_link` and `playerbots_travelnode_path`,
all corresponding `creature` spawn rows and their `creature_template` values,
the pinned core's faction-template data, and the Alliance and Horde player
template faction IDs. Derive `can_be_fought`, `trigger`, `civilian`, and `active`
with the pinned core's rules. Do not report percentages until that complete
snapshot has been replayed.
