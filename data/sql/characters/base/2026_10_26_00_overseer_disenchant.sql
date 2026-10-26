-- Add kind='disenchant' to overseer_command.kind (mod-overseer#444).
--
-- THE WHOLE LIST GOES IN. `ALTER ... MODIFY` replaces the entire ENUM rather
-- than appending, so every value that exists by the time this migration runs
-- must be named here. The base CREATE TABLE is `IF NOT EXISTS` so it can never
-- add one, and a migration that lands later and forgets a value deletes that
-- verb from the schema silently, after which every row of that kind fails to
-- insert.
--
-- The current list (20 values, from 2026_09_11_00_overseer_guild.sql):
--   'bot','chat','gm','probe','give','share','trade','job','sell','bank',
--   'auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild'
-- This migration adds 'disenchant' (21 values) and keeps every one of the 20.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','disenchant')
        NOT NULL DEFAULT 'bot';
