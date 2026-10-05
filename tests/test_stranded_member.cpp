/*
 * A member stranded on another continent from its leader travels back as a
 * player would (#274): its hearthstone if it is bound on the leader's
 * continent and ready, otherwise its own faction's boat or zeppelin.
 *
 * Measured on the dev realm 2026-10-05: each family's leader on map 0 and one
 * follower of each on map 1, and the worldserver saying "THE PARTY IS SPLIT
 * ACROSS TWO MAPS AND NOTHING IN THIS MODULE CAN REJOIN IT" for both on every
 * restart. The crossing only ever moved a dungeon run's leader.
 *
 * Two halves. DecideStrandedWay picks the way across. The lone crossing is
 * ReadCrossing with one member, the stranded one, read as the leader of its own
 * crossing: walk to the berth, wait, step aboard a docked deck, ride, walk off,
 * done. The second half replays that whole sequence, so a change to
 * ReadCrossing that only works for a party is caught here.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::CrossingAction;
using OverseerDecisions::CrossingActionName;
using OverseerDecisions::CrossingLimits;
using OverseerDecisions::CrossingMember;
using OverseerDecisions::CrossingStep;
using OverseerDecisions::CrossingWorld;
using OverseerDecisions::DecideStrandedWay;
using OverseerDecisions::ShouldLeaveDeck;
using OverseerDecisions::ReadCrossing;
using OverseerDecisions::StrandedFacts;
using OverseerDecisions::StrandedWay;
using OverseerDecisions::StrandedWayExplanation;
using OverseerDecisions::StrandedWayName;

namespace
{

int failures = 0;

void CheckWay(char const* what, StrandedWay got, StrandedWay want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, StrandedWayName(got),
                StrandedWayName(want));
    ++failures;
}

void CheckAction(char const* what, CrossingAction got, CrossingAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, CrossingActionName(got),
                CrossingActionName(want));
    ++failures;
}

void CheckSays(char const* what, std::string const& said, char const* fragment)
{
    if (said.find(fragment) != std::string::npos)
        return;
    std::printf("FAIL %s: '%s' does not mention '%s'\n", what, said.c_str(), fragment);
    ++failures;
}

// The live case: alive, out of combat, on map 1 with its leader on map 0, its
// stone bound in Kalimdor (so no help), and its own faction's boat serving both.
StrandedFacts Stranded()
{
    StrandedFacts f;
    f.readable = true;
    f.alive = true;
    f.memberMap = 1;
    f.leaderMap = 0;
    f.carriesStone = true;
    f.stoneReady = true;
    f.boundOnLeaderMap = false;
    f.transportServes = true;
    return f;
}

void TheWayAcross()
{
    CheckWay("stranded with a boat and a stone bound on its own side sails",
             DecideStrandedWay(Stranded()), StrandedWay::Sail);

    StrandedFacts home = Stranded();
    home.boundOnLeaderMap = true;
    CheckWay("bound on the leader's continent with the stone ready hearths, ahead of the boat",
             DecideStrandedWay(home), StrandedWay::Hearth);

    StrandedFacts walking = home;
    walking.crossingUnderWay = true;
    CheckWay("a stone that comes ready while walking to the berth is still the shorter way",
             DecideStrandedWay(walking), StrandedWay::Hearth);

    StrandedFacts cooling = home;
    cooling.stoneReady = false;
    CheckWay("a stone cooling down does not stop a boat that serves",
             DecideStrandedWay(cooling), StrandedWay::Sail);
    cooling.transportServes = false;
    CheckWay("with no boat, a stone bound there is waited for",
             DecideStrandedWay(cooling), StrandedWay::WaitForStone);

    // Rule 6b: Uzza, four hours on a ledge in Stonetalon, stone bound in
    // Ratchet on its own continent, walk to the berth given up on the ground.
    StrandedFacts ledge = Stranded();
    ledge.boundOnMemberMap = true;
    ledge.berthWalkGaveUp = true;
    CheckWay("a berth walk given up on the ground hearths to the inn on its own map",
             DecideStrandedWay(ledge), StrandedWay::Hearth);
    CheckSays("the hearth out of a ledge says why",
              StrandedWayExplanation(ledge, StrandedWay::Hearth), "gave up on the ground");
    StrandedFacts ledgeCooling = ledge;
    ledgeCooling.stoneReady = false;
    CheckWay("after the hearth, the stone cooling down, it sails from the inn",
             DecideStrandedWay(ledgeCooling), StrandedWay::Sail);
    StrandedFacts walkingFine = ledge;
    walkingFine.berthWalkGaveUp = false;
    CheckWay("a berth walk that has not given up keeps sailing, stone or not",
             DecideStrandedWay(walkingFine), StrandedWay::Sail);
    StrandedFacts boundElsewhere = ledge;
    boundElsewhere.boundOnMemberMap = false;
    CheckWay("a stone bound on a third map is no way off the ledge",
             DecideStrandedWay(boundElsewhere), StrandedWay::Sail);

    StrandedFacts none = Stranded();
    none.transportServes = false;
    CheckWay("no boat and no stone bound there is refused", DecideStrandedWay(none),
             StrandedWay::Refuse);
    CheckSays("the refusal names both maps", StrandedWayExplanation(none, StrandedWay::Refuse),
              "joins map 1 and map 0");
    CheckSays("the refusal names the stone", StrandedWayExplanation(none, StrandedWay::Refuse),
              "not bound on map 0");
    none.carriesStone = false;
    CheckSays("a missing stone is said as missing",
              StrandedWayExplanation(none, StrandedWay::Refuse), "not carried");

    StrandedFacts nostone = home;
    nostone.carriesStone = false;
    nostone.stoneReady = false;
    CheckWay("a bind with no stone to use it is no hearth", DecideStrandedWay(nostone),
             StrandedWay::Sail);
}

void WhatComesFirst()
{
    StrandedFacts unread = Stranded();
    unread.readable = false;
    unread.aboard = true;
    CheckWay("an unreadable member is waited on, ahead of everything",
             DecideStrandedWay(unread), StrandedWay::Wait);

    StrandedFacts riding = Stranded();
    riding.aboard = true;
    riding.familyOwnsTheWay = true;
    riding.boundOnLeaderMap = true;
    CheckWay("a passenger sails on, even with a run started and its stone ready",
             DecideStrandedWay(riding), StrandedWay::Sail);
    riding.memberMap = 0;
    riding.crossingLanded = true;
    CheckWay("a passenger already on the leader's map is still supervised off the deck",
             DecideStrandedWay(riding), StrandedWay::Sail);

    StrandedFacts run = Stranded();
    run.familyOwnsTheWay = true;
    CheckWay("a dungeon run or the family's crossing owns the way", DecideStrandedWay(run),
             StrandedWay::StandDown);
    run.crossingUnderWay = true;
    CheckWay("and stands a crossing on foot down", DecideStrandedWay(run),
             StrandedWay::StandDown);

    StrandedFacts together = Stranded();
    together.memberMap = 0;
    CheckWay("on the leader's map with nothing under way is not stranded",
             DecideStrandedWay(together), StrandedWay::NotStranded);
    together.crossingUnderWay = true;
    CheckWay("its leader came to its continent mid-walk: the crossing is over",
             DecideStrandedWay(together), StrandedWay::NotStranded);
    together.crossingLanded = true;
    CheckWay("landed with its crossing under way has the walk off left",
             DecideStrandedWay(together), StrandedWay::Sail);

    StrandedFacts dead = Stranded();
    dead.alive = false;
    CheckWay("a ghost runs to its corpse first", DecideStrandedWay(dead), StrandedWay::Wait);
    StrandedFacts fighting = Stranded();
    fighting.inCombat = true;
    CheckWay("nothing sets off mid-fight", DecideStrandedWay(fighting), StrandedWay::Wait);
    StrandedFacts casting = Stranded();
    casting.boundOnLeaderMap = true;
    casting.hearthInFlight = true;
    CheckWay("a hearth in flight is not cast twice or walked out of",
             DecideStrandedWay(casting), StrandedWay::Wait);

    StrandedFacts blank;
    CheckWay("a default reading decides nothing", DecideStrandedWay(blank), StrandedWay::Wait);
}

// The stranded member, read as the one member of its own crossing.
CrossingMember Lone(std::uint32_t map, float berthDistance, bool aboard = false)
{
    CrossingMember m;
    m.name = "Ugga";
    m.readable = true;
    m.isLeader = true;
    m.mapId = map;
    m.berthDistance = berthDistance;
    m.aboard = aboard;
    return m;
}

CrossingWorld Route()
{
    CrossingWorld w;
    w.originMap = 1;
    w.destinationMap = 0;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.mooringKnown = true;
    return w;
}

CrossingLimits Limits()
{
    CrossingLimits l;
    l.berthArrivedYards = 12.f;
    l.gatherYards = 30.f;
    l.minBoardDwellMs = 15000;
    l.fetchPastYards = 1500.f;
    l.fetchWaitSeconds = 300;
    return l;
}

CrossingAction Step(CrossingWorld const& w, CrossingMember const& m)
{
    return ReadCrossing(w, std::vector<CrossingMember>{m}, Limits()).action;
}

void TheLoneCrossing()
{
    CrossingWorld w = Route();
    CheckAction("far from the berth, it walks", Step(w, Lone(1, 2400.f)), CrossingAction::Walk);
    CheckAction("at the berth with the boat away, it holds", Step(w, Lone(1, 4.f)),
                CrossingAction::Hold);

    w.dockedAtOrigin = true;
    w.dwellLeftMs = 40000;
    CheckAction("docked with time left, a lone member steps aboard: nobody to gather",
                Step(w, Lone(1, 4.f)), CrossingAction::Board);
    w.leaderWaitSeconds = 3600;
    CheckAction("and a long wait never fetches anybody", Step(w, Lone(1, 4.f)),
                CrossingAction::Board);
    w.leaderWaitSeconds = 0;
    w.dwellLeftMs = 5000;
    CheckAction("docked with the stop nearly over, it holds for the next",
                Step(w, Lone(1, 4.f)), CrossingAction::Hold);

    CheckAction("aboard at the origin as the boat casts off, it rides: nobody is left behind",
                Step(w, Lone(1, 0.f, true)), CrossingAction::Ride);
    w.dockedAtOrigin = false;
    CheckAction("aboard at sea, it rides", Step(w, Lone(1, 0.f, true)), CrossingAction::Ride);

    w.dockedAtDestination = true;
    w.dwellLeftMs = 40000;
    CheckAction("aboard and docked at the far end, it walks off", Step(w, Lone(0, 0.f, true)),
                CrossingAction::WalkOff);
    w.dockedAtDestination = false;
    CheckAction("aboard on the far map before the boat docks, it waits aboard",
                Step(w, Lone(0, 0.f, true)), CrossingAction::Disembark);
    CheckAction("ashore and off the deck, the crossing is over", Step(w, Lone(0, 0.f)),
                CrossingAction::Done);

    CrossingWorld guarded = Route();
    guarded.berthGuarded = true;
    CheckAction("a guarded berth is refused", Step(guarded, Lone(1, 2400.f)),
                CrossingAction::Refuse);
    CrossingWorld overdue = Route();
    overdue.overdue = true;
    CheckAction("an overdue crossing is refused", Step(overdue, Lone(1, 2400.f)),
                CrossingAction::Refuse);
    CheckAction("but never walks a passenger off a moving deck",
                Step(overdue, Lone(1, 0.f, true)), CrossingAction::Ride);
    CrossingWorld third = Route();
    CheckAction("a third map is refused", Step(third, Lone(530, 0.f)), CrossingAction::Refuse);
}

}  // namespace

void SteppingOffTheDeck()
{
    // Uzza at the Undercity tower: still aboard, docked, 2.0 yards from ground.
    if (!ShouldLeaveDeck(true, true, 2.0f, 2.0f))
    {
        std::printf("FAIL a passenger beside the dock leaves the transport\n");
        ++failures;
    }
    if (ShouldLeaveDeck(true, false, 1.0f, 2.0f))
    {
        std::printf("FAIL nobody steps off a transport that is not docked\n");
        ++failures;
    }
    if (ShouldLeaveDeck(true, true, 6.0f, 2.0f))
    {
        std::printf("FAIL a passenger short of the ground walks on rather than leaving\n");
        ++failures;
    }
    if (ShouldLeaveDeck(false, true, 1.0f, 2.0f))
    {
        std::printf("FAIL one already ashore has nothing to leave\n");
        ++failures;
    }
}

int main()
{
    SteppingOffTheDeck();
    TheWayAcross();
    WhatComesFirst();
    TheLoneCrossing();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a stranded member hearths or sails to its leader's continent\n");
    return 0;
}
