/*
 * Source contract for the open world's upgrades (#973).
 *
 * The decisions are proved in test_open_world_upgrades.cpp. What only the
 * adapter can get wrong is whether anything asks them: every turn-in this
 * module makes takes the reward ChooseQuestReward names, the quest_reward row
 * carries that choice, and a member finished at a town trip's counter buys
 * its bags and then its upgrade. A decision nothing feeds is the failure this
 * repository has shipped before, so the wiring is pinned as text.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

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

void Expect(std::string const& body, char const* where, char const* fed)
{
    if (!body.empty() && body.find(fed) != std::string::npos)
        return;
    std::cerr << where << " must use: " << fed << "\n";
    ++failures;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const hook = Body(text, "void OnPlayerCompleteQuest(");
    Expect(hook, "OnPlayerCompleteQuest", "TakeRewardChoice(player, quest->GetQuestId())");

    std::string const choose = Body(text, "static QuestRewardChosen ChooseQuestRewardFor(");
    Expect(choose, "ChooseQuestRewardFor", "OverseerDecisions::ChooseQuestReward(");
    Expect(choose, "ChooseQuestRewardFor", "OverseerDecisions::QuestRewardDetail(");

    std::string const trainer = Body(text, "static void HandInAtTrainer(");
    Expect(trainer, "HandInAtTrainer", "ChooseQuestRewardFor(bot, quest)");
    Expect(trainer, "HandInAtTrainer", "RewardQuestChosen(bot, quest, reward, npc)");

    std::string const row = Body(text, "static char const* DoQuest(");
    Expect(row, "DoQuest", "ChooseQuestRewardFor(player, quest)");
    Expect(row, "DoQuest", "RewardQuestChosen(player, quest, reward, giver)");

    std::string const dungeon = Body(text, "static bool RewardDungeonQuestHolders(");
    Expect(dungeon, "RewardDungeonQuestHolders", "ChooseQuestRewardFor(player, quest)");
    Expect(dungeon, "RewardDungeonQuestHolders", "RewardQuestChosen(player, quest, reward, giver)");

    std::string const shop = Body(text, "static void ShopAtTheCounter(");
    Expect(shop, "ShopAtTheCounter", "BuyBagsAtTheCounter(");
    Expect(shop, "ShopAtTheCounter", "BuyUpgradeAtTheCounter(");
    Expect(Body(text, "static uint32 BuyBagsAtTheCounter("), "BuyBagsAtTheCounter",
           "OverseerDecisions::ChooseBagPurchase(");
    Expect(Body(text, "static void BuyUpgradeAtTheCounter("), "BuyUpgradeAtTheCounter",
           "OverseerDecisions::ChooseVendorUpgrade(");
    Expect(Body(text, "void DriveTownTripFor("), "DriveTownTripFor", "ShopAtTheCounter(name, bot)");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "every turn-in chooses its reward and the counter sells bags first\n";
    return EXIT_SUCCESS;
}
