-- overseer_command: index the two columns every reader filters on.
--
-- WHY. On the dev realm on 2026-10-08 the table held 748,894 rows (1 GB) and
-- carried only PRIMARY, (status) and (kind, status, updated_at). Every read the
-- bridge makes of "this character's recent rows" or "rows of the last day"
-- filters on `target_name` or `created_at`, neither of which had an index, so
-- each one scanned the whole table. Several passes read it per cycle (the guild
-- jobs pass reads a day of rows for all 142 members), and the passes began to
-- time out ("Lost connection to MySQL server during query (timed out)"): the
-- guild jobs, equip, economy, route, share and town errand passes all failed in
-- one minute, and a death knight stranded in its starting zone got no step for
-- hours.
--
-- WHAT. A range index on `created_at`, so "the last day" is a range scan of
-- about 70,000 rows and not a scan of 749,000, and (`target_name`, `created_at`)
-- for a character's own history. No column, row or reader changes; a plain
-- secondary index on an InnoDB table is built online.
ALTER TABLE `overseer_command`
    ADD INDEX `idx_created_at` (`created_at`),
    ADD INDEX `idx_target_created` (`target_name`, `created_at`);
