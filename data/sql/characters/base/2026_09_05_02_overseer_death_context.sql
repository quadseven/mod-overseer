-- WHAT WAS MOVING THE CHARACTER, AND TOWARD WHAT: attribution columns on
-- `overseer_death` (mod-overseer#188).
--
-- WHAT THE TABLE COULD ANSWER BEFORE THIS, AND WHAT IT COULD NOT. Since
-- 2026_08_26_02 it has answered where a character died, at what level, against
-- what killer, with how much health, and what this module had last aimed it
-- at. What it has never answered is what actually had hold of it. That is not
-- a nice-to-have: two separate investigations have now stopped at exactly this
-- wall. #231 was filed on a plausible mechanism for the falls and then refuted
-- from the sources, because there was no way to check the mechanism against a
-- single real death. Over one measured day, 55 of 113 roster deaths carried no
-- travel target at all and 21 carried no quest aim, which is the most
-- informative fact anybody has established and also the least actionable:
-- something was moving these characters and this module had not asked it to.
--
-- AND THE DEATHS ARE A KILL PLANE, NOT TERRAIN. 223 under-world deaths since
-- 2026-08-30, every one between z -642.2 and -500.1 across four maps. A
-- maximum that tight is a threshold, not ground. The per-day counts run 1, 56,
-- 77, 75, 8, 0, 6, so the roughly 95 percent fall from the 09-01 peak is real
-- and is evidence that the below-terrain recovery is preventing deaths rather
-- than only hiding them - which also gives #188's requested "recoveries per
-- hour" health metric the denominator it was missing.
--
-- EVERY COLUMN HERE IS SAMPLED, NOT LIVE, AND THAT IS NOT A COMPROMISE.
-- Unit::setDeathState stops combat and clears the motion master before
-- Player::KillPlayer runs, so a death hook asking the live Player "were you in
-- combat" or "what was moving you" is guaranteed "no" and "nothing" every
-- time. This is the same trap `health_at_death` already documents, and the
-- same answer: the last sample before the death is the only place the fact
-- still exists. The sample cadence is five seconds and the measured falls
-- complete in nought to five, so it is the right resolution for this question
-- and the wrong one for a slower one - `last_seen_seconds` is on the row so a
-- reader can see how stale each answer is rather than assume.
--
-- SENTINELS ARE NEGATIVE AND MEAN "NOT SAMPLED", never "zero". `in_combat` of
-- -1 is not "was not fighting", `yards_fallen` of -1 is not "did not fall",
-- and `recovery_seconds` of -1 is not "a recovery zero seconds ago". Folding
-- unknown into a plausible value is precisely how a kill plane went 223 deaths
-- without an explanation, so the two are kept apart at the column.
--
-- TWO ANSWERS, KEPT SEPARATE. `movement_generator` is the CORE'S own fact
-- about the character and `driver` is this module's interpretation of that
-- fact beside its own aims. Both are stored so that a reader who thinks the
-- interpretation is wrong can re-derive it instead of having to trust it, for
-- the same reason `killer_type` and `killer_name` are both kept.
--
-- IDEMPOTENT FROM THE FIRST APPLY. Same argument as
-- 2026_09_02_02_overseer_dungeon_run_accounting.sql: the updater hashes the
-- whole FILE, comments included, so a comment-only edit later reapplies these
-- statements against a database that already has the columns. `ADD COLUMN IF
-- NOT EXISTS` is a parse error on this pipeline's MySQL 8.4, so each column is
-- guarded by the portable INFORMATION_SCHEMA + PREPARE/EXECUTE dance instead.

SET @driver_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'driver'
);
SET @add_driver = IF(
    @driver_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `driver` VARCHAR(12) NOT NULL DEFAULT 'unknown' COMMENT 'mod-overseer#188: what this module thinks was moving the character - unknown, recovery, errand, following, fighting, thrown, idle, unattributed. See OverseerDecisions::NameTheDriver for the precedence. unattributed means something moved it and this module did not ask.'",
    "SELECT 1"
);
PREPARE add_driver_stmt FROM @add_driver;
EXECUTE add_driver_stmt;
DEALLOCATE PREPARE add_driver_stmt;

