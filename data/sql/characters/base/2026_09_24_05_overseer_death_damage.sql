-- Keep the last three observed health drops on each death row (mod-overseer#507).
-- The values are nullable because a character can die before this sampler sees
-- any damage. An empty source is written as NULL when only the health delta was
-- observable.

SET @damage_columns_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME IN (
          'damage_1', 'damage_1_seconds', 'damage_1_source',
          'damage_2', 'damage_2_seconds', 'damage_2_source',
          'damage_3', 'damage_3_seconds', 'damage_3_source'
      )
);
SET @add_damage_columns = IF(
    @damage_columns_missing,
    "ALTER TABLE `overseer_death`
       ADD COLUMN `damage_1` INT UNSIGNED NULL COMMENT 'mod-overseer#507: observed health lost in the first retained sample before death',
       ADD COLUMN `damage_1_seconds` INT UNSIGNED NULL COMMENT 'mod-overseer#507: seconds between the first retained damage sample and death',
       ADD COLUMN `damage_1_source` VARCHAR(100) NULL COMMENT 'mod-overseer#507: source associated with the first retained damage sample, NULL when unknown',
       ADD COLUMN `damage_2` INT UNSIGNED NULL COMMENT 'mod-overseer#507: observed health lost in the second retained sample before death',
       ADD COLUMN `damage_2_seconds` INT UNSIGNED NULL COMMENT 'mod-overseer#507: seconds between the second retained damage sample and death',
       ADD COLUMN `damage_2_source` VARCHAR(100) NULL COMMENT 'mod-overseer#507: source associated with the second retained damage sample, NULL when unknown',
       ADD COLUMN `damage_3` INT UNSIGNED NULL COMMENT 'mod-overseer#507: observed health lost in the newest retained sample before death',
       ADD COLUMN `damage_3_seconds` INT UNSIGNED NULL COMMENT 'mod-overseer#507: seconds between the newest retained damage sample and death',
       ADD COLUMN `damage_3_source` VARCHAR(100) NULL COMMENT 'mod-overseer#507: source associated with the newest retained damage sample, NULL when unknown'",
    "SELECT 1"
);
PREPARE add_damage_columns_stmt FROM @add_damage_columns;
EXECUTE add_damage_columns_stmt;
DEALLOCATE PREPARE add_damage_columns_stmt;
