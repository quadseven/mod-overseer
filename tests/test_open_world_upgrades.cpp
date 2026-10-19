/*
 * The open world seeks upgrades (#973): which quest reward a turn-in takes,
 * which bag a member buys, and which vendor piece it buys after that.
 *
 * WHAT IS PINNED HERE:
 *
 *   - A turn-in takes the choice that scores highest for the holder among the
 *     ones that are an upgrade for the holder. Score, not gain, because that
 *     is what the ticket asks the event to be checked against.
 *   - When none is an upgrade for the holder, the choice that is an upgrade
 *     for a sibling, the largest share of new worth first, so the hand-over
 *     (#14) has something worth handing over.
 *   - When nothing is an upgrade for anybody, the holder's best wearable
 *     score, and failing that the best sale price; a quest whose choices are
 *     all reagents still pays the most it can.
 *   - A bag is bought only into an empty bag slot, the largest one the purse
 *     covers, the cheaper of two the same size.
 *   - A vendor piece is bought only once all four bag slots hold bags (bags
 *     come before any other discretionary spend), only when it is certainly
 *     better than what is worn, and only out of half the purse, so the next
 *     repair is still paid for. The largest gain that half covers wins.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::BagOffer;
using OverseerDecisions::ChooseBagPurchase;
using OverseerDecisions::ChooseQuestReward;
using OverseerDecisions::ChooseVendorUpgrade;
using OverseerDecisions::GearComparison;
using OverseerDecisions::QuestRewardDetail;
using OverseerDecisions::QuestRewardOption;
using OverseerDecisions::QuestRewardPick;
using OverseerDecisions::QuestRewardSibling;
using OverseerDecisions::QuestRewardWhy;
using OverseerDecisions::VendorUpgradeOffer;

namespace
{

int failures = 0;

void ExpectInt(char const* what, int got, int want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

void ExpectText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

QuestRewardOption Choice(unsigned entry, GearComparison holder, float score, unsigned sell)
{
    QuestRewardOption option;
    option.entry = entry;
    option.holder = holder;
    option.holderWearable = true;
    option.holderScore = score;
    option.sellPrice = sell;
    return option;
}

QuestRewardOption Reagent(unsigned entry, unsigned sell)
{
    QuestRewardOption option;
    option.entry = entry;
    option.sellPrice = sell;
    return option;
}

void ExpectPick(char const* what, QuestRewardPick const& got, int index, QuestRewardWhy why)
{
    ExpectInt(what, got.index, index);
    ExpectInt(what, static_cast<int>(got.why), static_cast<int>(why));
}

// Ten turn-ins, the number the acceptance criterion counts, each with the
// index the holder's score says it must take.
void TheHolderTakesItsHighestScoringUpgrade()
{
    struct Case
    {
        char const* what;
        std::vector<QuestRewardOption> options;
        int want;
    };
    GearComparison const up = GearComparison::Better;
    GearComparison const no = GearComparison::NotBetter;
    GearComparison const unsure = GearComparison::Undecided;
    std::vector<Case> const cases = {
        {"one upgrade among three", {Choice(1, no, 40.f, 90), Choice(2, up, 20.f, 10), Choice(3, no, 5.f, 5)}, 1},
        {"the higher of two upgrades", {Choice(1, up, 20.f, 10), Choice(2, up, 35.f, 10)}, 1},
        {"a bigger score that is not an upgrade loses", {Choice(1, no, 90.f, 10), Choice(2, up, 12.f, 10)}, 1},
        {"the sale price does not outbid an upgrade", {Choice(1, up, 8.f, 1), Choice(2, no, 4.f, 900)}, 0},
        {"undecided is not an upgrade", {Choice(1, unsure, 60.f, 10), Choice(2, up, 30.f, 10)}, 1},
        {"a tie keeps the first", {Choice(1, up, 30.f, 10), Choice(2, up, 30.f, 10)}, 0},
        {"four choices, the last best", {Choice(1, up, 1.f, 1), Choice(2, up, 2.f, 1), Choice(3, up, 3.f, 1), Choice(4, up, 4.f, 1)}, 3},
        {"a reagent beside an upgrade", {Reagent(1, 500), Choice(2, up, 9.f, 2)}, 1},
        {"a single upgrade", {Choice(1, up, 7.f, 0)}, 0},
        {"five choices, the middle best", {Choice(1, up, 3.f, 1), Choice(2, no, 70.f, 1), Choice(3, up, 33.f, 1), Choice(4, up, 31.f, 1), Choice(5, no, 2.f, 1)}, 2},
    };
    for (Case const& c : cases)
        ExpectPick(c.what, ChooseQuestReward(c.options), c.want, QuestRewardWhy::HolderUpgrade);
}

void ASiblingUpgradeWhenNoneIsTheHolders()
{
    QuestRewardOption a = Choice(11, GearComparison::NotBetter, 50.f, 100);
    QuestRewardOption b = Choice(12, GearComparison::NotBetter, 10.f, 10);
    QuestRewardOption c = Choice(13, GearComparison::NotBetter, 10.f, 10);
    b.siblings = {QuestRewardSibling{"Alpha", GearComparison::Better, 40}};
    c.siblings = {QuestRewardSibling{"Beta", GearComparison::Undecided, 90},
                  QuestRewardSibling{"Gamma", GearComparison::Better, 65}};
    QuestRewardPick const pick = ChooseQuestReward({a, b, c});
    ExpectPick("the largest sibling upgrade", pick, 2, QuestRewardWhy::SiblingUpgrade);
    ExpectText("the pick names the sibling it is for", pick.recipient, "Gamma");
    ExpectText("the detail names the reward and the sibling",
               QuestRewardDetail(c, pick), "reward 13, an upgrade for Gamma");

    // The holder's own upgrade still wins over a sibling's.
    QuestRewardOption mine = Choice(14, GearComparison::Better, 5.f, 1);
    ExpectPick("the holder first", ChooseQuestReward({c, mine}), 1, QuestRewardWhy::HolderUpgrade);
}

void NothingIsAnUpgradeForAnybody()
{
    ExpectPick("the holder's best score",
               ChooseQuestReward({Choice(21, GearComparison::NotBetter, 5.f, 900),
                                  Choice(22, GearComparison::NotBetter, 15.f, 1)}),
               1, QuestRewardWhy::HolderBest);
    QuestRewardPick const pick = ChooseQuestReward({Reagent(31, 40), Reagent(32, 75), Reagent(33, 60)});
    ExpectPick("the best sale price", pick, 1, QuestRewardWhy::SellPrice);
    ExpectText("the detail says why", QuestRewardDetail(Reagent(32, 75), pick),
               "reward 32, the best sale price");
    ExpectPick("no choices at all", ChooseQuestReward({}), -1, QuestRewardWhy::None);
}

void TheHolderDetail()
{
    QuestRewardOption const option = Choice(41, GearComparison::Better, 5.f, 1);
    QuestRewardPick const pick = ChooseQuestReward({option});
    ExpectText("an upgrade for the holder", QuestRewardDetail(option, pick),
               "reward 41, an upgrade for the holder");
}

void BagsFillEmptySlotsWithTheLargestAffordable()
{
    std::vector<BagOffer> const offers = {
        BagOffer{101, 6, 100, true},
        BagOffer{102, 10, 2500, true},
        BagOffer{103, 8, 900, true},
        BagOffer{104, 8, 700, true},
        BagOffer{105, 12, 50, false},
    };
    ExpectInt("the largest the purse covers", ChooseBagPurchase(offers, 2, 1000), 3);
    ExpectInt("all of them affordable", ChooseBagPurchase(offers, 1, 5000), 1);
    ExpectInt("only the smallest affordable", ChooseBagPurchase(offers, 4, 150), 0);
    ExpectInt("no empty bag slot", ChooseBagPurchase(offers, 0, 5000), -1);
    ExpectInt("nothing affordable", ChooseBagPurchase(offers, 3, 10), -1);
    ExpectInt("nothing on offer", ChooseBagPurchase({}, 3, 10000), -1);
}

void VendorGearWaitsForTheBags()
{
    std::vector<VendorUpgradeOffer> const offers = {
        VendorUpgradeOffer{201, GearComparison::Better, 4.f, 300, true},
        VendorUpgradeOffer{202, GearComparison::Better, 9.f, 800, true},
        VendorUpgradeOffer{203, GearComparison::Undecided, 30.f, 10, true},
        VendorUpgradeOffer{204, GearComparison::NotBetter, 0.f, 1, true},
        VendorUpgradeOffer{205, GearComparison::Better, 50.f, 10, false},
    };
    ExpectInt("bags come first", ChooseVendorUpgrade(offers, 1, 100000), -1);
    ExpectInt("the largest gain half the purse covers", ChooseVendorUpgrade(offers, 0, 1600), 1);
    ExpectInt("half the purse stays", ChooseVendorUpgrade(offers, 0, 1000), 0);
    ExpectInt("nothing affordable", ChooseVendorUpgrade(offers, 0, 500), -1);
}

}  // namespace

int main()
{
    TheHolderTakesItsHighestScoringUpgrade();
    ASiblingUpgradeWhenNoneIsTheHolders();
    NothingIsAnUpgradeForAnybody();
    TheHolderDetail();
    BagsFillEmptySlotsWithTheLargestAffordable();
    VendorGearWaitsForTheBags();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the open world upgrade decisions hold\n");
    return EXIT_SUCCESS;
}
