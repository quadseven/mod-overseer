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
 * Five characters alive at full health, an Alliance family of humans, a dwarf
 * and a gnome, on job `dungeon:wailing`, whose door is on map 1. The leader is
 * on the wrong continent from the dungeon and from three of its four
 * followers, and every drive in the module declines: `follow` acts only while
 * the master is on the same map, the catch-up walk has nowhere on this map to
 * aim, and an `at:` aim is refused outright when its map is not the
 * character's own.
 *
 * The thing this test exists to pin is that the answer to that is a BOAT, that
 * the module's whole contribution to a boat is walking one character to a
 * berth, and that every fact it cannot read resolves toward doing nothing.
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

// Menethil Harbour on map 0 to Theramore on map 1, which is the Alliance
// route this family would actually take, with every fact in hand.
CrossingWorld GoodWorld()
{
    CrossingWorld w;
    w.originMap = 0;
    w.destinationMap = 1;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.berthGuarded = false;
    w.berthGuardLevel = 0;
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

// The family exactly as it was read tonight: leader and one follower on map 0,
// two followers on map 1, one unreadable because it is logged out.
std::vector<CrossingMember> TheFamilyTonight()
{
    return {At(true, 0, 4200.f), At(false, 0, 4600.f), At(false, 1),
            At(false, 1), Unread()};
}

// ---------------------------------------------------------------- fail closed

// THE BRANCH THE WHOLE FILE IS BUILT AROUND. Bork is logged out. Four of five
// read cleanly, two of them already standing on the destination map. That is
// not four fifths of an answer, it is no answer: nothing whatever is known
// about the fifth, including whether it is on a boat, in an instance, or in
// the ocean.
void OneUnreadableMemberOutranksEveryOtherReading()
{
    CrossingStep const step =
        ReadCrossing(GoodWorld(), TheFamilyTonight(), Limits());
    CheckAction("a party with a logged-out member waits", step.action,
                CrossingAction::Wait);
    CheckLeg("and names no leg", step.leg, CrossingLeg::Unknown);
    CheckCount("one member unreadable", step.unreadable, 1u);
    CheckSays("and says how many it could not read",
              CrossingExplanation(step, GoodWorld()), "1 of 5");
}

// The same shape at the far end, which is the one that would actually cost
// something: four members read on the destination map and one unreadable is
// not an arrival, and calling it one would end the crossing with a character
// nobody has seen.
void FourAshoreAndOneUnreadableIsNotAnArrival()
{
    std::vector<CrossingMember> members = {At(true, 1), At(false, 1),
                                           At(false, 1), At(false, 1),
                                           Unread()};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("four ashore and one unread is not done", step.action,
                CrossingAction::Wait);
    Check("and it is certainly not Done",
          step.action == CrossingAction::Done, false);
}

// An empty roster is a caller that lost its roster, not a crossing that
// finished. It must not grade as arrived either.
void AnEmptyRosterIsNotAnArrival()
{
    CrossingStep const step = ReadCrossing(GoodWorld(), {}, Limits());
    CheckAction("no members at all waits", step.action, CrossingAction::Wait);
    CheckLeg("and names no leg", step.leg, CrossingLeg::Unknown);
}

// A crossing from a map to itself is a caller bug. Answering Done would hide
// it behind a success.
void ACrossingFromAMapToItselfIsRefused()
{
    CrossingWorld w = GoodWorld();
    w.destinationMap = 0;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 0)}, Limits());
    CheckAction("one map twice is refused", step.action,
                CrossingAction::Refuse);
    CheckSays("and says so", CrossingExplanation(step, w), "map 0 twice");
}

// ------------------------------------------------------------ the refusals

// No boat is a refusal and not a wait. Waiting implies the fact arrives on
// its own; this one does not, and a permanently true wait trains itself away.
void NoTransportIsRefusedRatherThanWaitedOn()
{
    CrossingWorld w = GoodWorld();
    w.transportFound = false;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 1)}, Limits());
    CheckAction("no transport refuses", step.action, CrossingAction::Refuse);
    CheckSays("and says no transport serves the far map",
              CrossingExplanation(step, w), "no transport on map 0");
}

