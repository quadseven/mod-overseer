-- overseer_command: prune answered share rows beyond the backoff memory (#1251).
--
-- WHY. The bridge's share refusal read (the window query over overseer_command)
-- took 2.6 s warm and 6.5 s cold and hit read timeouts on the dev realm. The table
-- held about 747,000 rows (1.25 GB) from 2026-08-21 and was never pruned. The query
-- reads 131,513 share rows from the last 90 days and sorts them by a key that
-- includes a JSON expression. A covering index cannot avoid fetching the base
-- table rows because the read also fetches `result` (about 144 bytes of JSON per
-- row). Retention is the real cure (#1251).
--
-- WHAT. Delete terminal share rows older than the 90-day backoff memory window,
-- and within the 90-day window delete rows exceeding the youngest 8 per group
-- (target_name, target_arg, command, status, reason) that the backoff reads.
-- Non-share kinds and non-terminal statuses are untouched.
--
-- GUARDED: follows the INFORMATION_SCHEMA + PREPARE idiom of earlier migrations.
-- Running twice is idempotent.

SET @command_table_present = (
    SELECT COUNT(*) > 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_command'
      AND COLUMN_NAME = 'result'
);

-- Step 1: delete terminal share rows older than the 90-day backoff memory.
SET @prune_old_share_commands = IF(
    @command_table_present,
    "DELETE FROM `overseer_command`
     WHERE `kind` = 'share'
       AND `status` IN ('delivered', 'error')
       AND `updated_at` < NOW() - INTERVAL 90 DAY",
    "SELECT 1"
);
PREPARE prune_old_share_stmt FROM @prune_old_share_commands;
EXECUTE prune_old_share_stmt;
DEALLOCATE PREPARE prune_old_share_stmt;

-- Step 2: inside the 90-day window, delete rows exceeding the youngest 8 per group.
SET @prune_excess_share_commands = IF(
    @command_table_present,
    "DELETE c FROM `overseer_command` AS c
     JOIN (
         SELECT id FROM (
             SELECT id,
                    ROW_NUMBER() OVER (
                        PARTITION BY `target_name`, `target_arg`, `command`, `status`,
                                     CASE
                                         WHEN `result` IS NOT NULL AND JSON_VALID(`result`)
                                         THEN COALESCE(JSON_UNQUOTE(JSON_EXTRACT(`result`, '$.reason')), '')
                                         ELSE ''
                                     END
                        ORDER BY `updated_at` DESC, `id` DESC
                    ) AS rn
             FROM `overseer_command`
             WHERE `kind` = 'share'
               AND `status` IN ('delivered', 'error')
               AND `updated_at` >= NOW() - INTERVAL 90 DAY
         ) AS ranked
         WHERE rn > 8
     ) AS excess ON c.id = excess.id",
    "SELECT 1"
);
PREPARE prune_excess_share_stmt FROM @prune_excess_share_commands;
EXECUTE prune_excess_share_stmt;
DEALLOCATE PREPARE prune_excess_share_stmt;
