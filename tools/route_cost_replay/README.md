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

## Building the input from exports

`build_input.py` turns read-only exports into the JSON above. Take every query
in one session, as `mysql -B -N` output (tab separated, no header), for the
maps measured (0 and 1):

```sql
-- nodes.tsv
select id, map_id, x, y, z from playerbots_travelnode where map_id in (0,1);
-- links.tsv (type 1 is walk)
select node_id, to_node_id, type, distance, extra_cost from playerbots_travelnode_link;
-- paths.tsv, ordered by nr
select p.node_id, p.to_node_id, p.x, p.y, p.z from playerbots_travelnode_path p
  join playerbots_travelnode n on n.id = p.node_id where n.map_id in (0,1)
  order by p.node_id, p.to_node_id, p.nr;
-- spawns.tsv, spawn override wins over the template, as in CanBeFought
select c.guid, c.map, c.position_x, c.position_y, c.position_z, t.maxlevel,
  t.`rank`, t.detection_range, t.faction,
  if(c.unit_flags <> 0, c.unit_flags, t.unit_flags), t.flags_extra,
  ifnull(g.eventEntry, 0)
  from creature c join creature_template t on t.entry = c.id
  left join game_event_creature g on g.guid = c.guid where c.map in (0,1);
```

`FactionTemplate.dbc` comes from the pinned core's data directory (it is a DBC,
not a table). Then:

```powershell
python tools/route_cost_replay/build_input.py --nodes nodes.tsv --links links.tsv `
  --paths paths.tsv --spawns spawns.tsv --factions FactionTemplate.dbc --output route-input.json
```

Player templates are fixed at 1 (Human, Alliance) and 2 (Orc, Horde). A spawn
linked to a game event is treated as not standing, because the export does not
know which events are running; a spawn in several events is read once.

## Live replay percentages

Measured on the dev realm's rows, 2026-11-17, walker level 60, one snapshot:
1,260 nodes and 5,247 directed walk legs on the two continents, 60,999 spawn
rows (none without a template), 841 faction templates. Second searches are
over every reachable ordered node pair with at least 200 yards of progress
(Eastern Kingdoms 384,505 routes, Kalimdor 262,174).

| Continent, side | today | B | A at N=2 | A at N=3 |
|---|---|---|---|---|
| Eastern Kingdoms, Alliance: second search | 19.15% | 30.27% | 98.13% | 94.97% |
| Eastern Kingdoms, Alliance: legs marked | 5.49% | 8.88% | 60.78% | 45.29% |
| Eastern Kingdoms, Horde: second search | 11.67% | 23.32% | 99.07% | 96.68% |
| Eastern Kingdoms, Horde: legs marked | 4.44% | 7.87% | 65.73% | 48.72% |
| Kalimdor, Alliance: second search | 10.53% | 35.45% | 96.50% | 90.37% |
| Kalimdor, Alliance: legs marked | 6.01% | 14.33% | 51.65% | 34.22% |
| Kalimdor, Horde: second search | 2.75% | 29.03% | 93.03% | 81.20% |
| Kalimdor, Horde: legs marked | 4.48% | 12.79% | 46.93% | 30.79% |

Limits of this measure: it is a graph-wide workload, not observed requests, and
one walker level. The two player templates stand in for every race of a side.
