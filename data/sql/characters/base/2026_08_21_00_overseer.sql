-- mod-overseer: command queue (Discord bridge -> bots) and presence
-- snapshots (worldserver -> live map). Applied once by dbimport, which
-- records it in acore_characters.updates.
CREATE TABLE IF NOT EXISTS `overseer_command` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `target_name` VARCHAR(12) NOT NULL,
  `command` VARCHAR(255) NOT NULL,
  `source` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'who asked, e.g. discord user id',
  `status` ENUM('pending','delivered','error') NOT NULL DEFAULT 'pending',
  `detail` VARCHAR(255) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `overseer_snapshot` (
  `guid` INT UNSIGNED NOT NULL,
  `name` VARCHAR(12) NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `race` TINYINT UNSIGNED NOT NULL,
  `class` TINYINT UNSIGNED NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `zone_id` INT UNSIGNED NOT NULL,
  `area_id` INT UNSIGNED NOT NULL,
  `pos_x` FLOAT NOT NULL,
  `pos_y` FLOAT NOT NULL,
  `pos_z` FLOAT NOT NULL,
  `health` INT UNSIGNED NOT NULL,
  `max_health` INT UNSIGNED NOT NULL,
  `in_combat` TINYINT(1) NOT NULL DEFAULT 0,
  `is_bot` TINYINT(1) NOT NULL DEFAULT 0,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `group_leader` INT UNSIGNED NOT NULL DEFAULT 0,
  `target_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`),
  KEY `idx_map_zone` (`map_id`, `zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
