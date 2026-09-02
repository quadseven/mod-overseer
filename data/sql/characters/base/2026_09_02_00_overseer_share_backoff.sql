-- Index for the share backoff read (infra#2892, infra PR #3152).
--
-- questshare backs off a share the worldserver keeps refusing for a
-- permanent reason. To decide, the bridge reads every answered share row
-- inside SHARE_REFUSAL_MEMORY_DAYS on each pass:
--
--     WHERE kind = 'share' AND status IN ('delivered', 'error')
--       AND updated_at > NOW() - INTERVAL ? DAY
--
-- overseer_command is never pruned and its only index was idx_status, so
-- that read walked every delivered and error row the table has ever held -
-- roster, chat, gm, give and share alike - on a five-minute loop, and the
-- cost grew with history. Codex flagged it on review. This key matches the
-- access path: equality on kind and status, then the range on updated_at.
--
-- updated_at, not created_at: the backoff clock starts when the worldserver
-- ANSWERS, and a row can sit pending or claimed before it does. The row's
-- terminal write is what stamps updated_at (ON UPDATE CURRENT_TIMESTAMP).
--
-- GUARDED, like 2026_08_26_01 and for the same reason: AzerothCore's updater
-- hashes this whole file, comments included, and re-applies it when the
-- hash changes. An unconditional ADD KEY run twice is ERROR 1061 (duplicate
-- key name) and a db-import crash loop. `ADD INDEX IF NOT EXISTS` is a
-- parse error on this pipeline's MySQL 8.4 (confirmed live for ADD COLUMN,
-- same grammar), so it is the INFORMATION_SCHEMA + PREPARE idiom again.
SET @share_backoff_index_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_command'
      AND INDEX_NAME = 'idx_kind_status_updated'
);
SET @add_share_backoff_index = IF(
    @share_backoff_index_missing,
    "ALTER TABLE `overseer_command` ADD KEY `idx_kind_status_updated` (`kind`, `status`, `updated_at`)",
    "SELECT 1"
);
PREPARE add_share_backoff_index_stmt FROM @add_share_backoff_index;
EXECUTE add_share_backoff_index_stmt;
DEALLOCATE PREPARE add_share_backoff_index_stmt;
