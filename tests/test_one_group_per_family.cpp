/*
 * One group per family, led by its head (#607), and one instance copy per
 * family (#620).
 *
 * The realm runs with LeaveGroupOnLogout on, so every relog of the head's
 * client takes him out of the family group and the core hands the lead to a
 * member. These pin how the family is put back together: which group is the
 * family's, who leaves a stray group, who joins, and when the dungeon
 * coordinator may let anybody through a door.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::FamilyGroupBreach;
using OverseerDecisions::FamilyGroupPlan;
using OverseerDecisions::FamilyGroupSeat;
using OverseerDecisions::InAnotherInstanceCopy;
using OverseerDecisions::InstanceSpot;
using OverseerDecisions::PlanFamilyGroup;
using OverseerDecisions::SplitAcrossInstanceCopies;

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

FamilyGroupSeat Seat(char const* name, bool head, std::uint64_t group, bool leads = false,
                     bool present = true, bool foreign = false)
{
    FamilyGroupSeat seat;
    seat.name = name;
    seat.head = head;
    seat.present = present;
    seat.groupId = group;
    seat.groupIsForeign = foreign;
    seat.leadsItsGroup = leads;
    return seat;
}

bool Is(std::vector<std::string> const& got, std::vector<std::string> const& want)
{
    return got == want;
}

void TheHeadInAStrayGroupPullsTheFamilyToHim()
{
    // The measured shape: the head back in a group of his own, the members in
    // the group the core handed to Bork. Before this change the members were
    // skipped as "in a party of their own" and the split lasted.
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Grug", true, 7, true), Seat("Bork", false, 3, true),
        Seat("Grog", false, 3),      Seat("Og", false, 7),
        Seat("Ugga", false, 3)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("the head's group is the family's", plan.targetGroupId == 7);
    Check("nothing is formed", plan.founder.empty());
    Check("the stray three leave", Is(plan.leave, {"Bork", "Grog", "Ugga"}));
    Check("the stray three join", Is(plan.join, {"Bork", "Grog", "Ugga"}));
    Check("nothing untouched", plan.untouched.empty());
}

void TheHeadBackInNoGroupJoinsTheFamily()
{
    // The ordinary relog: the head returns groupless and joins the members'
    // group. Nobody leaves anything.
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Grug", true, 0), Seat("Bork", false, 3, true), Seat("Grog", false, 3)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("the members' group is kept", plan.targetGroupId == 3);
    Check("nobody leaves", plan.leave.empty());
    Check("the head joins", Is(plan.join, {"Grug"}));
}

void WithoutTheHeadTheBiggestGroupWins()
{
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Grug", true, 0, false, false), Seat("Bork", false, 4, true),
        Seat("Grog", false, 9, true), Seat("Og", false, 9), Seat("Ugga", false, 0)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("the group of two is the family's", plan.targetGroupId == 9);
    Check("Bork leaves his group of one", Is(plan.leave, {"Bork"}));
    Check("Bork and Ugga join", Is(plan.join, {"Bork", "Ugga"}));

    std::vector<FamilyGroupSeat> const tie = {
        Seat("Bork", false, 4, true), Seat("Grog", false, 9, true)};
    Check("a tie goes to roster order", PlanFamilyGroup(tie).targetGroupId == 4);
}

void NoGroupAtAllIsFormedUnderTheHead()
{
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Bork", false, 0), Seat("Grug", true, 0), Seat("Grog", false, 0)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("formed", plan.targetGroupId == 0);
    Check("under the head, wherever he sorts", plan.founder == "Grug");
    Check("the others join", Is(plan.join, {"Bork", "Grog"}));

    std::vector<FamilyGroupSeat> const away = {
        Seat("Grug", true, 0, false, false), Seat("Bork", false, 0), Seat("Grog", false, 0)};
    Check("with the head away, under the first present member",
          PlanFamilyGroup(away).founder == "Bork");
}

void AGroupTheCoreOwnsIsLeftAlone()
{
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Grug", true, 5, true, true, true), Seat("Bork", false, 3, true),
        Seat("Grog", false, 0)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("a battleground group is not the family's", plan.targetGroupId == 3);
    Check("the head in it is untouched", Is(plan.untouched, {"Grug"}));
    Check("and not moved", Is(plan.join, {"Grog"}));
}

void AWholeFamilyNeedsNoMoves()
{
    std::vector<FamilyGroupSeat> const seats = {
        Seat("Grug", true, 3, true), Seat("Bork", false, 3), Seat("Grog", false, 3)};
    FamilyGroupPlan const plan = PlanFamilyGroup(seats);
    Check("no leave", plan.leave.empty());
    Check("no join", plan.join.empty());
    Check("whole", FamilyGroupBreach(seats).empty());
}

void TheBreachSaysWhatIsWrong()
{
    std::vector<FamilyGroupSeat> const stray = {
        Seat("Grug", true, 7, true), Seat("Bork", false, 3, true), Seat("Grog", false, 7)};
    Check("a member in another group",
          FamilyGroupBreach(stray) == "'Bork' is in another group");

    std::vector<FamilyGroupSeat> const memberLeads = {
        Seat("Grug", true, 3), Seat("Bork", false, 3, true)};
    Check("a member leads",
          FamilyGroupBreach(memberLeads) == "the head 'Grug' does not lead the group");

    std::vector<FamilyGroupSeat> const headAway = {
        Seat("Grug", true, 0, false, false), Seat("Bork", false, 3, true)};
    Check("the head is away",
          FamilyGroupBreach(headAway) == "the head 'Grug' is not in the world");

    std::vector<FamilyGroupSeat> const headLoose = {
        Seat("Grug", true, 0), Seat("Bork", false, 3, true)};
    Check("the head in no group",
          FamilyGroupBreach(headLoose) == "the head 'Grug' is in no family group");

    std::vector<FamilyGroupSeat> const memberAway = {
        Seat("Grug", true, 3, true), Seat("Bork", false, 0, false, false),
        Seat("Og", false, 0)};
    Check("members missing and loose",
          FamilyGroupBreach(memberAway) ==
              "'Bork' is not in the world; 'Og' is in no group");

    std::vector<FamilyGroupSeat> const headless = {
        Seat("Bork", false, 3, true), Seat("Grog", false, 3)};
    Check("no seat is the head",
          FamilyGroupBreach(headless) == "the roster names no head");

    std::vector<FamilyGroupSeat> const alone = {Seat("Grug", true, 0)};
    Check("a family of one is whole", FamilyGroupBreach(alone).empty());
}

void AnotherCopyIsNotInside()
{
    // (memberMap, memberInstance, headMap, headInstance). The measured case:
    // four members in copy 1 of Ragefire, the head logged into copy 2.
    Check("another copy of the head's map", InAnotherInstanceCopy(389, 1, 389, 2));
    Check("the same copy", !InAnotherInstanceCopy(389, 2, 389, 2));
    Check("a different map is not a copy", !InAnotherInstanceCopy(1, 0, 389, 2));
    Check("the head's copy unknown", !InAnotherInstanceCopy(389, 1, 389, 0));

    std::vector<InstanceSpot> const split = {{389, 1}, {389, 1}, {389, 2}, {1, 0}};
    Check("the family is split over two copies", SplitAcrossInstanceCopies(split, 389));
    std::vector<InstanceSpot> const together = {{389, 3}, {389, 3}, {1, 0}};
    Check("one copy", !SplitAcrossInstanceCopies(together, 389));
    Check("nobody on the map", !SplitAcrossInstanceCopies(together, 36));
}

}  // namespace

int main()
{
    TheHeadInAStrayGroupPullsTheFamilyToHim();
    TheHeadBackInNoGroupJoinsTheFamily();
    WithoutTheHeadTheBiggestGroupWins();
    NoGroupAtAllIsFormedUnderTheHead();
    AGroupTheCoreOwnsIsLeftAlone();
    AWholeFamilyNeedsNoMoves();
    TheBreachSaysWhatIsWrong();
    AnotherCopyIsNotInside();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("one group per family: all passed\n");
    return 0;
}
