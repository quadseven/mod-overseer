/*
 * A roster character arriving at a profession trainer buys THE recipe the
 * roster named, and the errand ends either way.
 *
 * TrainOnArrival bought only the next rank of the trade (Journeyman, Expert,
 * Artisan), which teaches a ceiling and nothing to make, and it dropped the
 * errand outright when no rank was due. A roster that names one recipe to
 * buy was therefore never served: the character walked to the trainer, stood
 * in front of it, and came away with nothing (wow-overseer#1094).
 *
 * So the roster may name a recipe, and the arrival path buys that one recipe
 * through the core's own Trainer::TeachSpell - which takes the money - and
 * reads the world back with bot->HasSpell, because TeachSpell returns void
 * and reports its refusals to a client no bot has.
 *
 * JudgeNamedRecipe is the decision for it: the facts of the one recipe, and
 * the verdict. Each row below is one row of that table. The last part reads
 * src/mod_overseer.cpp and the migration (run from the repo root) to pin that
 * the arrival path asks the decision, buys through the core, reads the world
 * back, and ends the errand on a refusal instead of retrying it every poll.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::JudgeNamedRecipe;
using OverseerDecisions::NamedRecipeFacts;
using OverseerDecisions::NamedRecipeRefusalReason;
using OverseerDecisions::NamedRecipeVerdict;
using OverseerDecisions::RECIPE_PURSE_FLOOR_COPPER;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr uint32_t FIRST_AID = 129;
constexpr uint32_t LINEN_BANDAGE = 3276;

// A recipe the trainer sells, the character can learn, and the purse covers.
NamedRecipeFacts Sellable(uint32_t spellId = LINEN_BANDAGE)
{
    NamedRecipeFacts f;
    f.spellId = spellId;
    f.soldHere = true;
    f.known = false;
    f.teachable = true;
    f.skillLine = FIRST_AID;
    f.reqSkillRank = 60;
    f.skillValue = 60;
    f.reqLevel = 10;
    f.level = 20;
    f.cost = 100;
    f.purse = 1000;
    return f;
}

std::string Read(std::string const& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main()
{
    // Row 1: the case the issue describes. The roster named one recipe, the
    // trainer sells it, the character can learn it, and the purse carries it.
    {
        Check("the named recipe is bought",
              JudgeNamedRecipe(Sellable()) == NamedRecipeVerdict::Buy);
    }
    // Row 2: the character already holds it. The errand is over, and it is
    // over whether or not this trainer happens to sell it.
    {
        NamedRecipeFacts f = Sellable();
        f.known = true;
        Check("a known recipe ends the errand",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::AlreadyKnown);
        NamedRecipeFacts g = Sellable();
        g.known = true;
        g.soldHere = false;
        Check("a known recipe is over even at the wrong trainer",
              JudgeNamedRecipe(g) == NamedRecipeVerdict::AlreadyKnown);
    }
    // Row 3: the trainer does not sell it. That is a different trainer's
    // recipe, and it is said as such rather than as a skill problem.
    {
        NamedRecipeFacts f = Sellable();
        f.soldHere = false;
        Check("a recipe this trainer does not sell is named as such",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::NotSoldHere);
    }
    // Row 4: skill short. The core would not teach it, and the reason is the
    // skill in the trade, not the level.
    {
        NamedRecipeFacts f = Sellable();
        f.teachable = false;
        f.skillValue = 30;
        Check("skill short is named as skill short",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::SkillTooLow);
    }
    // Row 5: level short.
    {
        NamedRecipeFacts f = Sellable();
        f.teachable = false;
        f.reqLevel = 60;
        f.level = 20;
        Check("level short is named as level short",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::LevelTooLow);
    }
    // Row 6: the price. The purse cannot carry it.
    {
        NamedRecipeFacts f = Sellable();
        f.cost = 5000;
        f.purse = 100;
        Check("a price the purse cannot carry is named as such",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::TooDear);
    }
    // Row 7: the core would not teach it and no narrower reason is known.
    {
        NamedRecipeFacts f = Sellable();
        f.teachable = false;
        Check("an unexplained refusal is named as not taught",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::NotTaught);
    }
    // Row 8: the classic cap. A recipe past 300 is not a recipe this ruleset
    // has, whatever the trainer's list says.
    {
        NamedRecipeFacts f = Sellable();
        f.reqSkillRank = 305;
        f.skillValue = 305;
        Check("a recipe past the 300 cap is never bought",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::NotTaught);
    }
    // Row 9: THE PURSE FLOOR DOES NOT REACH THE NAMED RECIPE. The floor is the
    // sweep's rule for a shopping list, so a visit never empties a character
    // of its repair money. One recipe the roster asked for, which the purse
    // covers exactly, is bought - the character is not left unable to repair
    // anything, it is left without that one recipe.
    {
        NamedRecipeFacts f = Sellable();
        f.cost = 100;
        f.purse = 100;
        Check("the sweep's purse floor does not apply to the named recipe",
              JudgeNamedRecipe(f) == NamedRecipeVerdict::Buy);
        Check("the floor is still a real number, and the row above means something",
              RECIPE_PURSE_FLOOR_COPPER == 100);
    }
    // Row 10: every refusal is said by name, and a buy is not a refusal.
    {
        Check("a refusal is said by name",
              std::string(NamedRecipeRefusalReason(NamedRecipeVerdict::SkillTooLow)).size() > 0);
        Check("the wrong trainer is said by name",
              std::string(NamedRecipeRefusalReason(NamedRecipeVerdict::NotSoldHere)).size() > 0);
        Check("the price is said by name",
              std::string(NamedRecipeRefusalReason(NamedRecipeVerdict::TooDear)).size() > 0);
        Check("a buy is not a refusal",
              std::string(NamedRecipeRefusalReason(NamedRecipeVerdict::Buy)).size() == 0);
    }

    // The adapter asks the decision, buys through the core, reads the world
    // back, and ends the errand on a refusal.
    {
        std::string const src = Read("src/mod_overseer.cpp");
        Check("adapter source readable", !src.empty());
        Check("the arrival path asks the named-recipe decision",
              src.find("OverseerDecisions::JudgeNamedRecipe(") != std::string::npos);
        Check("the purchase goes through the core's trainer",
              src.find("trainer->TeachSpell(npc, bot, spellId);") != std::string::npos);
        Check("the purchase is read back from the character",
              src.find("bot->HasSpell(spellId)") != std::string::npos);
        Check("a refusal ends the recipe errand",
              src.find("ClearLearnRecipe(name)") != std::string::npos);
        Check("the roster column is cleared on a refusal",
              src.find("UPDATE overseer_roster SET learn_recipe = 0 WHERE name") != std::string::npos);
        Check("the named recipe is read from the roster",
              src.find("learn_recipe") != std::string::npos);
        Check("an errand with only a recipe is not dropped",
              src.find("if (!skill && !recipe)") != std::string::npos);
    }

    // The column the roster names the recipe in.
    {
        std::string const sql =
            Read("data/sql/characters/base/2026_10_22_00_overseer_roster_learn_recipe.sql");
        Check("the migration is there", !sql.empty());
        Check("the migration adds learn_recipe", sql.find("learn_recipe") != std::string::npos);
        Check("the migration adds it to the roster",
              sql.find("overseer_roster") != std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
