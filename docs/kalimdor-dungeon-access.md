# Kalimdor dungeon access, per faction

Which pre-Molten-Core dungeons on Kalimdor the dungeon runner can reach, for each faction, and what stands in the way. Every number comes from a table: the portal rows in `DungeonPortals()` (`src/mod_overseer.cpp`), the world database's `areatrigger`, `areatrigger_teleport`, `dungeon_access_template`, `gameobject` and `creature` rows, `Lock.dbc` and `DungeonEncounter.dbc`, the shipped navmesh tiles, and the travel survey (`playerbots_travelnode*`). The live record is the dev realm's `overseer_dungeon_run` table on 2026-09-24.

Eastern Kingdoms doors are not covered here.

## How to read the table

- **Rows**: the portal keywords a `dungeon:<keyword>` job may name.
- **Door level**: the core's `dungeon_access_template.min_level`. The core refuses a member under it at the knock. Since #674 the coordinator refuses to open the run first and logs who is under it.
- **Key**: a locked gameobject on the approach, and the item its lock names in `Lock.dbc`. Since #674 a run is not opened until one member carries it.
- **Approach**: whether the row carries a measured corridor and a home inn. With none, the leader is routed by the survey and then walked straight at the derived staging point.
- **Survey reach**: whether any overworld survey node links into the door's own exit node. When none does, the survey cannot plan the last stretch, and the walk to the door is an unsurveyed step.
- **Completion**: whether `DungeonRunCompletion` can prove a run finished. "Provable" means the map's encounter mask is the whole run. "Unknowable" means the map is split into wings the DBC does not tell apart, so a run ends by the watchdog, a job change or the map emptying, and is never recorded `complete`.

## The table

| Dungeon | Rows | Door level | Key | Approach | Survey reach | Completion | Alliance | Horde | Live record |
|---|---|---|---|---|---|---|---|---|---|
| Ragefire Chasm (map 389) | `ragefire` | 8 | none | corridor of 11 points from the Orgrimmar inn down the Cleft of Shadow, home Innkeeper Gryshka (#668) | Vol'jin (488) and the barracks (1713) | provable, 4 encounters | not sent: the door is inside Orgrimmar | yes | 30 attempts, 0 complete: 21 `staging_failed`, most with a member above the door |
| Wailing Caverns (map 43) | `wailing` | 10 | none | corridor of 17 points from the Ratchet inn, home Innkeeper Wiley (neutral) (#242, #342, #348) | none: only the corridor reaches the door | provable, 8 encounters | yes, through Ratchet, clear of the Crossroads guards | yes, the same corridor; Ratchet is neutral | 1 `complete` (Alliance, 2026-09-09); 29 `staging_failed`, the last on 2026-09-09 |
| Blackfathom Deeps (map 48) | `blackfathom` | 19 | none | none; door at z -23 in the sunken temple on the Zoram Strand | none | provable, 7 encounters | untried; nearest Alliance inns are 1,695 yards and more away | untried; Zoram'gar Outpost is Horde | never run |
| Razorfen Kraul (map 47) | `razorfen-kraul` | 17 | none | none | The Barrens Razorfen Kraul (3710), 79 yards | provable, 6 encounters | untried; the short way up is the Great Lift, a transport the walker cannot ride, and the long way passes Camp Taurajo | untried; walk south from Camp Taurajo, clear of the Bael'dun digsite | never run |
| Razorfen Downs (map 129) | `razorfen-downs` | 25 | none | none | The Barrens Razorfen Downs (2775), 404 yards | provable, 4 encounters | untried; same two ways up as Razorfen Kraul | untried; from Camp Taurajo | never run |
| Zul'Farrak (map 209) | `zulfarrak` | 35 | none | none; open desert, trigger radius 20 | Tanaris Zul'Farrak (3653), 83 yards | provable, 8 encounters; mod-dungeon-clear rings the gong for Gahz'rilla without the Mallet | yes; Gadgetzan (Innkeeper Fizzgrimble) is neutral | under the door level (family 26 to 29) | 17 attempts, 0 complete: 9 `staging_failed` on the walk from Un'Goro and Tanaris (#652), 3 `left`, 2 `evacuated`, 2 `emptied`, 1 `split_failed`; the party has been inside |
| Maraudon (map 349) | `maraudon-orange`, `maraudon-purple` | 30 | none for either door | none | purple (1722) from the Valley of Spears (2493), 198 yards; orange (1723) only through the purple node, 984 yards | provable, 8 encounters: the interior connects, so Pristine Waters and Princess Theradras are cleared from either door | untried; Nijel's Point | under the door level | never run |
| Dire Maul East (map 429) | `dire-maul-east-east`, `dire-maul-east-west`, `dire-maul-east-south` | 45 | none | none | each door node is linked; `east-east` from Lariss Pavilion (3311), 184 yards | unknowable: map 429 is three wings (#670) | untried | under the door level; Camp Mojache is 435 yards from `east-east` | never run |
| Dire Maul West (map 429) | `dire-maul-west-north`, `dire-maul-west-south` | 45 | gameobjects 177189 and 177188, lock 1562: item 18249, the Crescent Key, which drops in the East wing | none | both linked, from Dire Maul The Maul (3639) | unknowable (#670) | refused until a member carries the Crescent Key | under the door level | never run |
| Dire Maul North (map 429) | `dire-maul-north` | 45 | gameobject 177192, lock 1562: the Crescent Key | none | linked, from Dire Maul The Maul (3639) | unknowable (#670) | refused until a member carries the Crescent Key | under the door level | never run |

## What each faction can run today

With the families as they stand on 2026-09-24 (Alliance level 60, Horde levels 26 to 29):

- **Alliance**: Wailing Caverns, Blackfathom Deeps, Razorfen Kraul, Razorfen Downs, Zul'Farrak, both Maraudon doors and all three Dire Maul East doors pass every check the module makes. Dire Maul West and North wait for the Crescent Key, so a Dire Maul East run comes first. Ragefire Chasm is not a door the Alliance is sent to.
- **Horde**: Ragefire Chasm, Wailing Caverns, Blackfathom Deeps, Razorfen Kraul and Razorfen Downs pass every check. Zul'Farrak (35), Maraudon (30) and Dire Maul (45) are under the door level and are refused at IDLE.

"Passes every check" is not "proved live". Only Wailing Caverns has a `complete` run. Blackfathom Deeps, both Razorfen doors, Maraudon and Dire Maul have never been tried, and #579 records the outcome of each first attempt.

## Known gaps

- **No survey road reaches the Blackfathom Deeps or Wailing Caverns door.** Their exit nodes (1668, 1663) are linked only from their own entrance and paired exit nodes, never from the overworld. Wailing Caverns has its corridor for this reason; Blackfathom Deeps has none yet, and its door is at the bottom of a sunken temple.
- **The Great Lift** (gameobject 19844) is a transport the walker cannot ride, so an Alliance party reaches the Razorfen doors only by the long way past Camp Taurajo.
- **A Dire Maul run is never recorded `complete`.** The per-wing encounter mapping is in no table the module reads (#431).
- **Inner Maraudon has no row of its own and needs none.** The Portal to Inner Maraudon (gameobject 178404, which asks for the Scepter of Celebras) is a shortcut. A run from either outer door clears the same connected interior (#580).
