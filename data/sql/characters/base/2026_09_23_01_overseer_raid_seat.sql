-- Who sits where in an ordered raid: one row per seat, written by the bridge.
--
-- WHY THIS TABLE EXISTS. The raid run (DriveRaidRun) forms a forty-player raid
-- when the operator orders one. Who fills the forty places, and which of groups
-- 1 to 8 each stands in, is the lineup the site already shows (wow-overseer's
-- raidlineup.py). The bridge writes that lineup here when the order is given,
-- and the module reads it, so the raid that forms is the raid the operator saw.
--
-- `subgroup` is ZERO-BASED, 0 to 7, the way `group_member`.`subgroup` and every
-- core call hold it; the site prints it one-based. `keyword` is the raid the
-- seats are for (`moltencore`), so a lineup written for one raid is never read
-- for another. `family` is the roster's own `family` value, the head's name.
CREATE TABLE IF NOT EXISTS `overseer_raid_seat` (
    `family` VARCHAR(32) NOT NULL DEFAULT '',
    `keyword` VARCHAR(20) NOT NULL,
    `name` VARCHAR(12) NOT NULL,
    `subgroup` TINYINT UNSIGNED NOT NULL,
    `role` VARCHAR(8) NOT NULL DEFAULT '',
    `written_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`family`, `keyword`, `name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='The seats of an ordered raid, written by the bridge from its lineup';