SET @movement_generator_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'movement_generator'
);
SET @add_movement_generator = IF(
    @movement_generator_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `movement_generator` VARCHAR(8) NOT NULL DEFAULT '' COMMENT 'mod-overseer#188: what the core itself reported, folded - idle, follow, point, chase, flee, effect, other. Empty means never sampled. Kept beside driver so the interpretation can be re-derived rather than trusted.'",
    "SELECT 1"
);
PREPARE add_movement_generator_stmt FROM @add_movement_generator;
EXECUTE add_movement_generator_stmt;
DEALLOCATE PREPARE add_movement_generator_stmt;

SET @in_combat_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'in_combat'
);
SET @add_in_combat = IF(
    @in_combat_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `in_combat` TINYINT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: last SAMPLED combat state. -1 not sampled, 0 no, 1 yes. The live value is always 0 in a death hook: setDeathState calls CombatStop before KillPlayer runs.'",
    "SELECT 1"
);
PREPARE add_in_combat_stmt FROM @add_in_combat;
EXECUTE add_in_combat_stmt;
DEALLOCATE PREPARE add_in_combat_stmt;

SET @last_seen_seconds_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'last_seen_seconds'
);
SET @add_last_seen_seconds = IF(
    @last_seen_seconds_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `last_seen_seconds` INT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: how old the sample the columns below came from is, in seconds. -1 means there was no sample, which is not the same as 0.'",
    "SELECT 1"
);
PREPARE add_last_seen_seconds_stmt FROM @add_last_seen_seconds;
EXECUTE add_last_seen_seconds_stmt;
DEALLOCATE PREPARE add_last_seen_seconds_stmt;

SET @last_pos_x_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'last_pos_x'
);
SET @add_last_pos_x = IF(
    @last_pos_x_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `last_pos_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: where the character was at the last sample before it died. Meaningless when last_seen_seconds is -1.'",
    "SELECT 1"
);
PREPARE add_last_pos_x_stmt FROM @add_last_pos_x;
EXECUTE add_last_pos_x_stmt;
DEALLOCATE PREPARE add_last_pos_x_stmt;

SET @last_pos_y_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'last_pos_y'
);
SET @add_last_pos_y = IF(
    @last_pos_y_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `last_pos_y` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: see last_pos_x.'",
    "SELECT 1"
);
PREPARE add_last_pos_y_stmt FROM @add_last_pos_y;
EXECUTE add_last_pos_y_stmt;
DEALLOCATE PREPARE add_last_pos_y_stmt;

SET @last_pos_z_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'last_pos_z'
);
SET @add_last_pos_z = IF(
    @last_pos_z_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `last_pos_z` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: the height it fell FROM, which is the only place that number still exists once it has landed. See yards_fallen.'",
    "SELECT 1"
);
PREPARE add_last_pos_z_stmt FROM @add_last_pos_z;
EXECUTE add_last_pos_z_stmt;
DEALLOCATE PREPARE add_last_pos_z_stmt;

SET @yards_fallen_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'yards_fallen'
);
SET @add_yards_fallen = IF(
    @yards_fallen_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `yards_fallen` FLOAT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: last_pos_z minus pos_z when that is a descent. -1 means not sampled and 0 means it did not fall, which are different findings and are deliberately not folded together.'",
    "SELECT 1"
);
PREPARE add_yards_fallen_stmt FROM @add_yards_fallen;
EXECUTE add_yards_fallen_stmt;
DEALLOCATE PREPARE add_yards_fallen_stmt;

SET @leader_seen_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'leader_seen'
);
SET @add_leader_seen = IF(
    @leader_seen_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `leader_seen` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: 1 when the leader position below was sampled. 0 means no sample, so the zeros below are not a position on map 0.'",
    "SELECT 1"
);
PREPARE add_leader_seen_stmt FROM @add_leader_seen;
EXECUTE add_leader_seen_stmt;
DEALLOCATE PREPARE add_leader_seen_stmt;

SET @leader_map_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'leader_map'
);
SET @add_leader_map = IF(
    @leader_map_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `leader_map` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: the map of the party leader at its last sample. Compare with `map` to see a party split across continents on the row itself.'",
    "SELECT 1"
);
PREPARE add_leader_map_stmt FROM @add_leader_map;
EXECUTE add_leader_map_stmt;
DEALLOCATE PREPARE add_leader_map_stmt;

SET @leader_pos_x_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'leader_pos_x'
);
SET @add_leader_pos_x = IF(
    @leader_pos_x_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `leader_pos_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: the last sampled position of the party leader. Taken from the sample cache and never from a Player*, because the leader of a split party is owned by another map thread (#125).'",
    "SELECT 1"
);
PREPARE add_leader_pos_x_stmt FROM @add_leader_pos_x;
EXECUTE add_leader_pos_x_stmt;
DEALLOCATE PREPARE add_leader_pos_x_stmt;

SET @leader_pos_y_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'leader_pos_y'
);
SET @add_leader_pos_y = IF(
    @leader_pos_y_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `leader_pos_y` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: see leader_pos_x.'",
    "SELECT 1"
);
PREPARE add_leader_pos_y_stmt FROM @add_leader_pos_y;
EXECUTE add_leader_pos_y_stmt;
DEALLOCATE PREPARE add_leader_pos_y_stmt;

