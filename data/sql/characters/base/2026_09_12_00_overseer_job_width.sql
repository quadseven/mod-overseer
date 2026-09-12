-- Widen `job` from VARCHAR(20) to VARCHAR(32), because the dungeon keywords
-- outgrew the column and MySQL refused them rather than truncating them.
--
-- WHAT BROKE, AND IT BROKE SILENTLY FROM THE MODULE'S SIDE. `job` has been
-- VARCHAR(20) since 2026_08_26_01_overseer_roster_job.sql, when every value it
-- had to hold was a single English word from a fixed list - `quest`, `farm`,
-- `dungeon`, `grind`, `town run`, the longest of which is `guild business` at
-- fourteen characters. The column was sized for that list and the size was
-- never wrong for it.
--
-- Then the job grew a QUALIFIER. `dungeon` alone means "the default dungeon",
-- and a campaign that can be pointed at a named dungeon spells it
-- `dungeon:<keyword>` - the form IsDungeonJob and DungeonKeywordForJob in
-- src/mod_overseer.cpp both read. That form is as long as the keyword, and the
-- keywords for the four wings of one dungeon are long:
--
--     dungeon:scarlet-library      23 characters
--     dungeon:scarlet-armory       22 characters
--     dungeon:scarlet-cathedral    25 characters
--
-- All three are over twenty. Every attempt to set one was rejected outright
-- with `ERROR 1406 Data too long for column 'job'`, and a rejected UPDATE is
-- not a truncated job - it is NO job change at all. The bridge asks for a wing,
-- the row keeps whatever it had, and the coordinator faithfully runs the
-- dungeon it was already running. Nothing in the worldserver can see that
-- happen: the module reads the column it is given, and a column that was never
-- written reads back as valid.
--
-- WHY 32 AND NOT 64, AND WHY A NUMBER AT ALL. The longest value that exists
-- today is 25, and `dungeon:` plus a hyphenated dungeon-and-wing keyword is the
-- shape the vocabulary has settled into. 32 clears the longest current value by
-- seven characters and every other named mode by seventeen, which is room for
-- the next wing of the next dungeon without being a column that has stopped
-- meaning anything. A VARCHAR's declared length costs nothing in storage for
-- values shorter than it - the stored bytes are the string plus a length prefix
-- either way - so the only thing a bigger number buys is a weaker statement
-- about what belongs in the column, and the only thing a smaller one buys is
-- this same migration again.
--
-- AND THE DEATH TABLE CARRIES THE SAME VALUE, so it gets the same width. This
-- is the half that is easy to miss and the half with teeth. `overseer_death`
-- has had its own `job VARCHAR(20)` since 2026_08_26_02_overseer_death.sql, and
-- the death recorder copies the character's live job into it on every death
-- (the `job` column in the INSERT built in src/mod_overseer.cpp's death hook).
-- Leaving that column at twenty means that once a wing job CAN be set, the very
-- next death of a character running one fails its INSERT with the same
-- ERROR 1406 - and unlike a refused job change, that loses a death record
-- outright, in the one table this module's whole death-attribution apparatus is
-- built on. The two columns hold the same vocabulary and must be the same
-- width; widening one without the other trades a visible refusal for a silent
-- data loss.
--
-- THE REPO AND THE REALM DISAGREE UNTIL THIS LANDS, AND THAT IS WHY IT IS A
-- FILE RATHER THAN A NOTE. The dev realm's column was widened by hand to
-- unblock testing the new wings. A hand-applied ALTER is invisible to this
-- directory, to a fresh database built from it, and to anybody reading the repo
-- to find out what the schema is - so the next environment built from these
-- files would reproduce the original failure exactly, and the failure is a
-- refused UPDATE that looks like nothing happening.
--
-- MODIFY IS IDEMPOTENT AND THE GUARD IS NOT ABOUT THAT. Running
-- `MODIFY COLUMN ... VARCHAR(32)` twice is harmless: the second one is a no-op
-- against a column that is already that type. The INFORMATION_SCHEMA guard
-- below is for the other case, and for the reason the job column's own
-- migration writes down at length: AzerothCore's updater hashes the whole FILE,
-- comments included, so a later commit that only edits a comment up here makes
-- the updater decide this file changed and run it again. If it ever runs
-- against a database where the column does not exist - a partial restore, a
-- schema older than the ADD - an unguarded MODIFY is a hard error that
-- crash-loops db-import, where a guarded one is a no-op that leaves the earlier
-- migration to do its job. `MODIFY COLUMN IF EXISTS` is not available here
-- (confirmed against this pipeline's MySQL 8.4.11, same as `ADD COLUMN IF NOT
-- EXISTS`: a flat parse error, ERROR 1064), so the portable
-- INFORMATION_SCHEMA + PREPARE/EXECUTE shape is used instead.
--
-- THE COMMENT IS RESTATED IN FULL, NOT DROPPED. A MySQL `MODIFY COLUMN`
-- replaces the whole column definition rather than amending it, so a MODIFY
-- that omits the COMMENT, the NOT NULL or the DEFAULT deletes them. The roster
-- column's comment is reproduced below with the qualified form added to it,
-- which is also the only place in the schema that says the `dungeon:<keyword>`
-- form exists at all.
SET @roster_job_present = (
    SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'job'
);
SET @widen_roster_job = IF(
    @roster_job_present,
    "ALTER TABLE `overseer_roster` MODIFY COLUMN `job` VARCHAR(32) NOT NULL DEFAULT 'quest' COMMENT 'Job-schedule mode (infra#2834): quest, farm, dungeon, grind, gear hunt, craft, town run, train, rest, bank, reputation, guild business. A dungeon run may also name its dungeon as dungeon:<keyword> - for example dungeon:scarlet-cathedral - which is what widened this column to 32 (#429).'",
    "SELECT 1"
);
PREPARE widen_roster_job_stmt FROM @widen_roster_job;
EXECUTE widen_roster_job_stmt;
DEALLOCATE PREPARE widen_roster_job_stmt;

SET @death_job_present = (
    SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'job'
);
SET @widen_death_job = IF(
    @death_job_present,
    "ALTER TABLE `overseer_death` MODIFY COLUMN `job` VARCHAR(32) NOT NULL DEFAULT 'quest'",
    "SELECT 1"
);
PREPARE widen_death_job_stmt FROM @widen_death_job;
EXECUTE widen_death_job_stmt;
DEALLOCATE PREPARE widen_death_job_stmt;
