-- overseer_command: index the readers that filter on `source`.
--
-- WHY. On the dev realm on 2026-10-10 the table held 894,518 rows (1.1 GB).
-- The overseer site's operator console reads its own orders back with
--
--     WHERE source = 'web:overseer' ORDER BY id DESC LIMIT 40
--
-- With no key on `source`, MySQL walks the primary key backwards past every
-- row until it finds 40 of them. Only 43 rows in the table carry that source,
-- the newest from 2026-09-25, so the walk reads nearly the whole table: it was
-- measured at 13.5 s, past the site's 10 s read timeout, and /api/decree
-- answered 503 on every call. The bridge's team-sync readers filter on
-- `source` the same way and scan the table on every pass.
--
-- WHAT. A key on (`source`, `id`): equality on the source, then the newest
-- rows first straight from the index. No column, row or reader changes; a
-- plain secondary index on an InnoDB table is built online.
--
-- GUARDED, like 2026_09_02_00: AzerothCore's updater hashes this file and
-- re-applies it when the hash changes, and an unconditional ADD KEY run twice
-- is ERROR 1061 (duplicate key name).
SET @source_index_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_command'
      AND INDEX_NAME = 'idx_source_id'
);
SET @add_source_index = IF(
    @source_index_missing,
    "ALTER TABLE `overseer_command` ADD KEY `idx_source_id` (`source`, `id`)",
    "SELECT 1"
);
PREPARE add_source_index_stmt FROM @add_source_index;
EXECUTE add_source_index_stmt;
DEALLOCATE PREPARE add_source_index_stmt;
