-- Run recovery requests: how a dungeon campaign recovers instead of stopping.
--
-- WHY THIS TABLE EXISTS. A campaign used to stop after three attempts in a row
-- that never got the party inside, and the operator restarted it by hand. It
-- never stops now: each failed attempt puts the coordinator into RECOVERING,
-- which waits a backoff and then applies one recovery (restage nearer,
-- regroup, town for bags, wait for a client, walk into one copy, replan, or
-- reset). The module writes one row here per failed attempt (kind
-- 'run_recovery') and one per repeated staging take-back (kind
-- 'staging_stall'), with its facts, the options it offers and its own
-- heuristic's choice. The bridge may answer a row (status 'answered', with the
-- option Jev chose); the module reads the answer when the backoff ends, uses its
-- heuristic when there is none, and marks the row 'applied' with what it did.
--
-- NOTHING HERE IS REQUIRED. Without the table the module decides every
-- recovery with its own heuristic and says so once at start-up.
CREATE TABLE IF NOT EXISTS `overseer_run_recovery` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `family` VARCHAR(24) NOT NULL DEFAULT '',
    `leader_name` VARCHAR(12) NOT NULL DEFAULT '',
    `campaign_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `run_number` INT UNSIGNED NOT NULL DEFAULT 0,
    `kind` VARCHAR(16) NOT NULL,
    `attempt` INT UNSIGNED NOT NULL DEFAULT 0,
    `failure` VARCHAR(500) NOT NULL DEFAULT '',
    `facts` VARCHAR(1000) NOT NULL DEFAULT '',
    `options` VARCHAR(200) NOT NULL DEFAULT '',
    `heuristic` VARCHAR(24) NOT NULL DEFAULT '',
    `heuristic_why` VARCHAR(300) NOT NULL DEFAULT '',
    `status` ENUM('pending','answered','applied') NOT NULL DEFAULT 'pending',
    `answer` VARCHAR(24) NOT NULL DEFAULT '',
    `answered_by` VARCHAR(12) NOT NULL DEFAULT '',
    `confidence` FLOAT NULL DEFAULT NULL,
    `answered_at` TIMESTAMP NULL DEFAULT NULL,
    `applied` VARCHAR(24) NOT NULL DEFAULT '',
    `applied_by` VARCHAR(12) NOT NULL DEFAULT '',
    `applied_at` TIMESTAMP NULL DEFAULT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_leader_kind` (`leader_name`, `kind`, `campaign_id`, `attempt`),
    KEY `idx_status_created` (`status`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Recovery requests for failed dungeon attempts and staging stalls, answered by the bridge';