// THE FACT THE EARLIER ATTEMPT AT THIS DIED ON. A boat with no stop frame on
// our map gives no berth, and this module will not derive one by offsetting
// something else: that is exactly how a staging point ended up inside rock.
void ABoatWithNoBerthOnThisMapIsRefusedAndNothingIsInvented()
{
    CrossingWorld w = GoodWorld();
    w.berthKnown = false;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 1)}, Limits());
    CheckAction("no berth refuses", step.action, CrossingAction::Refuse);
    CheckSays("and says the berth is what is missing",
              CrossingExplanation(step, w), "no stop frame on map 0");
    CheckSays("and says it will not derive one",
              CrossingExplanation(step, w), "will not derive one");
}

// A path that never stops on the destination map does not land there, whatever
// its two ends claim.
void ABoatThatDoesNotStopAtTheFarEndIsRefused()
{
    CrossingWorld w = GoodWorld();
    w.landingKnown = false;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 1)}, Limits());
    CheckAction("no landing refuses", step.action, CrossingAction::Refuse);
    CheckSays("and says the path does not land there",
              CrossingExplanation(step, w), "does not land where");
}

// THE GUARD THAT MUST NOT BE LOST. A travel destination standing in ground the
// party cannot survive is not a destination, however correct its coordinates.
// The berth is a travel destination like any other and gets the same sweep,
// because the last time a destination was chosen without regard for what
// surrounds it the family took eighteen deaths to one level 65 elite.
void AGuardedBerthIsNotADestination()
{
    CrossingWorld w = GoodWorld();
    w.berthGuarded = true;
    w.berthGuardLevel = 65;
    CrossingStep const step =
        ReadCrossing(w, {At(true, 0), At(false, 1)}, Limits());
    CheckAction("a guarded berth refuses", step.action,
                CrossingAction::Refuse);
    CheckSays("and names the level that guards it",
              CrossingExplanation(step, w), "level 65");
    CheckSays("and gives the reason a destination is refused",
              CrossingExplanation(step, w), "cannot survive");
}

