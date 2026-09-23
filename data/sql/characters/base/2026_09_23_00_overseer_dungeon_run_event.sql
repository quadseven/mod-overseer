-- The dungeon run timeline: how a run got to where it ended.
--
-- WHY THIS TABLE EXISTS. overseer_dungeon_run keeps one row per run and says
-- how it ended. Everything before that - which phase it reached, how long each
-- took, which boss was credited, when `dc on` was accepted or refused, what the
-- watchdogs decided - was only ever in the worldserver's log, and the module
-- logs enough that the container log rotates within minutes. A run that failed
-- an hour ago could not be explained without an external log search.
--
-- WHAT IS WRITTEN. One row per phase change and per decision the coordinator
-- takes about a run, and one row when a run ends, carrying the same reason the
-- run row gets. Nothing per poll: a poll that changes nothing writes nothing
-- (see OverseerDecisions::RunTimelineEvents). Rows older than 14 days are
-- deleted by the module's own sweep.
--
-- WHY run_id MAY BE 0. The run row is opened when somebody from the roster is
-- first seen on the instance map, so RESETTING, GATHERING and BARRIER happen
-- before there is a run id. Those rows carry the campaign and run number, and
-- a reader splits a family's rows into runs at each 'ended' row.
CREATE TABLE IF NOT EXISTS `overseer_dungeon_run_event` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `family` VARCHAR(24) NOT NULL DEFAULT '',
    `leader_name` VARCHAR(12) NOT NULL DEFAULT '',
    `character_name` VARCHAR(12) NOT NULL DEFAULT '',
    `run_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `campaign_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `run_number` INT UNSIGNED NOT NULL DEFAULT 0,
    `portal` VARCHAR(32) NOT NULL DEFAULT '',
    `phase` VARCHAR(16) NOT NULL DEFAULT '',
    `kind` VARCHAR(24) NOT NULL,
    `detail` VARCHAR(500) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_run_created` (`run_id`, `created_at`),
    KEY `idx_family_created` (`family`, `created_at`),
    KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Phase changes and decisions of dungeon runs, one row each, kept 14 days';
