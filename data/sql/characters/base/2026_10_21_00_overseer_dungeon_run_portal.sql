-- Keep the selected dungeon keyword on its run row for campaign summaries.
-- Existing runs have no reliable keyword to backfill from their map alone.
-- Guarded like every other ALTER here, so applying the file twice is a no-op.
SET @run_portal_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_dungeon_run'
      AND COLUMN_NAME = 'portal_keyword'
);
SET @add_run_portal_column = IF(
    @run_portal_column_missing,
    "ALTER TABLE `overseer_dungeon_run`
       ADD COLUMN `portal_keyword` VARCHAR(64) NOT NULL DEFAULT '' AFTER `map_id`",
    "SELECT 1"
);
PREPARE add_run_portal_column_stmt FROM @add_run_portal_column;
EXECUTE add_run_portal_column_stmt;
DEALLOCATE PREPARE add_run_portal_column_stmt;
