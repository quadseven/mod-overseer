-- The talent tree each guild raider's raid seat needs, written by the site.
--
-- WHY THIS TABLE EXISTS. The site (wow-overseer's raidlineup.py) plans each
-- guild's raid as eight groups of five, each one tank, one healer and three
-- damage dealers, from the forty raiders' classes, and gives each raider the
-- talent tree its seat needs. The natural guilds' bots start again at level 1
-- and spend their own talent points as they level. Without a target the
-- playerbots level-up action picks a tree at random at level 10 and keeps it,
-- so a warrior planned as a tank could come out Fury.
--
-- WHO READS IT. The module, once a minute, into memory. When a guild raider
-- gains a level or logs in with free talent points, they are spent in `tab`
-- (SpendTalentsTowardSeat). A family character is left to TrainRoster and its
-- roster tree. Points already in another tree stay where they are.
--
-- `tab` is TalentTab.tabpage, 0 to 2, the order the talent frame shows the
-- trees. `class` is the class the seat was planned for; a row whose class no
-- longer matches the character is stale and not followed. The site rewrites a
-- guild's rows each time it plans the lineup.
CREATE TABLE IF NOT EXISTS `overseer_raid_spec` (
    `name` VARCHAR(12) NOT NULL,
    `guild` VARCHAR(24) NOT NULL DEFAULT '',
    `class` TINYINT UNSIGNED NOT NULL,
    `tab` TINYINT UNSIGNED NOT NULL,
    `tree` VARCHAR(16) NOT NULL DEFAULT '',
    `duty` VARCHAR(16) NOT NULL DEFAULT '',
    `subgroup` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `written_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`name`),
    KEY `guild` (`guild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='The talent tree each guild raider seat needs, written by the site from its lineup';
