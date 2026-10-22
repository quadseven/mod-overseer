-- The buy-recipe errand (mod-overseer#1098). A DriveBuyRecipeManual is the C++
-- verb; this is the ONE column it reads and clears - same shape as `learn_skill`
-- for the trainer errand.
--
-- The column holds "<itemEntry>:<vendorEntry>" - the item template entry of the
-- recipe manual (a vendor-sold item that teaches a spell when used) and the
-- creature template entry of the vendor that stocks it - or '' when there is no
-- standing errand. The module never picks a vendor or a manual; the bridge
-- writes both, and this module executes and clears the ones it consumes.
--
-- WHY ONE COLUMN FOR BOTH. The walk and the purchase are two halves of one
-- errand, and the vendor is part of the instruction: the character is sent to
-- THAT vendor for THAT manual, not to the nearest vendor that might stock it.
-- Zero/empty means no errand, so a character with nothing to buy costs one
-- indexed comparison in this drive's own WHERE clause and nothing else.
--
-- CLEARED BY THE MODULE ITSELF when the character reads back as knowing the
-- recipe the manual teaches, and cleared on a refusal (the vendor does not
-- stock it, or the purse cannot pay its price) so the errand is released rather
-- than retried every cycle.
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
           COMMENT 'Manual item entry and vendor entry to buy it from, \"<item>:<vendor>\"; empty = no standing buy-recipe errand, cleared once the recipe is known'",
    "SELECT 1"
);
PREPARE add_buy_recipe_item_column_stmt FROM @add_buy_recipe_item_column;
EXECUTE add_buy_recipe_item_column_stmt;
DEALLOCATE PREPARE add_buy_recipe_item_column_stmt;
