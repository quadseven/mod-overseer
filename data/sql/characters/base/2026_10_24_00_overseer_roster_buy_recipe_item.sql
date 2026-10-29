-- The recipe manual errand (#1098). DriveRecipeManuals is the C++ verb; this
-- is the ONE column it reads and clears - same shape as `craft_spell` for the
-- trainer recipe and `lockpick` for the lockpick errand.
--
-- THE VALUE IS "<item entry>:<vendor entry>": the item_template entry of a
-- manual that teaches a recipe when it is used (Heavy Silk Bandage, Smoked Bear
-- Meat), and the creature_template entry of the vendor to buy it from. The
-- vendor is part of the instruction, so the character is walked to THAT vendor
-- rather than whichever one happens to stand nearest. The module never picks
-- either; the site writes both.
--
-- EMPTY MEANS NO STANDING ERRAND, so a character with nothing to buy costs one
-- comparison in DriveRecipeManuals' own WHERE clause and nothing else.
--
-- CLEARED BY THE MODULE ITSELF when HasSpell reads the recipe back, and on a
-- refusal (the vendor does not sell the manual, the purse cannot pay for it,
-- the item teaches nothing, or a used manual taught nothing) so the errand is
-- released rather than retried every cycle. The reason is in the worldserver
-- log line that clears it.
SET @buy_recipe_item_column_missing = (
    SELECT COUNT(*) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'buy_recipe_item'
);
SET @add_buy_recipe_item_column = IF(
    @buy_recipe_item_column_missing,
    "ALTER TABLE `overseer_roster`
       ADD COLUMN `buy_recipe_item` VARCHAR(32) NOT NULL DEFAULT ''
           COMMENT 'Recipe manual to buy and use, <item entry>:<vendor entry>; empty = no standing errand, cleared once the recipe is known or the buy is refused'",
    "SELECT 1"
);
PREPARE add_buy_recipe_item_column_stmt FROM @add_buy_recipe_item_column;
EXECUTE add_buy_recipe_item_column_stmt;
DEALLOCATE PREPARE add_buy_recipe_item_column_stmt;
