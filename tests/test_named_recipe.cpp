/*
 * A roster character's named trainer recipe (`craft_spell`) is bought on
 * arrival at its trade trainer, or refused with a reason and the errand
 * released. PlanNamedRecipe is the decision; each check below is one row of
 * its table. The last part reads src/mod_overseer.cpp (run from the repo root)
 * to pin that TrainOnArrival asks it and reads the purchase back.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::NamedRecipeStep;
using OverseerDecisions::NamedRecipeStepWord;
using OverseerDecisions::PlanNamedRecipe;

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

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main()
{
    // known, onList, canTeach, cost, purse
    Check("held recipe is not bought again",
          PlanNamedRecipe(true, true, true, 100, 1000) == NamedRecipeStep::AlreadyHeld);
    Check("held wins over every other fact",
          PlanNamedRecipe(true, false, false, 100, 0) == NamedRecipeStep::AlreadyHeld);
    Check("a recipe this trainer does not sell is the wrong trainer",
          PlanNamedRecipe(false, false, true, 100, 1000) == NamedRecipeStep::NotSold);
    Check("a recipe the core will not teach is refused (skill, level, chain)",
          PlanNamedRecipe(false, true, false, 100, 1000) == NamedRecipeStep::CannotTeach);
    Check("a purse short of the price is refused",
          PlanNamedRecipe(false, true, true, 100, 99) == NamedRecipeStep::TooPoor);
    Check("a purse exactly the price buys",
          PlanNamedRecipe(false, true, true, 100, 100) == NamedRecipeStep::Buy);
    Check("a free recipe buys with an empty purse",
          PlanNamedRecipe(false, true, true, 0, 0) == NamedRecipeStep::Buy);

    Check("words are distinct and non-empty",
          std::strcmp(NamedRecipeStepWord(NamedRecipeStep::NotSold), "not_sold") == 0 &&
              std::strcmp(NamedRecipeStepWord(NamedRecipeStep::CannotTeach), "cannot_teach") == 0 &&
              std::strcmp(NamedRecipeStepWord(NamedRecipeStep::TooPoor), "too_poor") == 0);

    std::string const source = Source();
    Check("the roster's craft_spell is read into the plan",
          source.find("it->second.recipeSpell = fields[1].Get<uint16>();") != std::string::npos);
    Check("TrainOnArrival asks the decision", source.find("PlanNamedRecipe(") != std::string::npos);
    Check("the purchase is read back with HasSpell",
          source.find("BuyNamedRecipe(") != std::string::npos &&
              source.find("bot->HasSpell(recipe)") != std::string::npos);

    if (failures)
        return 1;
    std::printf("test_named_recipe: all passed\n");
    return 0;
}
