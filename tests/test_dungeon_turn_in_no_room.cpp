/*
 * A holder whose bags cannot take a dungeon quest's reward does not hold EXIT
 * (#1067), and the counter's bags are bought out of a bounded budget and
 * recorded.
 *
 * #1062 made the dungeon turn-in ask CanRewardQuest with the chosen reward,
 * which also asks whether the reward items fit. A holder with full bags failed
 * that on every poll and the turn-in held EXIT for ever: the caller stamps
 * last_progress_at each poll, and the town trip that would empty the bags
 * refuses while a run holds. The same shape #1015 fixed for a quest with no
 * ender. The same review found the counter's bag purchase could spend the
 * whole purse one bag at a time, and recorded nothing saying what it bought.
 *
 * The decisions are asked directly. The adapter needs a worldserver, so its
 * wiring is pinned as text.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using OverseerDecisions::BagOffer;
using OverseerDecisions::ChooseBagPurchase;
using OverseerDecisions::CounterBagBudget;
using OverseerDecisions::CounterBuyDetail;
using OverseerDecisions::CounterBuyWhy;
using OverseerDecisions::DungeonTurnInHolder;
using OverseerDecisions::DungeonTurnInStep;
using OverseerDecisions::DungeonTurnInStepFor;
using OverseerDecisions::DungeonTurnInStepWord;

namespace
{
int failures = 0;

void Check(std::string const& what, bool ok)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

void ExpectStep(char const* what, DungeonTurnInHolder const& holder, DungeonTurnInStep want)
{
    DungeonTurnInStep const got = DungeonTurnInStepFor(holder);
    if (got != want)
    {
        std::cerr << "FAIL: " << what << ": got " << DungeonTurnInStepWord(got) << ", want "
                  << DungeonTurnInStepWord(want) << "\n";
        ++failures;
    }
}

DungeonTurnInHolder AtTheEnder()
{
    DungeonTurnInHolder holder;
    holder.inWorld = true;
    holder.atEnder = true;
    holder.rewardable = true;
    holder.rewardFits = true;
    return holder;
}

void TestFullBagsDoNotHoldExit()
{
    DungeonTurnInHolder full = AtTheEnder();
    full.rewardFits = false;
    // The bug: this was a wait, every poll, for ever.
    ExpectStep("a holder whose bags cannot take the reward is left, not waited on", full,
               DungeonTurnInStep::LeaveNoRoom);
    Check("leaving a holder does not hold EXIT",
          DungeonTurnInStepFor(full) != DungeonTurnInStep::Wait);

    ExpectStep("a holder with room hands it in", AtTheEnder(), DungeonTurnInStep::Reward);

    DungeonTurnInHolder done = AtTheEnder();
    done.rewarded = true;
    done.rewardFits = false;
    ExpectStep("a holder already rewarded is done, whatever its bags", done,
               DungeonTurnInStep::Done);
}

void TestEverythingElseStillWaits()
{
    DungeonTurnInHolder away = AtTheEnder();
    away.atEnder = false;
    away.rewardFits = false;
    ExpectStep("a holder not yet at the ender is waited for, bags or not", away,
               DungeonTurnInStep::Wait);

    DungeonTurnInHolder gone = AtTheEnder();
    gone.inWorld = false;
    ExpectStep("a holder not in the world is waited for", gone, DungeonTurnInStep::Wait);

    DungeonTurnInHolder unfinished = AtTheEnder();
    unfinished.rewardable = false;
    unfinished.rewardFits = false;
    ExpectStep("a quest the core will not reward for its own reasons still waits", unfinished,
               DungeonTurnInStep::Wait);
}

void TestBagsHaveABoundedBudget()
{
    Check("a visit's bags get half the purse", CounterBagBudget(10000) == 5000);
    Check("an empty purse buys no bag", CounterBagBudget(0) == 0);

    // Four empty slots and a purse of 1000: the same 400-copper bag, bought one
    // at a time, each against what is left of the visit's budget. Against the
    // purse it bought two and left 200 copper; against the budget it buys one
    // and keeps 600.
    std::vector<BagOffer> const offers = {{805, 10, 400, true}};
    std::uint64_t purse = 1000;
    std::uint64_t budget = CounterBagBudget(purse);
    int bought = 0;
    for (int emptySlots = 4; emptySlots > 0; --emptySlots)
    {
        int const pick = ChooseBagPurchase(offers, emptySlots, budget);
        if (pick < 0)
            break;
        std::uint64_t const price = offers[static_cast<std::size_t>(pick)].price;
        purse -= price;
        budget -= price;
        ++bought;
    }
    Check("the visit buys only what half the purse covers", bought == 1);
    Check("at least half the purse is kept for the repair", purse >= 500);
}

void TestBuysAreSaid()
{
    std::string const bag = CounterBuyDetail(CounterBuyWhy::Bag, 400, 1000, 10, 0.f);
    Check("a bag's row says what it was for and what it cost",
          bag.find("10-slot bag") != std::string::npos &&
              bag.find("400 copper of a 1000 copper purse") != std::string::npos);
    std::string const upgrade = CounterBuyDetail(CounterBuyWhy::Upgrade, 800, 2000, 0, 12.34f);
    Check("an upgrade's row names its gain",
          upgrade.find("upgrade of 12.3") != std::string::npos &&
              upgrade.find("800 copper") != std::string::npos);
    Check("the detail fits the event column", bag.size() < 255 && upgrade.size() < 255);
}

// The text from `signature` to the next line closing a member function at the
// class's indentation, or empty when the signature is missing.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find(to, start + 1);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void TestAdapterWiring()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        Check("cannot read src/mod_overseer.cpp (run from the repo root)", false);
        return;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const reward = Body(text, "static bool RewardDungeonQuestHolders(");
    Check("RewardDungeonQuestHolders asks DungeonTurnInStepFor",
          reward.find("OverseerDecisions::DungeonTurnInStepFor(holder)") != std::string::npos);
    Check("it asks the quest's own state apart from the bags",
          reward.find("CanRewardQuest(quest, false)") != std::string::npos &&
              reward.find("CanRewardQuest(quest, reward.index, false)") != std::string::npos);
    std::string const leave = Between(reward, "if (step == DungeonTurnInStep::LeaveNoRoom)",
                                      "if (step == DungeonTurnInStep::Wait)");
    Check("a holder with no room is skipped and said once, not held",
          !leave.empty() && leave.find("continue;") != std::string::npos &&
              leave.find("return false") == std::string::npos &&
              leave.find("leftNoRoom.insert(") != std::string::npos);

    std::string const turnIn = Body(text, "bool DriveDungeonQuestTurnIn(");
    Check("the turn-in keeps the run's record of who was left",
          turnIn.find("coord.turnInLeftNoRoom") != std::string::npos);

    std::string const bags = Body(text, "static uint32 BuyBagsAtTheCounter(");
    Check("the bags are bought out of CounterBagBudget",
          bags.find("OverseerDecisions::CounterBagBudget(bot->GetMoney())") != std::string::npos &&
              bags.find("ChooseBagPurchase(\n                offers, CountFreeBagSlots(bot), budget)") !=
                  std::string::npos);
    Check("each bag bought draws the budget down",
          bags.find("budget -= std::min(budget, spent);") != std::string::npos);

    std::string const buy = Body(text, "static bool BuyForSelf(");
    Check("every counter purchase is recorded as a counter_buy event",
          buy.find("RecordEvent(bot, \"counter_buy\"") != std::string::npos &&
              buy.find("OverseerDecisions::CounterBuyDetail(") != std::string::npos);

    std::string const shop = Body(text, "static void ShopAtTheCounter(");
    Check("a visit that bought a bag buys no vendor piece",
          shop.find("if (BuyBagsAtTheCounter(name, bot, lines) > 0)\n            return;") !=
              std::string::npos);
}
}  // namespace

int main()
{
    TestFullBagsDoNotHoldExit();
    TestEverythingElseStillWaits();
    TestBagsHaveABoundedBudget();
    TestBuysAreSaid();
    TestAdapterWiring();
    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_dungeon_turn_in_no_room: ok\n";
    return EXIT_SUCCESS;
}
