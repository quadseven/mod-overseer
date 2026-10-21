-- The lockpick errand (mod-overseer#445, job='lockpick'). A DriveLockpick is the
-- C++ verb; this is the ONE column it reads and clears - same shape as
-- `learn_fishing` for the fishing errand and `craft_spell` for crafting.
--
-- WHY A BOOLEAN AND NOT A SKILL ID OR SPELL ID. Lockpicking has exactly one
-- spell - Pick Lock (1804) - and exactly one skill - Lockpicking. There is no
-- choice for the Python planner to make per character, per bracket: either a
-- rogue is working its lockpicking up or it is not. `job='lockpick'` carries
-- the permission; this column carries the on/off.
--
-- ZERO MEANS NO STANDING ERRAND, the same sentinel every other aim column in
-- this table uses - so a character with nothing to lockpick costs one indexed
-- comparison in DriveLockpick's own WHERE clause and nothing else.
--
-- CLEARED BY THE MODULE ITSELF when GetSkillValue(SKILL_LOCKPICKING) reads back
-- at the character's level cap (5 * level), never assumed from the cast's own
-- return - see the read-back discipline in DriveCraft and TrainOnArrival.
SET @lockpick_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'lockpick'
);
SET @add_lockpick_column = IF(
    @lockpick_column_missing,
    "ALTER TABLE `overseer_roster`
       ADD COLUMN `lockpick` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0
           COMMENT 'DriveLockpick should cast Pick Lock at world lockboxes for this character; 0 = no standing lockpick errand, cleared once skill reads at the level cap'",
    "SELECT 1"
);
PREPARE add_lockpick_column_stmt FROM @add_lockpick_column;
EXECUTE add_lockpick_column_stmt;
DEALLOCATE PREPARE add_lockpick_column_stmt;
