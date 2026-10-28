-- kind='disenchant': a character with Enchanting disenchants one item it
-- already owns (a soulbound green it cannot equip) and the reagent is read
-- back out of its bags. The executor is DoDisenchant and DriveDisenchant in
-- src/mod_overseer.cpp; the rules are in src/overseer_decisions.h and pinned
-- by tests/test_drive_disenchant.cpp.
--
-- THE ENUM LISTS THE FULL UNION, for the reason 2026_09_11_00_overseer_guild.sql
-- spells out: an ALTER that MODIFYs an ENUM replaces the whole list. The list
-- before this file is the 22 values of 2026_09_24_05_overseer_naturalize.sql;
-- tests/test_disenchant_source.cpp fails if any value an earlier file listed
-- is missing here.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant')
        NOT NULL DEFAULT 'bot';