// A member on a third map is worse news than a member at the wrong end of a
// known route, and it is reported in its own words rather than folded into
// the split it is not.
void AMemberOnAThirdMapIsRefusedInItsOwnWords()
{
    std::vector<CrossingMember> members = {At(true, 0), At(false, 1),
                                           At(false, 530)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("a third map refuses", step.action, CrossingAction::Refuse);
    CheckLeg("and names the off-route leg", step.leg, CrossingLeg::OffRoute);
    CheckCount("one member off route", step.offRoute, 1u);
    CheckSays("and says no boat calls there",
              CrossingExplanation(step, GoodWorld()), "neither map 0 nor map 1");
}

// ONLY THE LEADER IS EVER AIMED, so a leader that has already crossed leaves
// nothing to aim. This is a real split and it is said out loud rather than
// worked around by aiming a follower, which is the scatter this repository
// keeps paying for.
void ALeaderAlreadyAshoreLeavesNothingToAim()
{
    std::vector<CrossingMember> members = {At(true, 1), At(false, 0),
                                           At(false, 0)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("a leader ashore with followers behind refuses", step.action,
                CrossingAction::Refuse);
    Check("and the leader is not on the origin map", step.leaderOnOrigin,
          false);
    CheckSays("and says only the leader is ever aimed",
              CrossingExplanation(step, GoodWorld()), "only ever aims the leader");
}

// ------------------------------------------------------------- the crossing

// THE ONE THAT MOVES ANYBODY. The family as it was tonight, minus the logged
// out member: the leader on map 0 with a follower, two followers already on
// map 1 beside the dungeon door, a boat that serves both maps and a clear
// berth. One aim, at the berth, for one character.
void AnAssembledLeaderWithABerthWalksToIt()
{
    std::vector<CrossingMember> members = {At(true, 0, 4200.f),
                                           At(false, 0, 4600.f), At(false, 1),
                                           At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("the leader walks to the berth", step.action,
                CrossingAction::Walk);
    CheckLeg("on the walk-to-berth leg", step.leg, CrossingLeg::WalkToBerth);
    CheckCount("two members already ashore", step.ashore, 2u);
    CheckCount("two still on the origin map", step.waiting, 2u);
    Check("the leader is on the origin map", step.leaderOnOrigin, true);
    Check("and is not at the berth yet", step.leaderAtBerth, false);
    CheckSays("and it says who is already across",
              CrossingExplanation(step, GoodWorld()), "already on map 1");
}

// Arrival is a tolerance, not a boarding decision. Inside it the leader is AT
// the berth, and what happens next is the bot AI's own once-a-second boarding
// poll, not anything this module does.
void ArrivingAtTheBerthIsNotBoarding()
{
    std::vector<CrossingMember> members = {At(true, 0, 4.f), At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    Check("the leader reads as at the berth", step.leaderAtBerth, true);
    CheckAction("and the action is still only to walk", step.action,
                CrossingAction::Walk);
    CheckCount("nobody is aboard", step.aboard, 0u);
}

// THE MOMENT THE MODULE STOPS HAVING AN OPINION. A passenger is carried by the
// transport, which relocates it every tick and teleports it when its path
// changes map. Any order issued now fights that, so the answer is a value that
// does nothing and is distinguishable from every other way of doing nothing.
void AboardMeansTheTransportOwnsItAndNothingIsAimed()
{
    std::vector<CrossingMember> members = {Aboard(true, 0), At(false, 0),
                                           At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("aboard rides", step.action, CrossingAction::Ride);
    CheckLeg("on the aboard leg", step.leg, CrossingLeg::Aboard);
    CheckCount("one aboard", step.aboard, 1u);
    Check("a passenger is not counted as waiting on the origin map",
          step.waiting == 1u, true);
}

// A passenger's map flips under the transport's own teleport rather than under
// anything the party did. Mid-ocean on the far map, still aboard, is still
// Ride: it must not read as an arrival and must not read as a fresh walk.
void APassengerWhoseMapHasFlippedIsStillRiding()
{
    // Every member is on the destination map by map id alone, and one of them
    // is still standing on a deck. If `aboard` were read after the map, this
    // would grade as Done and end the crossing over open water.
    std::vector<CrossingMember> members = {Aboard(true, 1), At(false, 1)};
    CrossingStep const step = ReadCrossing(GoodWorld(), members, Limits());
    CheckAction("still aboard, still riding", step.action,
                CrossingAction::Ride);
    CheckCount("the passenger is not counted ashore", step.ashore, 1u);
    CheckCount("it is counted aboard instead", step.aboard, 1u);
    Check("and the crossing is not over", step.action == CrossingAction::Done,
          false);
}

// Being aboard outranks the refusals, because once somebody is on a deck a
// missing berth is no longer a reason to do anything, and the one thing that
// must never happen is an order that walks a passenger off a moving boat.
void AboardOutranksAMissingBerth()
{
    CrossingWorld w = GoodWorld();
    w.berthKnown = false;
    w.berthGuarded = true;
    CrossingStep const step =
        ReadCrossing(w, {Aboard(true, 0), At(false, 1)}, Limits());
    CheckAction("aboard still rides", step.action, CrossingAction::Ride);
}

// The end. Every member read on the destination map, and it is asked before
// the boat is, so a party that has landed does not care whether a boat could
// still be found for a crossing it no longer needs.
void EverybodyOnTheFarMapIsDone()
{
    std::vector<CrossingMember> members = {At(true, 1), At(false, 1),
                                           At(false, 1), At(false, 1),
                                           At(false, 1)};
    CrossingWorld w = GoodWorld();
    w.transportFound = false;
    w.berthKnown = false;
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("every member ashore is done", step.action,
                CrossingAction::Done);
    CheckLeg("on the ashore leg", step.leg, CrossingLeg::Ashore);
    CheckCount("five ashore", step.ashore, 5u);
}

// ----------------------------------------------------------------- the words

// Every action says something, including the ones that do nothing. "Held" with
// no reason is a line an operator learns to skip; "held because no stop frame
// exists on this map" is one they can act on.
void EveryActionSaysSomething()
{
    CrossingWorld const w = GoodWorld();
    struct Case { std::vector<CrossingMember> members; CrossingWorld world; };
    std::vector<Case> const cases = {
        {{Unread()}, w},
        {{At(true, 0), At(false, 1)}, w},
        {{Aboard(true, 0)}, w},
        {{At(true, 1)}, w},
        {{At(true, 0), At(false, 530)}, w},
    };
    for (Case const& c : cases)
    {
        CrossingStep const step = ReadCrossing(c.world, c.members, Limits());
        std::string const said = CrossingExplanation(step, c.world);
        if (!said.empty())
            continue;
        std::printf("FAIL '%s' said nothing\n", CrossingActionName(step.action));
        ++failures;
    }
}

// Every leg and every action has its own name, so a log line can never say one
// thing while the value says another.
void EveryValueHasItsOwnName()
{
    std::vector<CrossingLeg> const legs = {
        CrossingLeg::Unknown, CrossingLeg::OffRoute, CrossingLeg::WalkToBerth,
        CrossingLeg::Aboard, CrossingLeg::Ashore};
    for (std::size_t i = 0; i < legs.size(); ++i)
        for (std::size_t j = i + 1; j < legs.size(); ++j)
            if (std::string(CrossingLegName(legs[i])) ==
                CrossingLegName(legs[j]))
            {
                std::printf("FAIL two legs share the name '%s'\n",
                            CrossingLegName(legs[i]));
                ++failures;
            }

    std::vector<CrossingAction> const actions = {
        CrossingAction::Wait, CrossingAction::Refuse, CrossingAction::Walk,
        CrossingAction::Ride, CrossingAction::Done};
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

// Being handed a better world never turns a blind reading into progress. The
// unreadable branch is ahead of everything, and this is the test that keeps it
// there when somebody reorders the function.
void MoreFactsNeverRescueABlindReading()
{
    std::vector<CrossingMember> members = {At(true, 0), Unread()};
    CrossingWorld w = GoodWorld();
    CheckAction("blind with a full world", ReadCrossing(w, members, Limits()).action,
                CrossingAction::Wait);
    w.berthGuarded = true;
    CheckAction("blind with a guarded berth", ReadCrossing(w, members, Limits()).action,
                CrossingAction::Wait);
    w.transportFound = false;
    CheckAction("blind with no boat", ReadCrossing(w, members, Limits()).action,
                CrossingAction::Wait);
}

} // namespace

int main()
{
    OneUnreadableMemberOutranksEveryOtherReading();
    FourAshoreAndOneUnreadableIsNotAnArrival();
    AnEmptyRosterIsNotAnArrival();
    ACrossingFromAMapToItselfIsRefused();

    NoTransportIsRefusedRatherThanWaitedOn();
    ABoatWithNoBerthOnThisMapIsRefusedAndNothingIsInvented();
    ABoatThatDoesNotStopAtTheFarEndIsRefused();
    AGuardedBerthIsNotADestination();
    AMemberOnAThirdMapIsRefusedInItsOwnWords();
    ALeaderAlreadyAshoreLeavesNothingToAim();

    AnAssembledLeaderWithABerthWalksToIt();
    ArrivingAtTheBerthIsNotBoarding();
    AboardMeansTheTransportOwnsItAndNothingIsAimed();
    APassengerWhoseMapHasFlippedIsStillRiding();
    AboardOutranksAMissingBerth();
    EverybodyOnTheFarMapIsDone();

    EveryActionSaysSomething();
    EveryValueHasItsOwnName();
    MoreFactsNeverRescueABlindReading();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("the family crosses a continent by boat, or says why it cannot\n");
    return 0;
}
