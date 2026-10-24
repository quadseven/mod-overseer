-- overseer_order: one row per accepted Discord order (mod-overseer#150).
--
-- WHY. An operator types a sentence and the LLM bridge writes the roster job
-- and aim rows it means. Afterwards the question is always the same: who said
-- what, and what did that become. This is that record. An order that asked a
-- question instead of writing (unknown name, two matches, no such member)
-- writes nothing here either, because it changed nothing.
--
-- `bridge` names the bridge that took the order ('dev' or 'live'). Each runs
-- on its own Discord application token and answers only in its own channel, so
-- a dev order cannot land in the live world or channel; the column shows which
-- one wrote the row.
--
-- `rows_written` is a JSON array of {"table": ..., "key": ...} entries, one
-- per roster or aim row the order wrote, so the reply's ids can be checked
-- against the database. Never updated after the insert.
CREATE TABLE IF NOT EXISTS `overseer_order` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bridge` VARCHAR(16) NOT NULL,
    `author` VARCHAR(64) NOT NULL COMMENT 'Discord user id of the person who typed the order',
    `text` VARCHAR(2000) NOT NULL COMMENT 'The order as typed',
    `rows_written` TEXT NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_author_created` (`author`, `created_at`),
    KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='One row per accepted Discord order: who, what was said, what was written (mod-overseer#150)';
