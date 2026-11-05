-- Which liquid the core reported at the death point (#1158), from the
-- player's own LiquidData::Flags, the data Unit::IsInWater reads. The fall
-- guard's in-water bit is true in any liquid, magma and slime included, so the
-- bit alone cannot say drowning. Values: water (water, ocean or dark water),
-- magma, slime, or none (read, no liquid there). NULL means the liquid was
-- not read: every row written before this column, and any row from a writer
-- that predates it. NULL is never water.
--
-- Re-applyable: dbimport may run this file again (#1354), so the column is
-- added only when it is missing.

SET @death_liquid_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'liquid_type'
);
SET @add_death_liquid_column = IF(
    @death_liquid_column_missing,
    "ALTER TABLE `overseer_death`
       ADD COLUMN `liquid_type` VARCHAR(8) NULL COMMENT 'Core liquid at the death point: water, magma, slime or none. NULL when not read.'",
    "SELECT 1"
);
PREPARE add_death_liquid_column_stmt FROM @add_death_liquid_column;
EXECUTE add_death_liquid_column_stmt;
DEALLOCATE PREPARE add_death_liquid_column_stmt;
