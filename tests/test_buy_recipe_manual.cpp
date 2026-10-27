/*
 * A roster character's recipe manual (`buy_recipe_item`) is bought from the
 * vendor the row names and used from the bag, or refused with a reason and the
 * errand released. This is the vendor-taught half of a trade that
 * TrainOnArrival never reaches (Heavy Silk Bandage, Smoked Bear Meat).
 *
 * ParseRecipeManualErrand reads the column and PlanRecipeManual is the
 * decision; each check below is one row of its table. The refusal tables say
 * which answers from the buy and learn verbs release the errand. The last part
 * reads src/mod_overseer.cpp (run from the repo root) to pin that
 * DriveRecipeManuals asks the decision, buys and uses through the module's own
 * verbs, and reads the recipe back with HasSpell.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::ManualBuyRefusalReleases;
using OverseerDecisions::ManualPurchaseLine;
using OverseerDecisions::ManualUseRefusalReleases;
using OverseerDecisions::ParseRecipeManualErrand;
using OverseerDecisions::PlanRecipeManual;
using OverseerDecisions::RECIPE_MANUAL_MAX_USES;
using OverseerDecisions::RecipeManualErrand;
using OverseerDecisions::RecipeManualFacts;
using OverseerDecisions::RecipeManualPlan;
using OverseerDecisions::RecipeManualStep;
namespace Refusal = OverseerDecisions::RecipeManualRefusal;

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

// The body of the module's buy verb, DoBuy, from its signature to the closing
// brace at member indent. Empty when the signature is not found.
std::string DoBuyBody()
{
    std::string const source = Source();
    std::size_t const start = source.find("static char const* DoBuy(Player* buyer,");
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = source.find("\n    }\n", start);
    return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// A character standing at a vendor that sells the manual, with the money for
// it and nothing done yet: the row every other case changes one fact of.
RecipeManualFacts AtTheVendor()
{
    RecipeManualFacts f;
    f.teachesRecipe = true;
    f.vendorNearby = true;
    f.vendorInReach = true;
    f.vendorSells = true;
    f.price = 100;
    f.purse = 1000;
    return f;
}

bool Is(RecipeManualPlan const& plan, RecipeManualStep step, char const* refusal)
{
    return plan.step == step && std::strcmp(plan.refusal, refusal) == 0;
}

}  // namespace

int main()
{
    // ---- the column -----------------------------------------------------
    {
        RecipeManualErrand e;
        Check("parses <item>:<vendor>",
              ParseRecipeManualErrand("16112:3373", e) && e.itemEntry == 16112 &&
                  e.vendorEntry == 3373);
    }
    {
        RecipeManualErrand e;
        e.itemEntry = 9;
        e.vendorEntry = 9;
        Check("an empty column is no errand and resets the output",
              !ParseRecipeManualErrand("", e) && e.itemEntry == 0 && e.vendorEntry == 0);
        Check("a bare colon is malformed", !ParseRecipeManualErrand(":", e));
        Check("no vendor side is malformed", !ParseRecipeManualErrand("16112:", e));
        Check("no item side is malformed", !ParseRecipeManualErrand(":3373", e));
        Check("a zero item is malformed", !ParseRecipeManualErrand("0:3373", e));
        Check("a zero vendor is malformed", !ParseRecipeManualErrand("16112:0", e));
        Check("text is malformed", !ParseRecipeManualErrand("abc:3373", e));
        Check("a sign is malformed", !ParseRecipeManualErrand("-1:3373", e));
        Check("a third field is malformed", !ParseRecipeManualErrand("16112:3373:1", e));
        Check("an entry past uint32 is malformed",
              !ParseRecipeManualErrand("4294967296:3373", e));
        Check("an entry far past uint32 is malformed, not thrown",
              !ParseRecipeManualErrand("99999999999999999999999:3373", e));
    }

    // ---- the plan, one row per fact -------------------------------------
    Check("a manual it can afford, at a vendor that sells it, is bought",
          Is(PlanRecipeManual(AtTheVendor()), RecipeManualStep::Buy, ""));
    {
        RecipeManualFacts f = AtTheVendor();
        f.purse = 100;
        Check("a purse exactly the price buys",
              Is(PlanRecipeManual(f), RecipeManualStep::Buy, ""));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.purse = 99;
        Check("a purse short of the price is refused, naming why",
              Is(PlanRecipeManual(f), RecipeManualStep::Refuse, Refusal::TooPoor));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.vendorSells = false;
        Check("a manual the vendor does not sell is refused, naming why",
              Is(PlanRecipeManual(f), RecipeManualStep::Refuse, Refusal::NotSold));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.vendorNearby = false;
        f.vendorInReach = false;
        f.vendorSells = false;
        f.purse = 0;
        Check("a vendor out of reach is walked to before stock or price is judged",
              Is(PlanRecipeManual(f), RecipeManualStep::Walk, ""));
        f.vendorNearby = true;
        Check("a vendor inside the arrival radius but outside the interact gate is stepped to",
              Is(PlanRecipeManual(f), RecipeManualStep::Approach, ""));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.teachesRecipe = false;
        Check("an item that teaches no recipe is refused before any walk",
              Is(PlanRecipeManual(f), RecipeManualStep::Refuse, Refusal::NotARecipe));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.knowsRecipe = true;
        f.vendorSells = false;
        Check("a recipe already known ends the errand, whatever else is true",
              Is(PlanRecipeManual(f), RecipeManualStep::Learned, ""));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.learning = true;
        f.carriesManual = true;
        f.usesTried = 1;
        Check("a use still being read back is waited for",
              Is(PlanRecipeManual(f), RecipeManualStep::Wait, ""));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.carriesManual = true;
        f.vendorNearby = false;
        f.vendorInReach = false;
        Check("a manual already carried is used, with no walk and no second buy",
              Is(PlanRecipeManual(f), RecipeManualStep::Use, ""));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.carriesManual = true;
        f.usesTried = RECIPE_MANUAL_MAX_USES - 1;
        Check("a use that never ran is tried again under the bound",
              Is(PlanRecipeManual(f), RecipeManualStep::Use, ""));
        f.usesTried = RECIPE_MANUAL_MAX_USES;
        Check("uses past the bound are refused rather than tried every cycle",
              Is(PlanRecipeManual(f), RecipeManualStep::Refuse, Refusal::UsedAndNotLearned));
    }
    {
        RecipeManualFacts f = AtTheVendor();
        f.usesTried = 1;
        Check("a manual consumed without teaching is refused, not bought again",
              Is(PlanRecipeManual(f), RecipeManualStep::Refuse, Refusal::UsedAndNotLearned));
    }

    // ---- which verb refusals release the errand -------------------------
    // Each detail below is fed to the release table AND looked up as a
    // refuse("...") inside DoBuy itself, so a test detail the real buy verb
    // never says (or a verb whose words drift from the table) fails here
    // rather than passing against a phantom.
    struct BuyRefusalRow
    {
        char const* what;
        char const* detail;
        bool releases;
    };
    BuyRefusalRow const buyRefusals[] = {
        {"a vendor that does not stock the item releases", "vendor does not stock the item", true},
        {"a purse the core finds short releases", "cannot afford the purchase", true},
        {"a price over the cap the drive set releases", "price exceeds the cap the row set", true},
        {"an item for the other faction releases", "item is for the other faction", true},
        {"full bags stand for the next poll", "bags cannot take the item", false},
        {"a vendor out of range stands, so the walk can close it", "vendor not in range", false},
        {"a limited vendor out of stock stands until it restocks", "vendor is out of stock", false},
    };
    std::string const doBuy = DoBuyBody();
    Check("DoBuy is found in the module source", !doBuy.empty());
    for (BuyRefusalRow const& row : buyRefusals)
    {
        Check(row.what, ManualBuyRefusalReleases(row.detail) == row.releases);
        if (doBuy.find(std::string("refuse(\"") + row.detail + "\"") == std::string::npos)
        {
            std::printf("FAIL DoBuy never refuses with '%s'\n", row.detail);
            ++failures;
        }
    }
    Check("a skill too low to use the manual releases",
          ManualUseRefusalReleases(OverseerDecisions::LearnRefusal::SkillTooLow));
    Check("a manual already known releases",
          ManualUseRefusalReleases(OverseerDecisions::LearnRefusal::AlreadyKnown));
    Check("a character in combat stands for the next poll",
          !ManualUseRefusalReleases(OverseerDecisions::LearnRefusal::InCombat));
    Check("a character still moving stands for the next poll",
          !ManualUseRefusalReleases(OverseerDecisions::LearnRefusal::Moving));

    // ---- the proof line -------------------------------------------------
    Check("the proof line names the member, manual, vendor, recipe and price",
          ManualPurchaseLine("Mender", 16112, "Manual: Heavy Silk Bandage", 3373, 7929,
                             "Heavy Silk Bandage", 2000) ==
              "overseer: manual_purchase member='Mender' item=16112 'Manual: Heavy Silk "
              "Bandage' vendor=3373 spell=7929 'Heavy Silk Bandage' paid_copper=2000");

    // ---- the drive in the module source ---------------------------------
    std::string const source = Source();
    Check("the drive reads the buy_recipe_item column",
          source.find("buy_recipe_item <> ''") != std::string::npos);
    Check("the drive parses the column",
          source.find("OverseerDecisions::ParseRecipeManualErrand(") != std::string::npos);
    Check("the drive asks the decision",
          source.find("OverseerDecisions::PlanRecipeManual(") != std::string::npos);
    Check("the drive walks through the travel aim book",
          source.find("_travelAims.Claim(name, std::to_string(errand.vendorEntry)") !=
              std::string::npos);
    Check("the manual is bought through the buy verb at the price judged",
          source.find("DoBuy(bot, buy, status, out)") != std::string::npos);
    Check("the manual is used through the learn verb",
          source.find("DoLearnRecipe(bot, use, status, out, _pendingLearns, 0)") !=
              std::string::npos);
    Check("the recipe is read back with HasSpell",
          source.find("facts.knowsRecipe = bot->HasSpell(recipe);") != std::string::npos);
    Check("a refusal clears the column",
          source.find("SET buy_recipe_item = ''") != std::string::npos);
    Check("the release escapes the roster name in its UPDATE",
          source.find("\"UPDATE overseer_roster SET buy_recipe_item = '' WHERE name = '{}'\", "
                      "Esc(name));") != std::string::npos);
    Check("a release drops the errand's progress with the column",
          source.find("Esc(name));\n        _recipeManuals.erase(name);") != std::string::npos);
    Check("progress for a column cleared from outside is pruned every poll",
          source.find("it = errands.count(it->first) ? std::next(it) : _recipeManuals.erase(it);") !=
              std::string::npos);
    Check("the drive is polled",
          source.find("DriveRecipeManuals();") != std::string::npos);

    if (failures)
        return 1;
    std::printf("test_buy_recipe_manual: all passed\n");
    return 0;
}
