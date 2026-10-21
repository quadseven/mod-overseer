-- Keep the selected dungeon keyword on its run row for campaign summaries.
-- Existing runs have no reliable keyword to backfill from their map alone.
ALTER TABLE `overseer_dungeon_run`
    ADD COLUMN `portal_keyword` VARCHAR(64) NOT NULL DEFAULT '' AFTER `map_id`;
