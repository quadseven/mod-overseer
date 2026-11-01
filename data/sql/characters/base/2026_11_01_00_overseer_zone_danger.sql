-- The routing rule's current answer for each online roster member.
-- The worldserver replaces these alongside presence snapshots. The site only
-- reads them; it neither queues gameplay commands nor computes another score.
CREATE TABLE IF NOT EXISTS `overseer_zone_danger` (
    `name` VARCHAR(12) NOT NULL,
    `result` MEDIUMTEXT NOT NULL,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
