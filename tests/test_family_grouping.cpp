/*
 * One party per family, and never a party of enemies.
 *
 * mod-overseer#548, step 1. KeepRosterGrouped read the whole roster into a
 * single party under whichever `lead` row sorted first. That was correct while
 * overseer_roster held one family. With an Alliance five and a Horde five it
 * built ONE party of both, which the operator caught on 2026-09-21: the two
 * factions cannot be in a party together. The `family` column that says who
 * belongs to whom has existed since schema 2026_09_19_00 and nothing in the
 * module read it.
 *
 * What is pinned here is the partition: who ends up in which roster and who
 * leads it. Actually forming the Group needs the core and is compiled by the
 * adapter check; the decision that used to be wrong is testable without it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::FamilyMember;
using OverseerDecisions::FamilyRoster;
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

void CheckSize(char const* what, size_t got, size_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %zu, wanted %zu\n", what, got, want);
    ++failures;
}

// The rows exactly as KeepRosterGrouped reads them: `lead` DESC, then name. The
// two heads sort to the front, which is the very thing that made the old code
// wrong - the first `lead` row won for everybody.
std::vector<FamilyMember> BothFamilies()
{
    return {
        {"Grug", "Grug", true}, {"Zug", "Zug", true},
        {"Bork", "Grug", false}, {"Grog", "Grug", false}, {"Og", "Grug", false},
        {"Oz", "Zug", false},   {"Ugga", "Grug", false}, {"Uzza", "Zug", false},
        {"Zork", "Zug", false}, {"Zrog", "Zug", false},
    };
}

FamilyRoster const* Find(std::vector<FamilyRoster> const& all, std::string const& family)
{
    for (FamilyRoster const& r : all)
    {
        if (r.family == family)
            return &r;
    }
    return nullptr;
}

bool Has(FamilyRoster const& r, std::string const& name)
{
    for (FamilyMember const& m : r.members)
    {
        if (m.name == name)
            return true;
    }
    return false;
}

void TwoFamiliesAreTwoRostersNotOne()
{
    // THE DEFECT. Both heads carry `lead`, so a single pass that keeps "the
    // first lead row" made Grug the leader of everybody, Zug included.
    auto const all = PartitionRosterByFamily(BothFamilies());
    CheckSize("two families, two rosters", all.size(), 2);
}

void NobodyIsInTheOtherFamilysRoster()
{
    auto const all = PartitionRosterByFamily(BothFamilies());
    FamilyRoster const* grug = Find(all, "Grug");
    FamilyRoster const* zug = Find(all, "Zug");
    Check("the Grug roster exists", grug != nullptr, true);
    Check("the Zug roster exists", zug != nullptr, true);
    if (!grug || !zug)
        return;
    CheckSize("the Alliance five", grug->members.size(), 5);
    CheckSize("the Horde five", zug->members.size(), 5);
    for (char const* horde : {"Zug", "Oz", "Uzza", "Zork", "Zrog"})
        Check("no Horde character in the Alliance party", Has(*grug, horde), false);
    for (char const* alliance : {"Grug", "Bork", "Grog", "Og", "Ugga"})
        Check("no Alliance character in the Horde party", Has(*zug, alliance), false);
}

void EachFamilyLeadsItself()
{
    // The other half of the defect: not just who is grouped with whom, but who
    // is told to follow whom.
    auto const all = PartitionRosterByFamily(BothFamilies());
    FamilyRoster const* grug = Find(all, "Grug");
    FamilyRoster const* zug = Find(all, "Zug");
    Check("both families exist to be led", grug && zug, true);
    if (!grug || !zug)
        return;
    CheckStr("Grug leads the Alliance", grug->leader, "Grug");
    CheckStr("Zug leads the Horde", zug->leader, "Zug");
}

void TheOrderTheCallerGaveIsKept()
{
    // The caller sorts `lead` DESC, so the leader is first and the first member
    // is the one who forms the party. Re-sorting here would move that decision
    // somewhere no test can see it.
    auto const all = PartitionRosterByFamily(BothFamilies());
    CheckSize("two families to order", all.size(), 2);
    if (all.size() != 2)
        return;
    CheckStr("families come out in first-seen order, first", all[0].family, "Grug");
    CheckStr("families come out in first-seen order, second", all[1].family, "Zug");
    CheckStr("a family's leader is still its first member",
             all[1].members.front().name, "Zug");
    CheckStr("members keep the order given", all[0].members[1].name, "Bork");
}

void OneFamilyBehavesExactlyAsBefore()
{
    // The floor: a roster with a single family is what every world had until
    // now, and it must still produce exactly one party under its one leader.
    std::vector<FamilyMember> one = {
        {"Grug", "Grug", true}, {"Bork", "Grug", false}, {"Ugga", "Grug", false}};
    auto const all = PartitionRosterByFamily(one);
    CheckSize("one family, one roster", all.size(), 1);
    if (all.size() != 1)
        return;
    CheckSize("with everybody in it", all[0].members.size(), 3);
    CheckStr("under its leader", all[0].leader, "Grug");
}

void ADisabledFamilyChangesNothingForTheOther()
{
    // enabled = 0 rows never reach this function (the query filters them), so
    // "the Horde is off" is simply "the Horde rows are absent". The Alliance
    // must come out identical either way.
    std::vector<FamilyMember> alliance;
    for (FamilyMember const& m : BothFamilies())
    {
        if (m.family == "Grug")
            alliance.push_back(m);
    }
    auto const withoutHorde = PartitionRosterByFamily(alliance);
    auto const withHorde = PartitionRosterByFamily(BothFamilies());
    FamilyRoster const* grug = Find(withHorde, "Grug");
    Check("the Alliance is found either way", grug && !withoutHorde.empty(), true);
    if (!grug || withoutHorde.empty())
        return;
    CheckSize("Alliance size is unchanged by the Horde", grug->members.size(),
              withoutHorde[0].members.size());
    CheckStr("Alliance leader is unchanged by the Horde", grug->leader,
             withoutHorde[0].leader);
}

void AFamilyWithNoLeaderHasNone()
{
    // Never the other family's. A family with no `lead` row keeps whatever
    // leader its group already has, which is what the old code did for the
    // whole roster.
    std::vector<FamilyMember> rows = {
        {"Grug", "Grug", true}, {"Bork", "Grug", false},
        {"Oz", "Zug", false},   {"Uzza", "Zug", false}};
    auto const all = PartitionRosterByFamily(rows);
    FamilyRoster const* zug = Find(all, "Zug");
    Check("the Zug family exists", zug != nullptr, true);
    if (!zug)
        return;
    CheckStr("a family with no lead row has no leader, and is not handed Grug",
             zug->leader, "");
}

void ABlankFamilyIsNotAdoptedByAnother()
{
    // A row with no cohort must not be quietly folded into whichever cohort
    // comes first - that is the same defect on a smaller scale.
    std::vector<FamilyMember> rows = {
        {"Grug", "Grug", true}, {"Stray", "", false}, {"Bork", "Grug", false}};
    auto const all = PartitionRosterByFamily(rows);
    CheckSize("a blank family is its own roster", all.size(), 2);
    FamilyRoster const* grug = Find(all, "Grug");
    Check("the Grug roster exists", grug != nullptr, true);
    if (grug)
        Check("Stray is not in the Grug roster", Has(*grug, "Stray"), false);
}

void NoRowsIsNoRosters()
{
    CheckSize("empty in, empty out", PartitionRosterByFamily({}).size(), 0);
}

}  // namespace

int main()
{
    TwoFamiliesAreTwoRostersNotOne();
    NobodyIsInTheOtherFamilysRoster();
    EachFamilyLeadsItself();
    TheOrderTheCallerGaveIsKept();
    OneFamilyBehavesExactlyAsBefore();
    ADisabledFamilyChangesNothingForTheOther();
    AFamilyWithNoLeaderHasNone();
    ABlankFamilyIsNotAdoptedByAnother();
    NoRowsIsNoRosters();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: two families are two parties, each under its own leader\n");
    return EXIT_SUCCESS;
}
