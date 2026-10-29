-- kind='disenchant': the verb a later change uses to disenchant an owned item.
--
-- THIS FILE ONLY ADDS THE ENUM VALUE. The executor is a separate change; until
-- it lands nothing writes a row of this kind, so nothing reads one either.
--
-- NO STATUS VALUES ARE ADDED.
--
-- THE ENUM LISTS THE FULL UNION, for the reason 2026_09_11_00_overseer_guild.sql
-- spells out: an ALTER that MODIFYs an ENUM replaces the whole list, so the
-- migration that runs last must name every value that exists by then. The list
-- below is the one 2026_09_24_05_overseer_naturalize.sql left (22 values, the
-- last two being 'quest' and 'naturalize'), in the same order, with
-- 'disenchant' appended because it is the newest. tests/test_disenchant_kind.cpp
-- pins it.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant')
        NOT NULL DEFAULT 'bot';
