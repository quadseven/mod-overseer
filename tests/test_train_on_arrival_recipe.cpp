/*
 * TrainOnArrival also buys the trainer recipe the roster names for the
 * arriving trade: a new column, buy_recipe, or a spell this trainer sells and
 * Trainer::CanTeachSpell allows. The purchase goes through the core's own
 * Trainer::TeachSpell, which takes the money, with a read-back (bot->HasSpell),
 * and arriving with no rank to buy but a recipe to buy completes the errand.
 *
 * The decision whether the named recipe is buyable right now is a pure
 * function, RecipeToBuyNow, checked here. The rest of the errand - TeachSpell,
 * the read-back, the worldserver line naming the recipe, and releasing rather
 * than retrying when the trainer will not teach it - is pinned by reading
 * src/mod_overseer.cpp (run from the repo root).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::RECIPE_PURSE_FLOOR_COPPER;
using OverseerDecisions::RecipeToBuyNow;
using OverseerDecisions::TrainerRecipeOffer;

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

TrainerRecipeOffer Offer(uint32_t id, uint32_t line, uint32_t rank, uint64_t cost,
                         uint32_t level = 1)
{
    TrainerRecipeOffer o;
    o.spellId = id;
    o.skillLine = line;
    o.reqSkillRank = rank;
    o.reqLevel = level;
    o.cost = cost;
    return o;
}

constexpr uint32_t FIRST_AID = 129;
constexpr uint32_t TAILORING = 197;

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
    // RecipeToBuyNow: the pure decision whether the roster-named recipe is
    // buyable at this trainer, for this character, right now.

    // Row 1: a recipe of the trade, not known, affordable, within the cap.
    {
        auto const offer = Offer(3276, FIRST_AID, 60, 100);
        Check("an affordable recipe of the trade is buyable",
              RecipeToBuyNow(offer, FIRST_AID, true, 60, 10, 1000) == 3276);
    }
    // Row 2: wrong trade is never bought.
    {
        auto const offer = Offer(2387, TAILORING, 1, 10);
        Check("another trade's recipe is not buyable",
              RecipeToBuyNow(offer, FIRST_AID, true, 60, 10, 100000) == 0);
    }
    // Row 3: already known.
    {
        auto offer = Offer(3276, FIRST_AID, 60, 100);
        offer.known = true;
        Check("an already known recipe is not buyable",
              RecipeToBuyNow(offer, FIRST_AID, true, 60, 10, 100000) == 0);
    }
    // Row 4: a rank spell is the rank path's, not a recipe.
    {
        auto offer = Offer(3273, FIRST_AID, 50, 100);
        offer.startsSkill = true;
        Check("a rank spell is not buyable as a recipe",
              RecipeToBuyNow(offer, FIRST_AID, true, 60, 10, 100000) == 0);
    }
    // Row 5: skill or level too low.
    {
        auto const skillOffer = Offer(3276, FIRST_AID, 60, 100);
        auto const levelOffer = Offer(3276, FIRST_AID, 60, 100, 20);
        Check("skill below the requirement is not buyable",
              RecipeToBuyNow(skillOffer, FIRST_AID, true, 59, 10, 100000) == 0);
        Check("level below the requirement is not buyable",
              RecipeToBuyNow(levelOffer, FIRST_AID, true, 60, 10, 100000) == 0);
    }
    // Row 6: past the classic 300 cap is not buyable.
    {
        auto const offer = Offer(99999, TAILORING, 305, 100);
        Check("a recipe past the 300 cap is not buyable",
              RecipeToBuyNow(offer, TAILORING, true, 310, 70, 100000) == 0);
        Check("a recipe at the 300 cap is buyable",
              RecipeToBuyNow(Offer(99998, TAILORING, 300, 100), TAILORING, true,
                             300, 60, 100000) == 99998);
    }
    // Row 7: the purse floor. 1000 copper, floor 100: 900 may be spent.
    {
        Check("spending stops at the floor",
              RecipeToBuyNow(Offer(1, TAILORING, 1, 901), TAILORING, true, 50, 20,
                             1000) == 0);
        Check("exactly down to the floor is allowed",
              RecipeToBuyNow(Offer(1, TAILORING, 1, 900), TAILORING, true, 50, 20,
                             1000) == 1);
        Check("a free recipe is bought above the floor",
              RecipeToBuyNow(Offer(1, TAILORING, 1, 0), TAILORING, true, 50, 20,
                             RECIPE_PURSE_FLOOR_COPPER) == 1);
    }
    // Row 8: the trade must be held.
    {
        auto const offer = Offer(3276, FIRST_AID, 60, 100);
        Check("a trade the character does not hold buys nothing",
              RecipeToBuyNow(offer, FIRST_AID, false, 60, 10, 100000) == 0);
        Check("no trade at all buys nothing",
              RecipeToBuyNow(offer, 0, true, 60, 10, 100000) == 0);
    }

    // The adapter is wired: TrainOnArrival buys the named recipe through the
    // core's own trainer, reads it back, names it in the log, and releases the
    // errand (never retries every cycle) when the trainer will not teach it.
    {
        std::string const src = Source();
        Check("adapter source readable", !src.empty());
        Check("the roster names the recipe to buy",
              src.find("uint32 buyRecipe = 0;") != std::string::npos);
        Check("the buy_recipe column is loaded",
              src.find("plan.buyRecipe = fields[5].Get<uint16>();") != std::string::npos);
        Check("the trainer is asked whether it sells the named recipe",
              src.find("static uint32 RecipeTheTrainerSells(") != std::string::npos);
        Check("CanTeachSpell guards the named recipe",
              src.find("!trainer->CanTeachSpell(bot, &spell)") != std::string::npos);
        Check("the purchase goes through the core's TeachSpell",
              src.find("trainer->TeachSpell(npc, bot, spellId);") != std::string::npos);
        Check("the purchase is read back with HasSpell",
              src.find("if (bot->HasSpell(spellId))") != std::string::npos);
        Check("the log line names the recipe bought",
              src.find("bought recipe '{}' ({}) of {} ({})") != std::string::npos);
        Check("each bought recipe is its own learn event",
              src.find("RecordEvent(bot, \"learn\", spellId, recipe,") != std::string::npos);
        Check("can't teach means release, not retry",
              src.find("Releasing the errand.") != std::string::npos);
        Check("no rank but a recipe completes the errand",
              src.find("no rank was on offer") != std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