SET @leader_pos_z_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'leader_pos_z'
);
SET @add_leader_pos_z = IF(
    @leader_pos_z_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `leader_pos_z` FLOAT NOT NULL DEFAULT 0 COMMENT 'mod-overseer#188: see leader_pos_x.'",
    "SELECT 1"
);
PREPARE add_leader_pos_z_stmt FROM @add_leader_pos_z;
EXECUTE add_leader_pos_z_stmt;
DEALLOCATE PREPARE add_leader_pos_z_stmt;

SET @recovery_rung_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'recovery_rung'
);
SET @add_recovery_rung = IF(
    @recovery_rung_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `recovery_rung` SMALLINT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: the below-terrain recovery rung this module last issued for this character, after the remedy. -1 means it has never moved this character at all.'",
    "SELECT 1"
);
PREPARE add_recovery_rung_stmt FROM @add_recovery_rung;
EXECUTE add_recovery_rung_stmt;
DEALLOCATE PREPARE add_recovery_rung_stmt;

SET @recovery_prev_rung_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'recovery_prev_rung'
);
SET @add_recovery_prev_rung = IF(
    @recovery_prev_rung_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `recovery_prev_rung` SMALLINT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: the rung before that remedy, so a ladder climbing rather than repeating is visible. -1 means never.'",
    "SELECT 1"
);
PREPARE add_recovery_prev_rung_stmt FROM @add_recovery_prev_rung;
EXECUTE add_recovery_prev_rung_stmt;
DEALLOCATE PREPARE add_recovery_prev_rung_stmt;

SET @recovery_seconds_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'recovery_seconds'
);
SET @add_recovery_seconds = IF(
    @recovery_seconds_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `recovery_seconds` INT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#188: seconds between that remedy and this death. -1 means never, which is not the same as a remedy zero seconds ago. The driver column reads this first: see DEATH_RECOVERY_WINDOW_SECONDS.'",
    "SELECT 1"
);
PREPARE add_recovery_seconds_stmt FROM @add_recovery_seconds;
EXECUTE add_recovery_seconds_stmt;
DEALLOCATE PREPARE add_recovery_seconds_stmt;

-- ONE INDEX, FOR THE ONE QUESTION THIS IS ALL FOR. "How did the roster die in
-- the last hour, grouped by what was moving them" is
-- `WHERE created_at > ? GROUP BY driver`, and "show me every death this module
-- caused" is `WHERE driver = 'recovery' ORDER BY created_at`. Both are covered
-- by one composite. No index on the leader or last-position columns: nothing
-- filters on a coordinate, it reads them off rows it already found.

SET @driver_index_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND INDEX_NAME = 'idx_driver_created'
);
SET @add_driver_index = IF(
    @driver_index_missing,
    "ALTER TABLE `overseer_death` ADD KEY `idx_driver_created` (`driver`, `created_at`)",
    "SELECT 1"
);
PREPARE add_driver_index_stmt FROM @add_driver_index;
EXECUTE add_driver_index_stmt;
DEALLOCATE PREPARE add_driver_index_stmt;
