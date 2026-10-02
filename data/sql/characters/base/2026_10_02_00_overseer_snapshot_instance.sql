-- A map id identifies a dungeon template, not the live copy of that dungeon.
-- Trades need both characters in the same instance, so the world snapshot
-- carries the instance id used by the core's own in-map range check.
SET @instance_id_missing = (
    SELECT COUNT(*) = 0
      FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE()
       AND TABLE_NAME = 'overseer_snapshot'
       AND COLUMN_NAME = 'instance_id'
);
SET @add_instance_id = IF(
    @instance_id_missing,
    'ALTER TABLE `overseer_snapshot` ADD COLUMN `instance_id` INT UNSIGNED DEFAULT NULL AFTER `map_id`',
    'SELECT 1'
);
PREPARE add_instance_id_stmt FROM @add_instance_id;
EXECUTE add_instance_id_stmt;
DEALLOCATE PREPARE add_instance_id_stmt;
