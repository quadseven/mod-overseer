-- HOW MANY TIMES, AND HOW MANY SO FAR: the campaign counter the rerun loop is
-- bounded by (mod-overseer#144).
--
-- THE MEASURED FACT THIS EXISTS FOR (2026-09-02). The family cleared Deadmines
-- once. Sent back in with `job = 'dungeon'`, every part of the machinery worked:
-- the coordinator staged them, the whole party crossed together, and `dc on` was
-- issued and verified automatically for the first time. Then, immediately, the
-- dungeon brain said `advance: no next boss (all dead/skipped) -> stalling`.
--
-- The reason was in the database rather than in any of that code. The `instance`
-- row for map 36 carried `completedEncounters = 127` - all seven bits - and the
-- leader had a `character_instance` row binding him to it. He had walked into a
-- mine where everything was already dead. A dungeon keeps its instance save
-- until the group leader resets it, so without a reset every rerun is a walk
-- through an empty instance, however well the walking works.
--
-- WHY THE CAP IS A ROSTER COLUMN AND NOT A CONFIG FILE OR A NEW TABLE.
--
-- Not a config file: mod_overseer.cpp does not read one. There is not a single
-- sConfigMgr call in the module - every steering decision it takes is read out
-- of a database column the operator can edit live (`job`, `drive_quest`,
-- `travel_npc`, `professions`, `enabled`). Introducing a worldserver.conf key
-- for this one number would mean a rebuild-and-redeploy to change "run it 30
-- times" to "run it 5 times", which is exactly the round trip every other
-- steering column exists to avoid.
--
-- Not a new `overseer_dungeon_campaign` table, which is what #144 sketches. A
-- campaign row in that sketch carries the target count AND the GOAL KIND
-- ("until every held quest is complete", "until item X drops"). The goal kind
-- has no reader and cannot have one yet: deriving a run's goal from the quest
-- log is #143 and it is not built, gear as a goal is #145, and item mandates
-- from plain language are #150. A table whose defining column nothing reads is
-- the inert-mechanism shape this module has already paid for - see the
-- `job` column's own migration, which named its unwired modes rather than
-- pretending they did anything. When #143 gives a campaign a goal, it can carry
-- these two numbers with it; until then the campaign is exactly what these two
-- columns say it is, and nothing is claimed beyond them.
--
-- WHY IT IS THE LEADER'S ROW THAT IS READ. `job = 'dungeon'` is already read off
-- the leader's row alone (the coordinator's own comment gives the reason: the
-- leader is the only character that can be aimed at all, and the bridge writes
-- the whole roster's job together). The run cap is a property of the same
-- campaign that job column starts, so it is read from the same row rather than
-- from a sixth place with its own opinion.
--
-- HOW AN OPERATOR USES THESE TWO COLUMNS.
--
--   -- run Deadmines thirty times
--   UPDATE overseer_roster SET dungeon_runs_wanted = 30, dungeon_runs_done = 0,
--          job = 'dungeon' WHERE `lead` = 1;
--
--   -- how far along is it
--   SELECT name, job, dungeon_runs_done, dungeon_runs_wanted
--     FROM overseer_roster WHERE `lead` = 1;
--
--   -- go again on a campaign that has already hit its cap
--   UPDATE overseer_roster SET dungeon_runs_done = 0 WHERE `lead` = 1;
--
-- `dungeon_runs_done = 0` is what starts a NEW campaign: the coordinator
-- allocates a fresh campaign id the next time it begins a run from zero, so the
-- rows of the old campaign stay grouped under the old id and the
-- three-consecutive-failures stop cannot be inherited across the boundary. See
-- 2026_09_02_02_overseer_dungeon_run_accounting.sql for the run side of that.
--
-- WHY THE DEFAULT IS 30. The operator's own words, quoted in the epic: "run it
-- like 30 times". Not a tuning constant anybody derived - a request, written
-- down where it can be changed by the person who made it.
--
-- WHY BOTH NUMBERS ARE WRITABLE BY THE MODULE AND BY A PERSON. `dungeon_runs_
-- done` is incremented by the coordinator as each run closes, which makes it the
-- one column here the module writes to `overseer_roster` on its own initiative.
-- That is deliberate and it is the reason the counter is durable at all: the
-- coordinator's own state is in-process and dies with the worldserver (see
-- DungeonRunCoordinatorState), and a cap that forgot how many runs had happened
-- every time the process bounced would not be a cap. A number that must survive
-- a restart has to live in a row.
--
-- WHY THE C++ SIDE PROBES BEFORE IT READS, exactly like `job` and `travel_npc`
-- before it. The DDL and the reader ship in DIFFERENT images and can arrive in
-- either order, and a SELECT naming a column the table does not have fails
-- WHOLE - it would take `name` and `lead` down with it. So the module asks
-- INFORMATION_SCHEMA once per process whether these columns exist, and when
-- they do not it does not loop at all: a worldserver newer than its database
-- keeps exactly today's behaviour (one run, then the coordinator returns to
-- IDLE) rather than looping unbounded on a cap it cannot read. Not looping is
-- the safe direction for an unknown; looping forever is not.
--
-- IDEMPOTENT FROM THE FIRST APPLY, and 2026_08_26_01_overseer_roster_job.sql
-- explains at length why that is not optional here: AzerothCore's updater hashes
-- this whole FILE, comments included, so a later commit that only fixes a typo
-- in a comment above changes the hash and the updater reapplies the statement.
-- An unconditional ADD COLUMN crash-loops db-import against any database that
-- already has the column. `ADD COLUMN IF NOT EXISTS` is a flat parse error on
-- this pipeline's MySQL 8.4 (ERROR 1064, confirmed live), so the portable
-- INFORMATION_SCHEMA + PREPARE/EXECUTE guard is used instead.

SET @dungeon_runs_wanted_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'dungeon_runs_wanted'
);
SET @add_dungeon_runs_wanted = IF(
    @dungeon_runs_wanted_missing,
    "ALTER TABLE `overseer_roster` ADD COLUMN `dungeon_runs_wanted` SMALLINT UNSIGNED NOT NULL DEFAULT 30 COMMENT 'mod-overseer#144: how many dungeon runs this campaign wants. Read off the party leader row only. 0 stops the campaign outright - no run starts at all - because the coordinator asks whether done >= wanted before it begins one.'",
    "SELECT 1"
);
PREPARE add_dungeon_runs_wanted_stmt FROM @add_dungeon_runs_wanted;
EXECUTE add_dungeon_runs_wanted_stmt;
DEALLOCATE PREPARE add_dungeon_runs_wanted_stmt;

SET @dungeon_runs_done_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'dungeon_runs_done'
);
SET @add_dungeon_runs_done = IF(
    @dungeon_runs_done_missing,
    "ALTER TABLE `overseer_roster` ADD COLUMN `dungeon_runs_done` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#144: how many runs of the current campaign have closed. Written by the run coordinator as each run ends; set back to 0 by hand to start a new campaign.'",
    "SELECT 1"
);
PREPARE add_dungeon_runs_done_stmt FROM @add_dungeon_runs_done;
EXECUTE add_dungeon_runs_done_stmt;
DEALLOCATE PREPARE add_dungeon_runs_done_stmt;
