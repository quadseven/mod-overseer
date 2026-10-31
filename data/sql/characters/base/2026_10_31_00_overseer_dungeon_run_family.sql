-- An instance map is shared by independent family groups. Its map id cannot
-- identify their run. Keep one active row per family and map instead.
SET @run_family_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'family'
);
SET @add_run_family = IF(@run_family_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD COLUMN `family` VARCHAR(32)
       CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' AFTER `leader_name`",
    "SELECT 1"
);
PREPARE add_run_family_stmt FROM @add_run_family;
EXECUTE add_run_family_stmt;
DEALLOCATE PREPARE add_run_family_stmt;

-- A shared legacy row may have been renamed by another coordinator. Its
-- current leader is not ownership evidence. Adopt only an active row whose
-- complete recorded member list names exactly one known nonempty family.
-- Historical outcomes and all counters remain unchanged. Unresolved rows
-- retain the blank legacy scope and expire through the ordinary cold sweep.
UPDATE overseer_dungeon_run AS target
JOIN (
    SELECT r.id, MIN(o.family) AS family
    FROM overseer_dungeon_run AS r
    JOIN overseer_roster AS o ON FIND_IN_SET(
        o.name COLLATE utf8mb4_unicode_ci,
        r.members COLLATE utf8mb4_unicode_ci) > 0
    WHERE r.state = 'active' AND r.family = '' AND r.members <> ''
    GROUP BY r.id, r.members
    HAVING COUNT(DISTINCT o.family) = 1 AND MIN(o.family) <> ''
       AND COUNT(*) = 1 + LENGTH(r.members) - LENGTH(REPLACE(r.members, ',', ''))
) AS proven ON proven.id = target.id
SET target.family = proven.family;

-- Add the replacement before dropping the old constraint, so neither the
-- initial apply nor a replay leaves an unconstrained active row writer.
SET @family_run_index_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'overseer_dungeon_run'
      AND INDEX_NAME = 'uniq_active_family_map'
);
SET @add_family_run_index = IF(@family_run_index_missing,
    "ALTER TABLE `overseer_dungeon_run` ADD UNIQUE KEY `uniq_active_family_map` (`family`, `active_map`)",
    "SELECT 1"
);
PREPARE add_family_run_index_stmt FROM @add_family_run_index;
EXECUTE add_family_run_index_stmt;
DEALLOCATE PREPARE add_family_run_index_stmt;

SET @old_run_index_present = (
    SELECT COUNT(*) > 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'overseer_dungeon_run'
      AND INDEX_NAME = 'uniq_active_map'
);
SET @drop_old_run_index = IF(@old_run_index_present,
    "ALTER TABLE `overseer_dungeon_run` DROP INDEX `uniq_active_map`",
    "SELECT 1"
);
PREPARE drop_old_run_index_stmt FROM @drop_old_run_index;
EXECUTE drop_old_run_index_stmt;
DEALLOCATE PREPARE drop_old_run_index_stmt;
