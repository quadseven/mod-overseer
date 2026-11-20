# zone-band

Projects the per-zone creature level band into `data/zone_band.json` (#1518),
and that table into `src/overseer_zone_band.h`, which the module compiles in.
The module's individual mode reads the header to decide whether a member
standing in a zone may roam alone: only when the zone's band top is at most the
member's level plus one.

## Method

- Band: for each zone, the 10th to 90th percentile (nearest rank) of the
  levels of hostile creature spawns. The low end is the percentile of each
  template's `minlevel`, the top the percentile of its `maxlevel`.
- Hostile: the creature's faction template is hostile to the side's player
  template (Human, id 1, for the alliance; Orc, id 2, for the horde), by the
  core's rule: a named enemy is hostile, a named friend is not, otherwise the
  creature's hostile mask against the player's own mask.
- Zone of a spawn: the tile is `int(32 - x / 533.33333)` by `int(32 - y /
  533.33333)`, the file is `<map 3 digits><tile x 2><tile y 2>.map`, the cell
  inside the tile is `int(16 * fraction)` per axis laid out `x * 16 + y`, and
  the area id read there goes through AreaTable's parent to the zone. The
  method matched the core's own `creature.zoneId` on every populated row.
- Level 0 templates and placeholders above 83 are skipped.
- A zone with no hostile spawn for a side has `null` for that side, which means
  no band and no grant. `families_stand_in` lists the zones the family members
  stood in when the table was made, and every one of them has an entry.

## Inputs

- `--maps`: the extracted `.map` tiles of the server's data directory.
- `--dbc`: the data directory's `dbc/` with `AreaTable.dbc` and
  `FactionTemplate.dbc`.
- `--spawns`: tab separated, no header, from the world database:

      select c.map, c.position_x, c.position_y, t.minlevel, t.maxlevel, t.faction
      from creature c join creature_template t on t.entry = c.id;

- `--roster-zones` (optional): tab separated `map`, `zone` of each family
  member, from `characters` joined to `overseer_roster`.

Extracted game data is not committed; only the table of numbers made from it is.

## Run

    python3 tools/zone-band/band.py --maps <maps dir> --dbc <dbc dir> \
        --spawns spawns.tsv --roster-zones roster.tsv --out data/zone_band.json

The tool exits 2 with a named reason when an input is missing. Tests:
`python3 -B tests/test_zone_band.py` (synthetic files, no game data).

## The header

The module reads no file at run time, so the band tops are compiled in. After
any change to `data/zone_band.json`, project the header again. This step reads
only the checked-in table and needs no game data:

    python3 tools/zone-band/band.py --header src/overseer_zone_band.h

`--table <file>` names another table to read. Giving `--out` and `--header`
together builds the table and projects the header from it in one run. The
header holds one row per zone with the band top for each side, zero where the
side has no band. `tests/test_zone_band.py` fails when the header and the table
disagree, so a table change without this step does not pass the `decisions`
check.
