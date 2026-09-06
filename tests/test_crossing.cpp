/*
 * Getting a family from one continent to the other, and refusing to pretend.
 *
 * THE READING THIS PINS, measured on the live realm 2026-09-06 after a forced
 * save so the positions were the world's and not the table's:
 *
 *     Bork  map 1  ( 992, -2204)  offline
 *     Grog  map 1  ( 790, -2542)  online
 *     Og    map 1  ( 790, -2542)  online
 *     Grug  map 0  (-8601,  -496) online   <- the party leader
 *     Ugga  map 0  (-10042, -711) online
 *
 * An Alliance family of five on job `dungeon:wailing`, whose door is on map 1,
 * with its leader on the wrong continent from the dungeon and from three of its
 * four followers. Every drive declines: `follow` acts only while the master is
 * on the same map, the catch-up walk has nowhere on this map to aim, and an
 * `at:` aim is refused outright when its map is not the character's own.
 *
 * MOST OF THIS FILE IS ABOUT THE FIRST VERSION'S DEFECTS, found by review
 * before the first live crossing. Each has a case named after what it did:
 *
 *   * A follower boarding one second before the leader promoted the whole
 *     family to Ride, which released the leader's aim, and the boat left
 *     without him. Ride is now about the LEADER.
 *   * A passenger standing on the destination map was counted as riding and
 *     never as needing to get off, because `aboard` was read before the map and
 *     skipped it. Both are read now, and Disembark is its own leg.
 *   * "At the berth" was still called Walk, so the caller reclaimed an errand
 *     the travel drive had just released on arrival, and every reclaim
 *     restamped the clock the death breaker measures its window from. Hold is
 *     its own action and claims nothing.
 *   * A crossing had no backstop at all, while the death breaker declines to
 *     act on any errand a run owns because "the run answers for it". Overdue is
 *     a fact the caller can set.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::CrossingAction;
using OverseerDecisions::CrossingActionName;
using OverseerDecisions::CrossingExplanation;
using OverseerDecisions::CrossingLeg;
using OverseerDecisions::CrossingLegName;
using OverseerDecisions::CrossingLimits;
using OverseerDecisions::CrossingMember;
using OverseerDecisions::CrossingStep;
using OverseerDecisions::CrossingWorld;
using OverseerDecisions::ReadCrossing;

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

void CheckAction(char const* what, CrossingAction got, CrossingAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what,
                CrossingActionName(got), CrossingActionName(want));
    ++failures;
}

void CheckLeg(char const* what, CrossingLeg got, CrossingLeg want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, CrossingLegName(got),
                CrossingLegName(want));
    ++failures;
}

void CheckCount(char const* what, std::size_t got, std::size_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %zu, wanted %zu\n", what, got, want);
    ++failures;
}

void CheckSays(char const* what, std::string const& said, char const* fragment)
{
    if (said.find(fragment) != std::string::npos)
        return;
    std::printf("FAIL %s: '%s' does not mention '%s'\n", what, said.c_str(),
                fragment);
    ++failures;
}

// A crossing from the Eastern Kingdoms to Kalimdor with every fact in hand,
// including a boardable place at each end. Nothing in the world can supply the
// last two today, which is exactly why the tests supply them: the decision has
// to be provably right for the day something can.
CrossingWorld GoodWorld()
{
    CrossingWorld w;
    w.originMap = 0;
    w.destinationMap = 1;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.mooringKnown = true;
    w.berthGuarded = false;
    w.berthGuardLevel = 0;
    w.overdue = false;
    return w;
}

CrossingLimits Limits()
{
    CrossingLimits l;
    l.berthArrivedYards = 10.f;
    return l;
}

CrossingMember At(bool leader, std::uint32_t map, float berthDistance = 500.f)
{
    CrossingMember m;
    m.readable = true;
    m.isLeader = leader;
    m.aboard = false;
    m.mapId = map;
    m.berthDistance = berthDistance;
    return m;
}

CrossingMember Unread(bool leader = false)
{
    CrossingMember m;
    m.readable = false;
    m.isLeader = leader;
    return m;
}

CrossingMember Aboard(bool leader, std::uint32_t map)
{
    CrossingMember m = At(leader, map);
    m.aboard = true;
    return m;
}

// ------------------------------------------------- the reviewed defects --

// DEFECT 1. A follower stepping onto the deck one second before the leader
// promoted the whole family to Ride, and the caller's Ride branch released the
// leader's aim. The leader stopped walking and the boat left without him, every
// circuit. A passenger is never the character being aimed, so a passenger is
// never a reason to stop aiming the leader.
void AFollowerAboardDoesNotStopTheLeaderWalking()
{
    std::vector<CrossingMember> members = {At(true, 0, 400.f), Aboard(false, 0),
                                           At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("the leader keeps walking", step.action, CrossingAction::Walk);
    CheckLeg("on the walk-to-berth leg", step.leg, CrossingLeg::WalkToBerth);
    CheckCount("one follower aboard", step.aboard, 1u);
    Check("and the leader is not aboard", step.leaderAboard, false);
}

// The other half of the same rule: when it IS the leader, the transport owns
// the outcome and nothing may be aimed.
void TheLeaderAboardIsWhatRideMeans()
{
    std::vector<CrossingMember> members = {Aboard(true, 0), At(false, 0),
                                           At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("the leader aboard rides", step.action, CrossingAction::Ride);
    CheckLeg("on the aboard leg", step.leg, CrossingLeg::Aboard);
    Check("and the leader is aboard", step.leaderAboard, true);
    Check("and is not counted as on the origin map", step.leaderOnOrigin, false);
}

// DEFECT 2. `aboard` was read before the map and then skipped it, so a
// character who had arrived on the destination map while still standing on the
// deck could never be seen as needing to get off. It rode back.
void APassengerOnTheDestinationMapNeedsToGetOff()
{
    std::vector<CrossingMember> members = {Aboard(true, 1), Aboard(false, 1),
                                           At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("still on the deck at the far end", step.action,
                CrossingAction::Disembark);
    CheckLeg("on the disembark leg", step.leg, CrossingLeg::Disembark);
    CheckCount("two still aboard on the destination map", step.stillAboard, 2u);
    Check("and it is certainly not Done", step.action == CrossingAction::Done,
          false);
    CheckSays("and it says being on the deck is not being ashore",
              CrossingExplanation(step, GoodWorld()), "not the same fact as being ashore");
}

// And Done needs BOTH halves: on the destination map, and off every transport.
// A single passenger holds the whole crossing open.
void DoneNeedsEverybodyAshoreAndOffEveryTransport()
{
    std::vector<CrossingMember> allAshore = {At(true, 1), At(false, 1),
                                             At(false, 1), At(false, 1),
                                             At(false, 1)};
    CrossingWorld w = GoodWorld();
    w.transportFound = false;   // a landed party does not need a boat any more
    w.berthKnown = false;
    CrossingStep const done = ReadCrossing(w, allAshore, Limits());
    CheckAction("all five ashore is done", done.action, CrossingAction::Done);
    CheckLeg("on the ashore leg", done.leg, CrossingLeg::Ashore);
    CheckCount("five ashore", done.ashore, 5u);

    std::vector<CrossingMember> oneStillOn = allAshore;
    oneStillOn[3] = Aboard(false, 1);
    CrossingStep const held = ReadCrossing(w, oneStillOn, Limits());
    CheckAction("one passenger holds it open", held.action,
                CrossingAction::Disembark);
    CheckCount("and four are ashore, not five", held.ashore, 4u);
}

// DEFECT 3, at the decision's own level. A passenger's map flips under the
// transport's teleport rather than under anything the party did, so the reading
// must never treat a passenger as being somewhere by map alone.
void APassengerIsCountedAsAPassengerWhereverTheBoatIs()
{
    CrossingStep const onOrigin =
        ReadCrossing(GoodWorld(), {At(true, 0, 400.f), Aboard(false, 0)}, Limits());
    CheckCount("a passenger on the origin map is not waiting", onOrigin.waiting, 1u);
    CheckCount("and is counted aboard", onOrigin.aboard, 1u);

    CrossingStep const onDestination =
        ReadCrossing(GoodWorld(), {At(true, 0, 400.f), Aboard(false, 1)}, Limits());
    CheckCount("a passenger on the destination map is not ashore",
               onDestination.ashore, 0u);
    CheckCount("and is counted as still aboard", onDestination.stillAboard, 1u);
}

// DEFECT 4 (5a in the review). Arrival was still called Walk, so the caller
// reclaimed an errand the travel drive had just handed back at five yards, and
// every reclaim restamped the errand clock the death breaker measures from.
// Holding is a different instruction from walking and the caller acts on it
// differently: it claims nothing.
void AtTheBerthIsHoldingAndNotWalking()
{
    std::vector<CrossingMember> members = {At(true, 0, 4.f), At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("at the berth, hold", step.action, CrossingAction::Hold);
    CheckLeg("on the wait-for-transport leg", step.leg,
             CrossingLeg::WaitForTransport);
    Check("and the leader reads as at the berth", step.leaderAtBerth, true);
    CheckSays("and it says why nothing is reclaimed",
              CrossingExplanation(step, GoodWorld()), "restart the clock");
}

// One yard outside the tolerance is still a walk, so the two readings cannot
// both be false at once and leave nobody instructed.
void JustOutsideTheBerthIsStillAWalk()
{
    std::vector<CrossingMember> members = {At(true, 0, 10.5f), At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("outside the tolerance, walk", step.action, CrossingAction::Walk);
    Check("and it does not read as at the berth", step.leaderAtBerth, false);
}

// DEFECT 5 (5b in the review). The death breaker declines to call off any
// errand a run owns, on the stated grounds that the run's own stall handling
// answers for it. Every other run-owned claim sits behind a backstop; this one
// shipped with no timer at all, so it was the one errand nothing could stop.
void AnOverdueCrossingIsGivenUpOn()
{
    CrossingWorld w = GoodWorld();
    w.overdue = true;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0, 400.f), At(false, 1)}, Limits());
    CheckAction("an overdue crossing refuses", step.action,
                CrossingAction::Refuse);
    CheckSays("and says the breaker cannot do it for us",
              CrossingExplanation(step, w), "nothing can ever stop");
}

// But a passenger outranks the backstop, because giving up on a crossing does
// not get anybody off a boat, and an aim issued now would pull them off it.
void AnOverdueCrossingStillDoesNotDisturbAPassenger()
{
    CrossingWorld w = GoodWorld();
    w.overdue = true;
    CrossingStep const step = ReadCrossing(w, {Aboard(true, 0)}, Limits());
    CheckAction("aboard outranks overdue", step.action, CrossingAction::Ride);
}

// ---------------------------------------------------------- fail closed --

// THE BRANCH THE WHOLE FILE IS BUILT AROUND. Four of five read cleanly, two of
// them already on the destination map. That is not four fifths of an answer.
void OneUnreadableMemberOutranksEveryOtherReading()
{
    std::vector<CrossingMember> members = {At(true, 0, 4200.f), At(false, 0),
                                           At(false, 1), At(false, 1), Unread()};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("a party with a logged-out member waits", step.action,
                CrossingAction::Wait);
    CheckLeg("and names no leg", step.leg, CrossingLeg::Unknown);
    CheckCount("one member unreadable", step.unreadable, 1u);
    CheckSays("and says how many it could not read",
              CrossingExplanation(step, GoodWorld()), "1 of 5");
}

void FourAshoreAndOneUnreadableIsNotAnArrival()
{
    std::vector<CrossingMember> members = {At(true, 1), At(false, 1),
                                           At(false, 1), At(false, 1), Unread()};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("four ashore and one unread is not done", step.action,
                CrossingAction::Wait);
    Check("and it is certainly not Done", step.action == CrossingAction::Done,
          false);
}

void AnEmptyRosterIsNotAnArrival()
{
    CrossingStep const step = ReadCrossing(GoodWorld(), {}, Limits());
    CheckAction("no members at all waits", step.action, CrossingAction::Wait);
    CheckLeg("and names no leg", step.leg, CrossingLeg::Unknown);
}

void ACrossingFromAMapToItselfIsRefused()
{
    CrossingWorld w = GoodWorld();
    w.destinationMap = 0;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 0)}, Limits());
    CheckAction("one map twice is refused", step.action, CrossingAction::Refuse);
    CheckSays("and says so", CrossingExplanation(step, w), "map 0 twice");
}

// ------------------------------------------------------------ refusals --

void NoTransportIsRefusedRatherThanWaitedOn()
{
    CrossingWorld w = GoodWorld();
    w.transportFound = false;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0, 400.f), At(false, 1)}, Limits());
    CheckAction("no transport refuses", step.action, CrossingAction::Refuse);
    CheckSays("and says no transport serves both maps",
              CrossingExplanation(step, w), "no transport is known");
}

// THE CORRECTION AT THE HEART OF THIS REVISION. A transport's stop frame is the
// SHIP's mooring, over water beside a pier, and the first version took it for a
// berth and would have aimed the family at it. `berthKnown` now means the world
// agreed a character may stand there, and nothing can say that yet.
void AMooringIsNotABerthAndABerthlessCrossingRefuses()
{
    CrossingWorld w = GoodWorld();
    w.berthKnown = false;      // the mooring is known; a standable place is not
    w.mooringKnown = true;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0, 400.f), At(false, 1)}, Limits());
    CheckAction("no boardable place refuses", step.action,
                CrossingAction::Refuse);
    CheckSays("and says a stop frame is the ship's mooring",
              CrossingExplanation(step, w), "SHIP's");
    CheckSays("and says it will not derive a pier from one",
              CrossingExplanation(step, w), "derive a pier");
    CheckSays("and says the deck cannot be stepped onto either",
              CrossingExplanation(step, w), "sixty yards");
}

void ACrossingThatCannotEndIsNotStarted()
{
    CrossingWorld w = GoodWorld();
    w.landingKnown = false;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0, 400.f), At(false, 1)}, Limits());
    CheckAction("no landing refuses", step.action, CrossingAction::Refuse);
    CheckSays("and says a crossing that cannot end is not started",
              CrossingExplanation(step, w), "cannot end");
}

// The gate that must not be lost: a destination the party cannot survive is not
// a destination, however correct its coordinates are.
void AGuardedBerthIsNotADestination()
{
    CrossingWorld w = GoodWorld();
    w.berthGuarded = true;
    w.berthGuardLevel = 65;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0, 400.f), At(false, 1)}, Limits());
    CheckAction("a guarded berth refuses", step.action, CrossingAction::Refuse);
    CheckSays("and names the level that guards it",
              CrossingExplanation(step, w), "level 65");
    CheckSays("and gives the reason a destination is refused",
              CrossingExplanation(step, w), "cannot survive");
}

void AMemberOnAThirdMapIsRefusedInItsOwnWords()
{
    std::vector<CrossingMember> members = {At(true, 0, 400.f), At(false, 1),
                                           At(false, 530)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("a third map refuses", step.action, CrossingAction::Refuse);
    CheckLeg("and names the off-route leg", step.leg, CrossingLeg::OffRoute);
    CheckCount("one member off route", step.offRoute, 1u);
    CheckSays("and says no boat calls there",
              CrossingExplanation(step, GoodWorld()), "neither map 0 nor map 1");
}

// Only the leader is ever aimed, so a leader already across leaves nothing to
// aim, and that is a real split somebody has to hear about rather than
// something to work around by aiming a follower.
void ALeaderAlreadyAshoreLeavesNothingToAim()
{
    std::vector<CrossingMember> members = {At(true, 1), At(false, 0),
                                           At(false, 0)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("a leader ashore with followers behind refuses", step.action,
                CrossingAction::Refuse);
    Check("and the leader is not on the origin map", step.leaderOnOrigin, false);
    CheckSays("and says only the leader is ever aimed",
              CrossingExplanation(step, GoodWorld()), "only ever aims the leader");
}

// ----------------------------------------------------------- the words --

void EveryActionSaysSomething()
{
    CrossingWorld const w = GoodWorld();
    std::vector<std::vector<CrossingMember>> const cases = {
        {Unread()},
        {At(true, 0, 400.f), At(false, 1)},
        {At(true, 0, 1.f), At(false, 1)},
        {Aboard(true, 0)},
        {Aboard(true, 1)},
        {At(true, 1)},
        {At(true, 0, 400.f), At(false, 530)},
    };
    for (std::vector<CrossingMember> const& members : cases)
    {
        CrossingStep const step = ReadCrossing(w, members, Limits());
        if (!CrossingExplanation(step, w).empty())
            continue;
        std::printf("FAIL '%s' said nothing\n", CrossingActionName(step.action));
        ++failures;
    }
}

void EveryValueHasItsOwnName()
{
    std::vector<CrossingLeg> const legs = {
        CrossingLeg::Unknown, CrossingLeg::OffRoute, CrossingLeg::WalkToBerth,
        CrossingLeg::WaitForTransport, CrossingLeg::Aboard,
        CrossingLeg::Disembark, CrossingLeg::Ashore};
    for (std::size_t i = 0; i < legs.size(); ++i)
        for (std::size_t j = i + 1; j < legs.size(); ++j)
            if (std::string(CrossingLegName(legs[i])) == CrossingLegName(legs[j]))
            {
                std::printf("FAIL two legs share the name '%s'\n",
                            CrossingLegName(legs[i]));
                ++failures;
            }

    std::vector<CrossingAction> const actions = {
        CrossingAction::Wait, CrossingAction::Refuse, CrossingAction::Walk,
        CrossingAction::Hold, CrossingAction::Ride, CrossingAction::Disembark,
        CrossingAction::Done};
    for (std::size_t i = 0; i < actions.size(); ++i)
        for (std::size_t j = i + 1; j < actions.size(); ++j)
            if (std::string(CrossingActionName(actions[i])) ==
                CrossingActionName(actions[j]))
            {
                std::printf("FAIL two actions share the name '%s'\n",
                            CrossingActionName(actions[i]));
                ++failures;
            }
}

// Being handed a better world never turns a blind reading into progress, and
// being handed a worse one never turns a passenger into somebody to order
// about. Both directions, because the ordering is the whole design.
void MoreFactsNeverRescueABlindReading()
{
    std::vector<CrossingMember> blind = {At(true, 0, 400.f), Unread()};
    CrossingWorld w = GoodWorld();
    CheckAction("blind with a full world", ReadCrossing(w, blind, Limits()).action,
                CrossingAction::Wait);
    w.berthGuarded = true;
    w.overdue = true;
    CheckAction("blind with a guarded, overdue crossing",
                ReadCrossing(w, blind, Limits()).action, CrossingAction::Wait);
    w.transportFound = false;
    CheckAction("blind with no boat", ReadCrossing(w, blind, Limits()).action,
                CrossingAction::Wait);
}

void FewerFactsNeverDisturbAPassenger()
{
    CrossingWorld w = GoodWorld();
    w.transportFound = false;
    w.berthKnown = false;
    w.landingKnown = false;
    w.berthGuarded = true;
    w.overdue = true;
    CheckAction("the leader aboard still rides",
                ReadCrossing(w, {Aboard(true, 0), At(false, 1)}, Limits()).action,
                CrossingAction::Ride);
    CheckAction("a passenger at the far end still disembarks",
                ReadCrossing(w, {Aboard(true, 1)}, Limits()).action,
                CrossingAction::Disembark);
}

} // namespace

int main()
{
    AFollowerAboardDoesNotStopTheLeaderWalking();
    TheLeaderAboardIsWhatRideMeans();
    APassengerOnTheDestinationMapNeedsToGetOff();
    DoneNeedsEverybodyAshoreAndOffEveryTransport();
    APassengerIsCountedAsAPassengerWhereverTheBoatIs();
    AtTheBerthIsHoldingAndNotWalking();
    JustOutsideTheBerthIsStillAWalk();
    AnOverdueCrossingIsGivenUpOn();
    AnOverdueCrossingStillDoesNotDisturbAPassenger();

    OneUnreadableMemberOutranksEveryOtherReading();
    FourAshoreAndOneUnreadableIsNotAnArrival();
    AnEmptyRosterIsNotAnArrival();
    ACrossingFromAMapToItselfIsRefused();

    NoTransportIsRefusedRatherThanWaitedOn();
    AMooringIsNotABerthAndABerthlessCrossingRefuses();
    ACrossingThatCannotEndIsNotStarted();
    AGuardedBerthIsNotADestination();
    AMemberOnAThirdMapIsRefusedInItsOwnWords();
    ALeaderAlreadyAshoreLeavesNothingToAim();

    EveryActionSaysSomething();
    EveryValueHasItsOwnName();
    MoreFactsNeverRescueABlindReading();
    FewerFactsNeverDisturbAPassenger();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a crossing that cannot board anybody refuses, and says which "
                "fact it is missing\n");
    return 0;
}
