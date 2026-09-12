-- The craft errand (infra#440, job='craft'). A DriveCraft is the C++ verb;
-- this is the ONE column it reads and clears - same shape as `learn_skill`
-- for the trainer errand.
--
-- WHY A SPELL ID AND NOT A SKILL ID. `learn_skill` names a SKILL because
-- Trainer::CanTeachSpell resolves which spell that implies. Crafting has no
-- such resolver here - the Python planner (infra#2757's sibling) is the one
-- that decides WHICH recipe, per character, per skill bracket, and hands
-- this module the exact spell to cast. mod-overseer never picks a recipe;
-- it only ever executes the one it was told, through the core's own
-- CastSpell, the identical path a real player's "Create" click runs. See
-- docs/design/profession-crafting-drive.md for the whole argument.
--
-- ZERO MEANS NO STANDING ERRAND, the same sentinel every other aim column in
-- this table uses - so a character with nothing to craft costs one indexed
-- comparison in DriveCraft's own WHERE clause and nothing else.
ALTER TABLE `overseer_roster`
    ADD COLUMN `craft_spell` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Recipe spell id DriveCraft should cast repeatedly; 0 = no standing craft errand';
