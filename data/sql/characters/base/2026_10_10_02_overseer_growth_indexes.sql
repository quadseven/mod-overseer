-- Two keys for the two reads that grow with the table rather than with the
-- window they ask for.
--
-- overseer_death (`created_at`). The module's own retention sweep runs every
-- five minutes as
--
--     DELETE FROM overseer_death WHERE created_at < NOW() - INTERVAL 90 DAY
--
-- and the table has no key that starts with `created_at`, so every sweep reads
-- every row: 55,650 rows examined per sweep on the dev realm on 2026-10-10. The
-- guilds write about 8,000 death rows a day since 2026-10-05, so the table is
-- on its way to about 720,000 rows at the 90-day retention, and a full-scan
-- DELETE locks every row it reads while it runs. The bridge's death readers that
-- ask for "the last N hours" without names take the same full scan.
--
-- overseer_command (`source`, `created_at`). idx_source_id (2026_10_10_00) made
-- "this source, newest first" an index read. A reader that asks for "this
-- source in the last day" still reads every row the source has ever written,
-- or every row of the day across all sources when the optimizer prefers
-- idx_created_at: EXPLAIN estimates about 30,000 rows a call today, and the
-- realm wrote 71,000 rows in one day on 2026-10-08. With the window in the key, a
-- reader reads only its own source's rows inside its own window.
--
-- No column, row or reader changes. A plain secondary key on an InnoDB table
-- is built online.
--
-- GUARDED, like 2026_10_10_00: AzerothCore's updater re-applies a file whose
-- hash changes, and an unconditional ADD KEY run twice is ERROR 1061.
SET @death_created_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND INDEX_NAME = 'idx_created_at'
);
SET @add_death_created = IF(
    @death_created_missing,
    "ALTER TABLE `overseer_death` ADD KEY `idx_created_at` (`created_at`)",
    "SELECT 1"
);
PREPARE add_death_created_stmt FROM @add_death_created;
EXECUTE add_death_created_stmt;
DEALLOCATE PREPARE add_death_created_stmt;

SET @source_created_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_command'
      AND INDEX_NAME = 'idx_source_created'
);
SET @add_source_created = IF(
    @source_created_missing,
    "ALTER TABLE `overseer_command` ADD KEY `idx_source_created` (`source`, `created_at`)",
    "SELECT 1"
);
PREPARE add_source_created_stmt FROM @add_source_created;
EXECUTE add_source_created_stmt;
DEALLOCATE PREPARE add_source_created_stmt;
