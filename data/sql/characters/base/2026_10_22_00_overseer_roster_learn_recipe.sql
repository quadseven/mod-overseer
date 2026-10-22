-- The recipe a roster row names for a character to buy at a profession
-- trainer (mod-overseer#1094).
--
-- WHY A COLUMN AT ALL. A profession trainer sells two kinds of thing: the
-- RANK of the trade, which `learn_skill` already names and which TrainOnArrival
-- already buys, and the RECIPES of that trade, which teach what to make and
-- which nothing named before. The roster could only ask for a rank, so a
-- character that needed one recipe had no way to say so, walked to the
-- trainer, and came away with nothing.
--
-- WHY A SPELL ID AND NOT A SKILL ID. The recipe is a spell, and the core's
-- Trainer::GetSpell(spellId) is what says whether the trainer at hand sells
-- it and what it costs. A skill id would have to be resolved into a spell
-- here, and the trainer's own list is the only place that resolution exists.
--
-- WHY INT UNSIGNED AND NOT SMALLINT. Spell ids in this client go well past
-- 65535, and a column that silently truncates a spell id turns "buy this
-- recipe" into "buy some other recipe, or none". The column is wider than
-- the ids it holds on purpose.
--
-- ZERO MEANS NO STANDING ERRAND, the same sentinel every other aim column in
-- this table uses - so a character with no named recipe costs one indexed
-- comparison and nothing else.
--
-- CLEARED BY THE MODULE ITSELF once the recipe is bought, once the character
-- is found to hold it already, or once the trainer will not sell it - the
-- refusal is said by name in the worldserver log and the errand ends there,
-- rather than being retried every poll forever. The read-back is
-- bot->HasSpell, never the void return of Trainer::TeachSpell, which
-- reports its refusals to a client no bot has.
SET @learn_recipe_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'learn_recipe'
);
SET @add_learn_recipe_column = IF(
    @learn_recipe_column_missing,
    "ALTER TABLE `overseer_roster`
       ADD COLUMN `learn_recipe` INT UNSIGNED NOT NULL DEFAULT 0
           COMMENT 'Recipe spell id to buy at the trainer this character is walked to; 0 = nothing to buy, cleared once the recipe is held or the trainer refuses it'
           AFTER `learn_skill`",
    "SELECT 1"
);
PREPARE add_learn_recipe_column_stmt FROM @add_learn_recipe_column;
EXECUTE add_learn_recipe_column_stmt;
DEALLOCATE PREPARE add_learn_recipe_column_stmt;
