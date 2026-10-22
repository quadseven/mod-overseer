-- The recipe the roster names for the arriving trade (1094). TrainOnArrival
-- buys it through the core's Trainer::TeachSpell, which takes the money, with
-- a read-back (bot->HasSpell). A recipe the trainer will not teach - skill or
-- level too low, the wrong trainer, or not enough money - is refused out loud
-- and the errand is released, not retried every cycle.
--
-- ZERO MEANS NO STANDING RECIPE ERRAND, the same sentinel every aim column in
-- this table uses - so a character with nothing to buy costs one indexed
-- comparison in LoadProfessionPlans's own WHERE clause and nothing else.
ALTER TABLE `overseer_roster`
    ADD COLUMN `buy_recipe` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Recipe spell id TrainOnArrival should buy at the arriving trade trainer; 0 = nothing to buy';
