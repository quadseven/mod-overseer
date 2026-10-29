-- The quarantine a failed dungeon approach leaves behind (mod-overseer#396).
--
-- WHY THIS TABLE EXISTS. When FailApproach closes a run because the leader is
-- above the corridor and not descending, the next IDLE poll must not re-open a
-- fresh attempt from the same rim. The coordinator holds that position in
-- memory, but a worldserver restart between the failure and the next poll
-- forgot it and the loop resumed. One row per family carries it across.
--
-- WRITTEN AND CLEARED BY THE MODULE ONLY. FailApproach upserts the row; the
-- IDLE gate reads it before it decides and deletes it the poll the pure
-- decision (OverseerDecisions::ApproachQuarantineDecision) clears it.
--
-- NOTHING HERE IS REQUIRED. Without the table the quarantine still holds in
-- memory for the life of the process, and the module says so once at start-up.
CREATE TABLE IF NOT EXISTS `overseer_approach_quarantine` (
    `family`      VARCHAR(32)  NOT NULL,
    `leader_name` VARCHAR(12)  NOT NULL DEFAULT '',
    `campaign_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `x`           FLOAT        NOT NULL,
    `y`           FLOAT        NOT NULL,
    `z`           FLOAT        NOT NULL,
    `since`       BIGINT       NOT NULL DEFAULT 0,
    PRIMARY KEY (`family`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Rim position a failed dungeon approach refused; the next run waits until the leader leaves it';
