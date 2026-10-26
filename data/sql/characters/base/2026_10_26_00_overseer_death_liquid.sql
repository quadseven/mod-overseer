-- Record the core liquid type sampled at the death position. Unknown is
-- explicit so a missing liquid read cannot be mistaken for water.

SET @death_liquid_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'liquid_type'
);
SET @add_death_liquid_column = IF(
    @death_liquid_column_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `liquid_type` VARCHAR(8) NOT NULL DEFAULT 'unknown' COMMENT 'Core liquid flags at the death position: water, magma, slime, or unknown.'",
    "SELECT 1"
);
PREPARE add_death_liquid_column_stmt FROM @add_death_liquid_column;
EXECUTE add_death_liquid_column_stmt;
DEALLOCATE PREPARE add_death_liquid_column_stmt;
