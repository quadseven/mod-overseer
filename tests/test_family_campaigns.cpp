/*
 * One dungeon campaign per family, running at the same time (mod-overseer#555).
 *
 * The coordinator was one state machine fed by ChooseCampaignRoster, which
 * returns the family of the first `lead` row. The second family could never
 * run a dungeon while the first had a leader, so an order like "this family
 * through Ragefire Chasm fifty times while the other keeps questing" had no way
 * to happen.
 *
 * Pinned here: which families get a coordinator, that two families never share
 * one active run row for the same instance map, and that two families opening a
 * campaign in the same poll never share a campaign id. Driving the coordinators
 * needs the core and is compiled by the adapter check.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::CampaignRosters;
using OverseerDecisions::ChooseCampaignRoster;
using OverseerDecisions::DungeonMapHeldByAnotherFamily;
using OverseerDecisions::FamilyMember;
using OverseerDecisions::FamilyRoster;
using OverseerDecisions::FamilyRunClaim;
using OverseerDecisions::NextCampaignId;
using OverseerDecisions::PartitionRosterByFamily;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckStr(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckNum(char const* what, unsigned long got, unsigned long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lu, wanted %lu\n", what, got, want);
    ++failures;
}

// `lead` DESC then name, the order LoadCampaignRoster reads them in.
std::vector<FamilyMember> TwoFamilies()
{
    return {
        {"Alda", "Alda", true},  {"Zeno", "Zeno", true},
        {"Bram", "Alda", false}, {"Cato", "Alda", false},
        {"Xeph", "Zeno", false}, {"Yuri", "Zeno", false},
    };
}

void BothFamiliesGetACampaign()
{
    std::vector<FamilyRoster> const all = PartitionRosterByFamily(TwoFamilies());
    std::vector<FamilyRoster const*> const driven = CampaignRosters(all);
    CheckNum("two families, two campaigns", driven.size(), 2);
    if (driven.size() != 2)
        return;
    CheckStr("first family first", driven[0]->family, "Alda");
    CheckStr("first family's own leader", driven[0]->leader, "Alda");
    CheckStr("second family second", driven[1]->family, "Zeno");
    CheckStr("second family's own leader", driven[1]->leader, "Zeno");
    for (FamilyMember const& m : driven[1]->members)
        Check("no first-family name in the second campaign", m.family == "Zeno", true);
}

void ASingleFamilyDrivesExactlyWhatItAlwaysDid()
{
    std::vector<FamilyMember> rows = {
        {"Alda", "Alda", true}, {"Bram", "Alda", false}, {"Cato", "Alda", false}};
    std::vector<FamilyRoster> const all = PartitionRosterByFamily(rows);
    std::vector<FamilyRoster const*> const driven = CampaignRosters(all);
    CheckNum("one family, one campaign", driven.size(), 1);
    if (driven.size() == 1)
        Check("the same family the one-campaign picker chose",
              driven[0] == ChooseCampaignRoster(all), true);
}

void AFamilyWithNoLeaderIsNotDriven()
{
    std::vector<FamilyMember> rows = {
        {"Alda", "Alda", true}, {"Bram", "Alda", false},
        {"Xeph", "Zeno", false}, {"Yuri", "Zeno", false}};
    std::vector<FamilyRoster> const all = PartitionRosterByFamily(rows);
    std::vector<FamilyRoster const*> const driven = CampaignRosters(all);
    CheckNum("the leaderless family has no campaign", driven.size(), 1);
    if (!driven.empty())
        CheckStr("the led family still does", driven[0]->family, "Alda");
    CheckNum("no rosters, no campaigns", CampaignRosters({}).size(), 0);
}

void ADisabledFamilyLeavesTheOtherAlone()
{
    // The second family's rows are what an enabled = 0 flip removes from the
    // read. The first family's campaign is the same object either way.
    std::vector<FamilyMember> rows = {
        {"Zeno", "Zeno", true}, {"Xeph", "Zeno", false}, {"Yuri", "Zeno", false}};
    std::vector<FamilyRoster> const all = PartitionRosterByFamily(rows);
    std::vector<FamilyRoster const*> const driven = CampaignRosters(all);
    CheckNum("the remaining family is still driven", driven.size(), 1);
    if (!driven.empty())
        CheckStr("by its own leader", driven[0]->leader, "Zeno");
}

void AnotherFamilysRunHoldsTheMap()
{
    std::vector<FamilyRunClaim> const claims = {
        {"Alda", 43, true},     // in Wailing Caverns
        {"Zeno", 389, true},    // in Ragefire Chasm
    };
    Check("the other family is on this map", DungeonMapHeldByAnotherFamily("Zeno", 43, claims),
          true);
    Check("a family's own run never blocks it",
          DungeonMapHeldByAnotherFamily("Zeno", 389, claims), false);
    Check("a map nobody is on is free", DungeonMapHeldByAnotherFamily("Zeno", 36, claims),
          false);

    std::vector<FamilyRunClaim> const idle = {{"Alda", 43, false}};
    Check("an idle coordinator claims nothing",
          DungeonMapHeldByAnotherFamily("Zeno", 43, idle), false);
    Check("no claims, no hold", DungeonMapHeldByAnotherFamily("Zeno", 43, {}), false);
}

void TwoCampaignsNeverShareAnId()
{
    // The table's MAX + 1 has not seen the other family's stamp yet.
    CheckNum("one past what the other family holds", NextCampaignId(7, {7}), 8);
    CheckNum("the table wins when it is ahead", NextCampaignId(9, {7}), 9);
    CheckNum("an unallocated coordinator counts for nothing", NextCampaignId(4, {0, 0}), 4);
    CheckNum("the largest held id is what is stepped past", NextCampaignId(3, {5, 2}), 6);
    CheckNum("nothing held is the table's answer", NextCampaignId(1, {}), 1);
}

}  // namespace

int main()
{
    BothFamiliesGetACampaign();
    ASingleFamilyDrivesExactlyWhatItAlwaysDid();
    AFamilyWithNoLeaderIsNotDriven();
    ADisabledFamilyLeavesTheOtherAlone();
    AnotherFamilysRunHoldsTheMap();
    TwoCampaignsNeverShareAnId();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: every led family runs its own campaign, and none share a map or an id\n");
    return EXIT_SUCCESS;
}
