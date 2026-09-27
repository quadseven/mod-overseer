/*
 * A dungeon-finder group is let go at RESET, not counted against it (#761).
 *
 * Measured on the dev realm 2026-09-27: after a run the finder took in, the
 * next run held at RESET on "this is a battleground, battlefield or
 * dungeon-finder group, which Group::ResetInstances refuses outright" with
 * every member outside, and closed reset_failed at the backstop. The call
 * that disbands the finder group sits below the blocker check and was never
 * reached. With nobody inside, a finder group is no blocker, so the disband
 * runs and the family's own party forms again.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::InstanceOccupiedBlocker;
using OverseerDecisions::ResetGroupShape;
using OverseerDecisions::ResetGroupShapeBlocker;

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

bool Contains(std::string const& text, std::string const& part)
{
    return text.find(part) != std::string::npos;
}

void AFinderGroupWithNobodyInsideIsNoBlocker()
{
    ResetGroupShape shape;
    shape.finderGroup = true;
    Check("a finder group led by the family's leader is no blocker",
          ResetGroupShapeBlocker(shape, "Uzza").empty());

    // The finder may hand the lead to somebody else. The group is about to be
    // disbanded, so whose bind it would reset does not matter.
    shape.leaderLeads = false;
    Check("a finder group led by somebody else is no blocker either",
          ResetGroupShapeBlocker(shape, "Uzza").empty());

    // With nobody inside, the whole reset precondition is clear, which is what
    // lets RESETTING reach the disband.
    Check("and an empty instance adds no blocker",
          InstanceOccupiedBlocker(389, 10, {}, {"Zug", "Uzza"}).empty());
}

void AFinderGroupWithSomebodyInsideStillWaits()
{
    // The shape passes, and occupancy is what holds it: the disband must not
    // pull a member out of a group while he stands in the instance.
    ResetGroupShape shape;
    shape.finderGroup = true;
    Check("the shape passes", ResetGroupShapeBlocker(shape, "Uzza").empty());
    Check("an occupied instance still blocks",
          !InstanceOccupiedBlocker(389, 10, {"Zork"}, {"Zork", "Uzza"}).empty());
}

void BattlegroundAndBattlefieldGroupsStillRefuse()
{
    ResetGroupShape bg;
    bg.battleground = true;
    std::string const bgWhy = ResetGroupShapeBlocker(bg, "Uzza");
    Check("a battleground group blocks", !bgWhy.empty());
    Check("and says why", Contains(bgWhy, "refuses outright"));

    ResetGroupShape bf;
    bf.battlefield = true;
    Check("a battlefield group blocks", !ResetGroupShapeBlocker(bf, "Uzza").empty());
}

void AnOrdinaryPartyStillNeedsItsLeader()
{
    ResetGroupShape party;
    Check("an ordinary party led by the family's leader passes",
          ResetGroupShapeBlocker(party, "Uzza").empty());

    party.leaderLeads = false;
    std::string const why = ResetGroupShapeBlocker(party, "Uzza");
    Check("an ordinary party led by somebody else blocks", !why.empty());
    Check("and names the leader", Contains(why, "'Uzza' is not the group leader"));
}

}   // namespace

int main()
{
    AFinderGroupWithNobodyInsideIsNoBlocker();
    AFinderGroupWithSomebodyInsideStillWaits();
    BattlegroundAndBattlefieldGroupsStillRefuse();
    AnOrdinaryPartyStillNeedsItsLeader();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("finder group at reset: all checks passed\n");
    return 0;
}
