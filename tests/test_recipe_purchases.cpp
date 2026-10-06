/*
 * A roster character at a profession trainer buys the recipes it can use and
 * afford, not just the rank.
 *
 * TrainOnArrival bought only the next rank (Journeyman, Expert, Artisan), a
 * ceiling that teaches nothing to make. Measured on the dev realm: a family
 * member held First Aid at 59 and never learned Heavy Linen Bandage (3276), and
 * the tailoring ladder stalled for the same reason. PlanRecipePurchases is the
 * decision for the second half of a visit; each check below is one row of its
 * table. The last part reads src/mod_overseer.cpp (run from the repo root) to
 * pin that the arrival path and the training stop both ask it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::PlanRecipePurchases;
using OverseerDecisions::RECIPE_PURSE_FLOOR_COPPER;
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

constexpr uint32_t FIRST_AID = 129;
constexpr uint32_t TAILORING = 197;

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

std::string Join(std::vector<uint32_t> const& v)
{
    std::ostringstream out;
    for (uint32_t id : v)
        out << id << ' ';
    return out.str();
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
    // Row 1: the reported case. First Aid at 60, Heavy Linen Bandage offered at
    // 60 for 100 copper, a purse that covers it.
    {
        auto const plan = PlanRecipePurchases({Offer(3276, FIRST_AID, 60, 100)}, FIRST_AID, true,
                                              60, 10, 1000);
        Check("heavy linen bandage is bought", plan == std::vector<uint32_t>{3276});
    }
    // Row 2: no profession, no recipe. The same list for a character without it.
    {
        auto const plan = PlanRecipePurchases({Offer(3276, FIRST_AID, 60, 100)}, FIRST_AID, false,
                                              60, 10, 100000);
        Check("a trade it does not hold buys nothing", plan.empty());
    }
    // Row 3: a recipe of another trade on the same trainer list is never bought.
    {
        auto const plan = PlanRecipePurchases(
            {Offer(2387, TAILORING, 1, 10), Offer(3276, FIRST_AID, 60, 100)}, FIRST_AID, true, 60,
            10, 100000);
        Check("another trade's recipe is left", plan == std::vector<uint32_t>{3276});
    }
    // Row 4: already known.
    {
        TrainerRecipeOffer known = Offer(3276, FIRST_AID, 60, 100);
        known.known = true;
        Check("a known recipe is not bought again",
              PlanRecipePurchases({known}, FIRST_AID, true, 60, 10, 100000).empty());
    }
    // Row 5: a rank spell belongs to the rank path.
    {
        TrainerRecipeOffer rank = Offer(3273, FIRST_AID, 50, 100);
        rank.startsSkill = true;
        Check("a rank spell is not a recipe",
              PlanRecipePurchases({rank}, FIRST_AID, true, 60, 10, 100000).empty());
    }
    // Row 6: skill short, level short.
    {
        Check("skill below the requirement buys nothing",
              PlanRecipePurchases({Offer(3276, FIRST_AID, 60, 100)}, FIRST_AID, true, 59, 10,
                                  100000).empty());
        Check("level below the requirement buys nothing",
              PlanRecipePurchases({Offer(3276, FIRST_AID, 60, 100, 20)}, FIRST_AID, true, 60, 10,
                                  100000).empty());
    }
    // Row 7: the classic cap: a recipe past 300 is never bought.
    {
        Check("a recipe past the 300 cap is left",
              PlanRecipePurchases({Offer(99999, TAILORING, 305, 100)}, TAILORING, true, 310, 70,
                                  100000).empty());
        Check("a recipe at the 300 cap is bought",
              PlanRecipePurchases({Offer(99998, TAILORING, 300, 100)}, TAILORING, true, 300, 60,
                                  100000) == std::vector<uint32_t>{99998});
    }
    // Row 8: cheapest first, then lowest skill requirement, then spell id.
    {
        auto const plan = PlanRecipePurchases(
            {Offer(30, TAILORING, 100, 500), Offer(10, TAILORING, 50, 200),
             Offer(21, TAILORING, 40, 200), Offer(20, TAILORING, 40, 200)},
            TAILORING, true, 150, 30, 100000);
        Check("cheapest, then rank, then id", Join(plan) == "20 21 10 30 ");
    }
    // Row 9: the purse floor. 1000 copper, floor 100: 900 may be spent.
    {
        std::vector<TrainerRecipeOffer> offers{Offer(1, TAILORING, 1, 400),
                                               Offer(2, TAILORING, 1, 500),
                                               Offer(3, TAILORING, 1, 600)};
        Check("spending stops at the floor",
              Join(PlanRecipePurchases(offers, TAILORING, true, 50, 20, 1000)) == "1 2 ");
        Check("exactly down to the floor is allowed",
              Join(PlanRecipePurchases({Offer(1, TAILORING, 1, 900)}, TAILORING, true, 50, 20,
                                       1000)) == "1 ");
        Check("one copper under the floor is refused",
              PlanRecipePurchases({Offer(1, TAILORING, 1, 901)}, TAILORING, true, 50, 20,
                                  1000).empty());
    }
    // Row 10: a purse at or under the floor buys nothing, even a free recipe
    // (nothing left to be a floor for), and a free recipe above it is bought.
    {
        Check("a purse under the floor buys nothing",
              PlanRecipePurchases({Offer(1, TAILORING, 1, 0)}, TAILORING, true, 50, 20,
                                  RECIPE_PURSE_FLOOR_COPPER - 1).empty());
        Check("a free recipe is bought above the floor",
              PlanRecipePurchases({Offer(1, TAILORING, 1, 0)}, TAILORING, true, 50, 20,
                                  RECIPE_PURSE_FLOOR_COPPER) == std::vector<uint32_t>{1});
    }
    // Row 11: nothing offered.
    Check("an empty list buys nothing",
          PlanRecipePurchases({}, TAILORING, true, 50, 20, 100000).empty());

    // The adapter asks the decision on both paths.
    {
        std::string const src = Source();
        Check("adapter source readable", !src.empty());
        Check("arrival path buys recipes",
              src.find("BuyRecipes(trainer, npc, bot, skill)") != std::string::npos);
        Check("adapter asks the decision",
              src.find("OverseerDecisions::PlanRecipePurchases(") != std::string::npos);
        Check("training stop admits a recipes-only learner",
              src.find("RecipesToBuy(trainer, nullptr, bot, plan->second.learnSkill)") !=
                  std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
