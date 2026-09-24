-- #648: retain the sampled start of a fatal ordinary fall when the ring
-- buffer can identify one. Nullable columns distinguish no qualifying fall
-- from a fall whose sample was unavailable.
SET @schema_version := '2026_09_24_00_overseer_death_fall_start';

SET @sql := IF(
  (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'overseer_death'
     AND COLUMN_NAME = 'fall_start_map') = 0,
  "ALTER TABLE `overseer_death`
     ADD COLUMN `fall_start_map` SMALLINT UNSIGNED NULL COMMENT 'mod-overseer#648: map where a sampled fatal fall started',
     ADD COLUMN `fall_start_x` FLOAT NULL COMMENT 'mod-overseer#648: x where a sampled fatal fall started',
     ADD COLUMN `fall_start_y` FLOAT NULL COMMENT 'mod-overseer#648: y where a sampled fatal fall started',
     ADD COLUMN `fall_start_z` FLOAT NULL COMMENT 'mod-overseer#648: z where a sampled fatal fall started',
     ADD COLUMN `fall_start_movement_generator` VARCHAR(8) NULL COMMENT 'mod-overseer#648: movement generator active at the sampled fall start'",
  'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
