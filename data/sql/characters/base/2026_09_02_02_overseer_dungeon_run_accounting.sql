-- A RUN IS ONE OF MANY: per-run accounting on `overseer_dungeon_run`
-- (mod-overseer#144).
--
-- WHAT THE TABLE COULD ANSWER BEFORE THIS, AND WHAT IT COULD NOT. Since
-- 2026_08_30_00 it has answered "is a run open on this map, who opened it, when
-- was somebody last seen inside" - the ownership question the drives arbitrate
-- on. What it could not answer is anything about a run as one of a SERIES: which
-- campaign it belonged to, whether it was the first attempt or the eleventh, how
-- it ended, and who was actually in it. #144 turns the coordinator into a loop,
-- and a loop nobody can count is a loop nobody can stop.
--
-- FOUR COLUMNS, EACH ONE READ BY SOMETHING IN THE SAME PULL REQUEST.
--
--   campaign_id  Groups the runs of one campaign. Allocated by the coordinator
--                as MAX(campaign_id) + 1 when it starts a run with the leader's
--                `dungeon_runs_done` at zero, and carried onto every run of that
--                campaign after it. NOT a foreign key into a campaign table:
--                there is no such table and #144's sketch of one is not built
--                (see 2026_09_02_01's own comment for why a goal-kind column
--                with no reader is worse than no column). It is a grouping key
--                and it claims nothing more.
--
--                A PLAIN COUNTER RATHER THAN THE FIRST RUN'S OWN id, which was
--                the first draft. Self-reference would mean inserting the row,
--                reading its id back, and updating it to point at itself - and
--                this module has already written down what it costs to read back
--                a row it has only just asked the async queue to write (see
--                OpenOrTouchRun's own comment on why its read-back is a separate
--                statement). MAX + 1 needs no read-back at all, and the world
--                thread is the only writer.
--
--   run_number   Which run of the campaign this is: 1, 2, 3. The campaign's
--                acceptance criteria are literally stated in these numbers.
--
--   outcome      How the run ended, in the vocabulary #144 asks for. VARCHAR and
--                not ENUM, for the reason `overseer_death.killer_type` already
--                gives: this project has twice needed a migration purely to
--                widen an enum before a value could be written, and #143 will
--                add `complete` to this vocabulary the moment a run has a goal
--                to complete.
--
--                WHAT THIS PULL REQUEST ACTUALLY WRITES, AND NOTHING ELSE:
--                  'left'          the coordinator walked the party back out
--                                  through the door under EXIT.
--                  'wipe'          the map emptied and `overseer_death` holds a
--                                  death on that map, for at least as many
--                                  distinct roster characters as went in, inside
--                                  this run's own window.
--                  'emptied'       the map holds nobody any more and the
--                                  coordinator did not walk them out and it was
--                                  not a wipe by that test: a relog, a bounce, a
--                                  party that left by some other path.
--                  'reset_failed'  the instance could not be reset, so the run
--                                  never started. Counts as a failed run.
--                Deliberately NOT written: 'complete' and 'stalled'. This module
--                cannot prove either one today. "Every encounter done" is not
--                available for map 36 (the coordinator's own comment carries the
--                measurement: Deadmines' script never calls SetBossState, so
--                GetEncounterCount is zero and "all encounters done" would read
--                TRUE the instant the party walked in), a run's real goal is
--                #143, and detecting a stall is #87. A value written before
--                anything has happened is worse than no value, which is the
--                lesson this whole drive family was rebuilt on.
--
--   members      Who was inside when the run got under way, comma-separated.
--                Names rather than guids, the same convention `leader_name`
--                above already uses and for the same stated reason: every other
--                overseer table keys on the character name, and a name survives
--                a character being recreated during development.
--
-- WHY runs THAT NEVER GOT INSIDE STILL GET A ROW. A reset that will not take
-- means nobody enters, so the arming drive - the only thing that opens a run row
-- - never sees anybody on the map and no row is ever created. #144 asks for the
-- opposite: "a failed reset is logged once with the reason and counts as a
-- failed run". So the coordinator inserts an already-ended row itself for that
-- case, with `outcome = 'reset_failed'`. That is safe against the one-active-
-- run-per-map unique key precisely because the row is born ended: the generated
-- `active_map` column is NULL for an ended row and NULLs do not collide.
--
-- WHY 'reset_failed' IS ALSO THE STOP CONDITION. #144 asks the loop to stop
-- "after 3 consecutive runs that did not complete". This code cannot see
-- completion, so it implements the half it can prove: three consecutive
-- `reset_failed` rows in the same campaign stop the campaign, because a reset
-- that has failed three times in a row is not going to start a fourth run and
-- every further attempt would be the same instance with the same bosses already
-- dead. The general rule needs #143's completion signal and is called out as
-- missing rather than faked.
--
-- THE INDEX. The two questions the loop asks every time a run ends are "how many
-- runs has this campaign had" and "how did the last few of them end", both of
-- which are `WHERE campaign_id = ? ORDER BY id DESC`. That is what
-- idx_campaign_run covers.
--
-- IDEMPOTENT FROM THE FIRST APPLY. Same argument as
-- 2026_08_26_01_overseer_roster_job.sql: the updater hashes the whole FILE,
-- comments included, so a comment-only edit later reapplies these statements
-- against a database that already has the columns. `ADD COLUMN IF NOT EXISTS`
-- is a parse error on this pipeline's MySQL 8.4, so each column is guarded by
-- the portable INFORMATION_SCHEMA + PREPARE/EXECUTE dance instead.

SET @campaign_id_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'campaign_id'
);
SET @add_campaign_id = IF(
    @campaign_id_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD COLUMN `campaign_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#144: groups the runs of one campaign. 0 means a run this coordinator did not drive. Not a foreign key - there is no campaign table yet (#143).'",
    "SELECT 1"
);
PREPARE add_campaign_id_stmt FROM @add_campaign_id;
EXECUTE add_campaign_id_stmt;
DEALLOCATE PREPARE add_campaign_id_stmt;

SET @run_number_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'run_number'
);
SET @add_run_number = IF(
    @run_number_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD COLUMN `run_number` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#144: which run of the campaign this is, 1-based. 0 means unstamped.'",
    "SELECT 1"
);
PREPARE add_run_number_stmt FROM @add_run_number;
EXECUTE add_run_number_stmt;
DEALLOCATE PREPARE add_run_number_stmt;

SET @outcome_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'outcome'
);
SET @add_outcome = IF(
    @outcome_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD COLUMN `outcome` VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'mod-overseer#144: left, wipe, emptied, reset_failed. complete and stalled are #143 and #87 and are deliberately never written yet.'",
    "SELECT 1"
);
PREPARE add_outcome_stmt FROM @add_outcome;
EXECUTE add_outcome_stmt;
DEALLOCATE PREPARE add_outcome_stmt;

SET @members_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'members'
);
SET @add_members = IF(
    @members_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD COLUMN `members` VARCHAR(200) NOT NULL DEFAULT '' COMMENT 'mod-overseer#144: comma-separated names of the roster characters inside when the run got under way.'",
    "SELECT 1"
);
PREPARE add_members_stmt FROM @add_members;
EXECUTE add_members_stmt;
DEALLOCATE PREPARE add_members_stmt;

SET @campaign_index_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND INDEX_NAME = 'idx_campaign_run'
);
SET @add_campaign_index = IF(
    @campaign_index_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD KEY `idx_campaign_run` (`campaign_id`, `run_number`)",
    "SELECT 1"
);
PREPARE add_campaign_index_stmt FROM @add_campaign_index;
EXECUTE add_campaign_index_stmt;
DEALLOCATE PREPARE add_campaign_index_stmt;
