-- What a dungeon death looked like the moment before it (the damage hook's
-- hits, OverseerDecisions::DeathHit). damage_1..3 already exist; these four
-- carry the rest: the member's mana before the killing blow and its pool, how
-- many hostile units were in combat with it, and who had it as their target.
-- NULL means the damage hook saw no hit before the death, which is not the
-- same as 0. max_mana_at_death 0 means the member has no mana bar.

SET @death_hit_columns_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME IN ('mana_at_death', 'max_mana_at_death', 'enemies_engaged', 'targeted_by')
);
SET @add_death_hit_columns = IF(
    @death_hit_columns_missing,
    "ALTER TABLE `overseer_death`
       ADD COLUMN `mana_at_death` INT UNSIGNED NULL COMMENT 'Mana just before the killing blow, from the damage hook. NULL when no hit was seen.',
       ADD COLUMN `max_mana_at_death` INT UNSIGNED NULL COMMENT 'The mana pool at that hit. 0 means no mana bar; NULL when no hit was seen.',
       ADD COLUMN `enemies_engaged` SMALLINT UNSIGNED NULL COMMENT 'Hostile units in combat with the member at the last hit. NULL when no hit was seen.',
       ADD COLUMN `targeted_by` VARCHAR(255) NULL COMMENT 'Who had the member as its target at the killing blow, e.g. Goblin Woodcarver x2, Defias Miner. NULL when no hit was seen.'",
    "SELECT 1"
);
PREPARE add_death_hit_columns_stmt FROM @add_death_hit_columns;
EXECUTE add_death_hit_columns_stmt;
DEALLOCATE PREPARE add_death_hit_columns_stmt;
