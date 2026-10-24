-- overseer_order_inbox and the three tables an order writes (mod-overseer#150).
--
-- WHY. The Discord bridge turns a sentence into one LLM tool call and leaves it
-- here; the module resolves the names against item_template, the map table and
-- the roster, writes the roster job or aim row, records the order in
-- overseer_order, and puts the Discord reply back on the inbox row for the
-- bridge to post. Nothing else writes those rows.
--
-- `bridge` is 'dev' or 'live'. Each module serves only the inbox rows whose
-- bridge matches Overseer.Order.Bridge, and each bridge posts only to its own
-- channel with its own Discord application token, so a dev order never reaches
-- the live world or channel.
--
-- `args` is the tool call's arguments, one key=value per line.
-- `status` starts 'pending' and ends 'accepted', 'asked' (a question, no row)
-- or 'failed'; `reply` is the Discord text for that outcome.
--
-- The three tables below are the rows the order writes. Their readers are the
-- campaign (#144), item goal (#17) and level goal (#21) work; this ticket only
-- writes them.
CREATE TABLE IF NOT EXISTS `overseer_order_inbox` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bridge` VARCHAR(16) NOT NULL,
    `author` VARCHAR(64) NOT NULL COMMENT 'Discord user id of the person who typed the order',
    `text` VARCHAR(2000) NOT NULL COMMENT 'The order as typed',
    `tool` VARCHAR(32) NOT NULL,
    `args` TEXT NOT NULL,
    `status` VARCHAR(16) NOT NULL DEFAULT 'pending',
    `reply` VARCHAR(2000) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_bridge_status` (`bridge`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Orders the Discord bridge left for the module, and the reply it posts (mod-overseer#150)';

CREATE TABLE IF NOT EXISTS `overseer_dungeon_campaign` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `map` SMALLINT UNSIGNED NOT NULL,
    `goal_kind` VARCHAR(16) NOT NULL,
    `goal_item` INT UNSIGNED NOT NULL DEFAULT 0,
    `runs_wanted` SMALLINT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='A campaign an order asked for: run a map until a goal item drops (mod-overseer#150)';

CREATE TABLE IF NOT EXISTS `overseer_aim` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `item` INT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='An item an order asked the family to farm (mod-overseer#150)';

CREATE TABLE IF NOT EXISTS `overseer_level_goal` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `member` VARCHAR(12) NOT NULL,
    `level` TINYINT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_member` (`member`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='A level an order asked a member to reach (mod-overseer#150)';
