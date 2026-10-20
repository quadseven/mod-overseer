-- What a dungeon run leaves for its post-run summary (mod-overseer#971).
--
-- WHAT WAS ALREADY HERE. overseer_dungeon_run has one row per run: who led it,
-- who was in it (`members`), the map, when it started and ended, and how it
-- ended. It is opened on the poll that sees a roster character on an instance
-- map and closed when none remain, the same map transition overseer_snapshot
-- shows. What a summary needs on top of that row was never written.
--
-- WHAT THE MODULE NOW WRITES INTO overseer_event (kind is a VARCHAR, so these
-- are code changes, not schema changes; see 2026_08_24_02_overseer_event.sql):
--
--   loot_won     subject_id = item entry, subject_name = item name
--                detail     = 'loot x1 from <source>', 'need x1', 'greed x1'
--                Every item a roster character wins on a dungeon or raid map,
--                any quality, equipped or not. item_loot stays the story of a
--                notable item; this is the run's loot table. item_guid carries
--                the stored item's guid, so two of one item are two rows.
--   boss_kill    subject_id = creature entry, subject_name = creature name
--                detail     = 'encounter <name>' or 'dungeon boss flag'
--                Written for the killer when the core's encounter list credits
--                the kill or the creature is flagged a dungeon boss. An
--                ordinary creature's death writes nothing.
--   achievement  subject_id = achievement id, subject_name = its name
--
-- WHY A VIEW AND NOT A TABLE. The summary is a query over things already
-- recorded, so it cannot disagree with them. A run's events are those of its
-- leader and members whose hour row overlaps the run's window: overseer_event
-- coalesces repeats per hour, so a row is matched when its last sighting is
-- after the run started and its first is before it ended. A repeat that fell
-- in the same hour as one before the run is attached too; that is the price of
-- the hourly coalescing, and the one-shot kinds (loot, a boss, a level, a quest,
-- an achievement) carry their own subject and are not coalesced across them.
--
-- THE KIND LIST BELOW IS THE LIVE TIMELINE'S LIST. OverseerDecisions::
-- RunHighlightKinds holds the same words and test_dungeon_run_record.cpp reads
-- this file and fails if the two differ, so the post-run screen and the
-- recording timeline highlight the same events.
--
-- ONE COLLATION ON EACH SIDE OF A NAME COMPARISON. overseer_dungeon_run was
-- created with the server's default utf8mb4 collation and overseer_event with
-- utf8mb4_unicode_ci; comparing two columns of different collations is an
-- 'Illegal mix of collations' error, so the run's side is converted to the
-- event table's collation explicitly.
--
-- IDEMPOTENT. CREATE OR REPLACE VIEW and CREATE TABLE IF NOT EXISTS, so a
-- re-applied file (the updater hashes comments too) changes nothing.

CREATE OR REPLACE VIEW `overseer_dungeon_run_summary` AS
SELECT
    r.`id`           AS `run_id`,
    r.`leader_name`  AS `leader_name`,
    r.`map_id`       AS `map_id`,
    r.`state`        AS `state`,
    r.`outcome`      AS `outcome`,
    r.`started_at`   AS `started_at`,
    r.`ended_at`     AS `ended_at`,
    e.`character_name` AS `character_name`,
    e.`kind`         AS `kind`,
    e.`subject_id`   AS `subject_id`,
    e.`subject_name` AS `subject_name`,
    e.`detail`       AS `detail`,
    e.`occurrences`  AS `occurrences`,
    e.`first_seen`   AS `first_seen`,
    e.`last_seen`    AS `last_seen`
FROM `overseer_dungeon_run` r
JOIN `overseer_event` e
  ON (e.`character_name` = CONVERT(r.`leader_name` USING utf8mb4) COLLATE utf8mb4_unicode_ci
      OR FIND_IN_SET(e.`character_name`,
                     CONVERT(r.`members` USING utf8mb4) COLLATE utf8mb4_unicode_ci) > 0)
 AND e.`last_seen` >= r.`started_at`
 AND e.`first_seen` <= COALESCE(r.`ended_at`, CURRENT_TIMESTAMP)
WHERE e.kind IN ('loot_won', 'boss_kill', 'death', 'level_up', 'quest_reward', 'item_equip', 'achievement');

-- PER-RUN DAMAGE, PER CHARACTER, WITHOUT A COMBAT LOG. The family meter's hooks
-- see every hit, heal and hit taken of a roster character; the module keeps a
-- running total per character per dungeon copy and adds it here on the event
-- flush. One row per character per copy (map + instance id), never one per
-- hit. Swept after a fortnight with overseer_event.
CREATE TABLE IF NOT EXISTS `overseer_run_damage` (
    `character_name` VARCHAR(12) NOT NULL,
    `map_id` SMALLINT UNSIGNED NOT NULL,
    `instance_id` INT UNSIGNED NOT NULL,
    `damage` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `healing` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `taken` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `first_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `last_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`character_name`, `map_id`, `instance_id`),
    KEY `idx_last_seen` (`last_seen`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Damage, healing and damage taken per roster character per dungeon copy';

-- A run's damage: its leader's and members' lines on the run's map whose
-- copy was in use inside the run's window.
CREATE OR REPLACE VIEW `overseer_dungeon_run_damage` AS
SELECT
    r.`id`             AS `run_id`,
    d.`character_name` AS `character_name`,
    d.`instance_id`    AS `instance_id`,
    d.`damage`         AS `damage`,
    d.`healing`        AS `healing`,
    d.`taken`          AS `taken`
FROM `overseer_dungeon_run` r
JOIN `overseer_run_damage` d
  ON d.`map_id` = r.`map_id`
 AND (d.`character_name` = CONVERT(r.`leader_name` USING utf8mb4) COLLATE utf8mb4_unicode_ci
      OR FIND_IN_SET(d.`character_name`,
                     CONVERT(r.`members` USING utf8mb4) COLLATE utf8mb4_unicode_ci) > 0)
 AND d.`last_seen` >= r.`started_at`
 AND d.`first_seen` <= COALESCE(r.`ended_at`, CURRENT_TIMESTAMP);
