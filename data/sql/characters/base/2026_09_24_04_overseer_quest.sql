-- kind='quest': take a quest from its giver, or hand it in to its taker.
--
-- WHY. Attunement to the Core (the Molten Core attunement) is given and taken
-- back by Lothos Riftwaker on Blackrock Mountain, and its Alliance row (7848)
-- is not shareable, so kind='share' cannot move it: each member takes it from
-- him in person. The site walks the family to him and writes one row per
-- member, `take quest:<id>` on arrival and `turnin quest:<id>` once the Core
-- Fragment is carried. DoQuest runs the core's own checks and calls
-- (CanTakeQuest, AddQuestAndCheckCompletion; CanRewardQuest, RewardQuest) and
-- reads the log back before it answers 'delivered'.
--
-- NO STATUS VALUES ARE ADDED: 'delivered' and 'error' mean what they mean for
-- kind='share'.
--
-- THE ENUM LISTS THE FULL UNION, for the reason 2026_09_11_00_overseer_guild.sql
-- spells out: an ALTER that MODIFYs an ENUM replaces the whole list, so the
-- migration that runs last must name every value that exists by then. 'quest'
-- is last because it is the newest.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest')
        NOT NULL DEFAULT 'bot';
