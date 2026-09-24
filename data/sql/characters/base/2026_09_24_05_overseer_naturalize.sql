-- kind='naturalize': give back what a character was handed rather than earned.
--
-- FOUR MODES, ONE CHARACTER PER ROW, ALL IRREVERSIBLE AND ALL OFF BY DEFAULT
-- (Overseer.Natural.Enabled = 0):
--
--   reset-level-1        a guild bot of a guild named in Overseer.Natural.Guilds
--                        starts over at level 1 at its race's start. Its
--                        character, name, race, class, account, guild
--                        membership and rank are kept.
--   strip-family-grants  a family character loses its GM-issued items, riding,
--                        untrained weapon skills and the class spells it was
--                        taught with no trainer and no gold. Its level, earned
--                        gear, professions, gold, quests and talents stay.
--   lower-to-natural-level  a family character goes down to level:<N>, and
--                        loses what no trainer would have sold it at N; its
--                        talents are reset and spent again in its tree.
--   discard-unearned-gold  a family character loses the guild dues the guild
--                        bots mailed it: sealed letters are emptied, and what
--                        it already took comes out of its purse.
--
-- Each takes `dry-run`, which writes to `result` exactly what the real run
-- would remove and changes nothing. The rules are in src/overseer_decisions.h
-- and pinned by tests/test_naturalize.cpp.
--
-- THE ENUM LISTS THE FULL UNION, for the reason 2026_09_11_00_overseer_guild.sql
-- spells out: an ALTER that MODIFYs an ENUM replaces the whole list.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize')
        NOT NULL DEFAULT 'bot';

-- THE LEDGER. One row per character and part (reset, items, riding, weapons,
-- spells, lower, gold) once a real run has done it, so a second real run is refused. A
-- later strip would otherwise remove spells the family has since bought from a
-- trainer, because a class trainer purchase leaves no record anywhere.
CREATE TABLE IF NOT EXISTS `overseer_naturalized` (
    `guid`       INT UNSIGNED NOT NULL,
    `part`       VARCHAR(16)  NOT NULL,
    `name`       VARCHAR(12)  NOT NULL,
    `done_at`    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`guid`, `part`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
