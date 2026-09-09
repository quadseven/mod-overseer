/*
 * Who gets walked back INTO a dungeon whose run is still on (#384).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter takes the census against the ENTRANCE door, so `through`
 * means "inside the instance" and the wrong side of that door is the outdoor
 * map. What is pinned here is the answer the rejoin acts on: which members can
 * be walked to that door right now, which cannot be walked at all, and which
 * are not this door's business.
 *
 * The measurement it exists for: a member took environmental damage inside map
 * 43 - out of combat, 118 yards below the floor, so drowning - and released to a
 * graveyard on the outdoor map about 2200 yards away. The four who stayed inside
 * did not move one yard in 36 minutes, because STAGED_INSIDE holds until the
 * census says every member is through and nothing was ever going to bring the
 * fifth back. Within the hour it had wandered several thousand yards further off
 * and died three more times out there.
 *
 * WHY THIS IS THE SAME FUNCTION THE EVACUATION USES. It was DungeonRunEvacuation
 * and it is DungeonRunWrongSide now, because the reset walking a straggler OUT
 * and a run walking a stranded member back IN are the same question asked of two
 * different doors. The crossing predicates beside it already share one shape for
 * both directions and say why; a second copy is how the second one rots. So this
 * test asks it in the direction the older test does not, over the same three
 * kinds of member.
 *
 * AND THE SECOND HALF: HOW LONG THAT WALK LASTS (#393). Picking who to walk was
 * never the part that failed. The walk was asked for through the run's own
 * escort lease, which is swept the poll after the run stops asking - and the run
 * stops asking by closing `split_failed`, which it does BECAUSE this member is
 * missing. Measured on the realm, two lines eight seconds apart: aimed at the
 * entrance 3712 yards away with a route of 5420 yards of walking legs at
 * 22:44:31, and stripped of `new rpg` "with no escort" at 22:44:39. Half an hour
 * and three deaths later it was 3718 yards out. ReadRejoinWalk is the lease's
 * own lifetime, and what is pinned below is that it outlives the run, ends on
 * arriving rather than on a clock, and is still bounded.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ArrivalReachesTrigger;
using OverseerDecisions::DungeonRunAllThrough;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::DungeonRunWrongSide;
using OverseerDecisions::DungeonWrongSide;
using OverseerDecisions::ReadRejoinWalk;
using OverseerDecisions::RejoinWalk;
using OverseerDecisions::RejoinWalkEnds;
using OverseerDecisions::RejoinWalkFacts;
using OverseerDecisions::RejoinWalkName;

namespace
{

int failures = 0;

// The adapter's TRAVEL_ARRIVED_POSITION_YARDS, and the entrance trigger radii
// this module aims at. Written here rather than imported for the reason the
// sibling tests give: this is about the shape of the rule and must keep meaning
// the same thing if the adapter retunes its number.
constexpr float ARRIVAL_YARDS = 5.f;

void CheckNames(char const* what, std::vector<std::string> const& got,
                std::vector<std::string> const& want)
{
    bool same = got.size() == want.size();
    for (std::size_t i = 0; same && i < got.size(); ++i)
        same = got[i] == want[i];
    if (same)
        return;

    std::printf("FAIL %s: got [", what);
    for (std::size_t i = 0; i < got.size(); ++i)
        std::printf("%s%s", i ? ", " : "", got[i].c_str());
    std::printf("], wanted [");
    for (std::size_t i = 0; i < want.size(); ++i)
        std::printf("%s%s", i ? ", " : "", want[i].c_str());
    std::printf("]\n");
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// A member the census found inside. Against the ENTRANCE door that is the RIGHT
// side, so it carries the negative distance the census leaves on anybody it did
// not measure against the door's own map.
DungeonRunEntryState Inside(char const* name)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.through = true;
    return state;
}

// A member the census found alive on the door's map, `yards` from the door.
DungeonRunEntryState Outside(char const* name, float yards)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.distanceFromDoor = yards;
    return state;
}

// The same, dead: a ghost that has released to a graveyard out there.
DungeonRunEntryState Ghost(char const* name, float yards)
{
    DungeonRunEntryState state = Outside(name, yards);
    state.alive = false;
    return state;
}

// Seen, alive, and on neither the instance map nor the door's map. The census
// leaves the distance at its negative sentinel for exactly this.
DungeonRunEntryState OnAThirdMap(char const* name)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    return state;
}

// Not found in the world this poll: mid-login, mid-teardown, or logged out.
DungeonRunEntryState NotSeen(char const* name)
{
    DungeonRunEntryState state;
    state.name = name;
    return state;
}

void TheMeasuredRunIsOneMemberShort()
{
    // Four inside and the fifth on the outdoor map 2200 yards away, which is
    // the run that sat 'active' for 36 minutes.
    std::vector<DungeonRunEntryState> const census{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"), Inside("Dmember"),
        Outside("Emember", 2200.f)};

    CheckBool("the census is not satisfied", DungeonRunAllThrough(census), false);

    DungeonWrongSide const stranded = DungeonRunWrongSide(census);
    CheckNames("the stranded member is walked", stranded.walk, {"Emember"});
    CheckNames("and nobody is waited for", stranded.wait, {});
}

void AGhostIsNamedRatherThanWalked()
{
    // The first thing that happens after a death outside is not a walk. The
    // member is a ghost, the revival drive owns it, and an aim on a corpse
    // takes it away from the corpse rather than to the door.
    std::vector<DungeonRunEntryState> const census{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"), Inside("Dmember"),
        Ghost("Emember", 2200.f)};

    DungeonWrongSide const stranded = DungeonRunWrongSide(census);
    CheckNames("a ghost is not walked", stranded.walk, {});
    CheckNames("a ghost is named", stranded.wait, {"Emember"});

    // And the moment it is on its feet, the same census walks it. This is the
    // whole of "does the resurrect have to complete before the walk back": it
    // does, and nothing has to sequence it, because the question is asked again
    // on every poll and the answer changes by itself.
    std::vector<DungeonRunEntryState> const risen{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"), Inside("Dmember"),
        Outside("Emember", 2200.f)};
    DungeonWrongSide const after = DungeonRunWrongSide(risen);
    CheckNames("once alive it is walked", after.walk, {"Emember"});
    CheckNames("and no longer waited for", after.wait, {});
}

void TwoStrandedAtOnceAreBothWalked()
{
    // A wipe that half succeeds is two members outside, one of them still a
    // ghost. Both are reported, in the two lists that are acted on differently,
    // rather than one list the caller has to re-sort.
    std::vector<DungeonRunEntryState> const census{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"),
        Outside("Dmember", 400.f), Ghost("Emember", 2200.f)};

    DungeonWrongSide const stranded = DungeonRunWrongSide(census);
    CheckNames("the living one walks", stranded.walk, {"Dmember"});
    CheckNames("the dead one waits", stranded.wait, {"Emember"});
}

void WhatIsNotThisDoorsBusiness()
{
    // A member on a THIRD map cannot be aimed at this door - an `at:` aim is
    // refused for a character that is not on the aim's map - so writing one
    // that could only be refused is not done. It is not silently walked and it
    // is not silently waited for; it is in neither list, and the caller's own
    // blockers line is what names it.
    //
    // A member this poll could not find is out on the same terms: a name that
    // does not resolve is not on the map either.
    std::vector<DungeonRunEntryState> const census{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"),
        OnAThirdMap("Dmember"), NotSeen("Emember")};

    DungeonWrongSide const stranded = DungeonRunWrongSide(census);
    CheckNames("a third map is not walked", stranded.walk, {});
    CheckNames("and not waited for either", stranded.wait, {});

    // The run is still not assembled, which is what keeps the ceiling in play:
    // nothing here can reach those two, so the phase's own backstop is what
    // ends the run rather than a walk that would never start.
    CheckBool("the census is still not satisfied", DungeonRunAllThrough(census),
              false);
}

void AWholePartyInsideAsksForNothing()
{
    // The rejoin returns doing nothing when there is nobody outside, which is
    // every poll of a healthy run. Asserted rather than assumed, because it is
    // the case that runs thousands of times more often than the others.
    std::vector<DungeonRunEntryState> const census{
        Inside("Aleader"), Inside("Bmember"), Inside("Cmember"), Inside("Dmember"),
        Inside("Emember")};

    DungeonWrongSide const stranded = DungeonRunWrongSide(census);
    CheckNames("nobody to walk", stranded.walk, {});
    CheckNames("nobody to wait for", stranded.wait, {});
    CheckBool("and the census is satisfied", DungeonRunAllThrough(census), true);

    // An empty roster is not a party standing in a dungeon. The crossing
    // predicate fails closed on it and this returns nothing, so a rejoin can
    // never be started for a run with no members.
    DungeonWrongSide const none = DungeonRunWrongSide({});
    CheckNames("an empty roster walks nobody", none.walk, {});
    CheckNames("and waits for nobody", none.wait, {});
    CheckBool("and is not all through", DungeonRunAllThrough({}), false);
}

void TheDoorHasToBeReachableBeforeAnybodyIsAimedAtIt()
{
    // The rejoin refuses to aim at a door a walk cannot open, on exactly the
    // terms the evacuation already refuses. Arriving "successfully" outside the
    // thing you were sent to is worse than not arriving: the errand completes
    // and the character walks away from the door it was touching.
    //
    // The entrance triggers this module aims at have radii of 7 and 6 against
    // an arrival tolerance of 5, so both are reachable; a radius the tolerance
    // does not fit inside is not.
    CheckBool("a radius 7 entrance is reachable",
              ArrivalReachesTrigger(ARRIVAL_YARDS, 7.f), true);
    CheckBool("a radius 6 entrance is reachable",
              ArrivalReachesTrigger(ARRIVAL_YARDS, 6.f), true);
    CheckBool("a radius 5 door is not, at exactly the tolerance",
              ArrivalReachesTrigger(ARRIVAL_YARDS, 5.f), false);
    CheckBool("and a box trigger has no radius to reach",
              ArrivalReachesTrigger(ARRIVAL_YARDS, 0.f), false);
}

// The lease's own clock, in the adapter's terms: DUNGEON_REJOIN_BACKSTOP_SECONDS
// borrows TRAVEL_BACKSTOP_SECONDS, which is twenty minutes. Written here rather
// than imported for the reason the arrival tolerance above is: this is about the
// shape of the rule, and it must keep meaning the same thing if the adapter
// retunes its number.
constexpr std::int64_t BACKSTOP_SECONDS = 20 * 60;

void CheckVerdict(char const* what, RejoinWalk got, RejoinWalk want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RejoinWalkName(got),
                RejoinWalkName(want));
    ++failures;
}

// A member still on the wrong side of the door, walking, `walkingFor` seconds
// into its lease.
RejoinWalkFacts StillWalking(std::int64_t walkingFor)
{
    RejoinWalkFacts facts;
    facts.seen = true;
    facts.withTheFamily = false;
    facts.walkingForSeconds = walkingFor;
    facts.backstopSeconds = BACKSTOP_SECONDS;
    return facts;
}

void TheWalkOutlivesTheRunThatAskedForIt()
{
    // THE MEASURED DEFECT, AS THIS RULE SEES IT. Eight seconds after the aim was
    // issued the run had closed and the old lease was two polls from being
    // swept. Nothing about the member had changed: it was in the world, it was
    // still on the wrong map, and it had 5420 yards of walking legs ahead of it.
    // The only correct answer at eight seconds is to keep walking, and the run
    // being over is not one of the inputs.
    CheckVerdict("eight seconds in, the walk keeps going",
                 ReadRejoinWalk(StillWalking(8)), RejoinWalk::Keep);
    CheckBool("and eight seconds in is not an ending",
              RejoinWalkEnds(ReadRejoinWalk(StillWalking(8))), false);

    // AND IT IS STILL GOING WHEN THE WALK ITSELF WOULD FINISH. 5420 yards of
    // legs at the core's own run speed of 7 yards a second is about thirteen
    // minutes, which is the number the backstop has to clear for this fix to be
    // worth shipping at all: a clock that expires mid-journey would strand the
    // member exactly where the old lease did, only later.
    CheckVerdict("and at thirteen minutes, which is the walk's own length",
                 ReadRejoinWalk(StillWalking(13 * 60)), RejoinWalk::Keep);

    // Half an hour is what was actually observed, and by then the clock has
    // stopped this rather than let it run forever.
    CheckVerdict("but half an hour is past the clock",
                 ReadRejoinWalk(StillWalking(30 * 60)), RejoinWalk::GaveUp);
}

void ArrivingEndsItWhateverTheClockSays()
{
    // The walk exists to end a split, so ending the split ends the walk - and it
    // is asked BEFORE the clock, because a walk that took longer than its
    // backstop and still got there worked. Reporting that as a give-up would put
    // a line in the log saying a walk failed on the poll it succeeded.
    RejoinWalkFacts arrived = StillWalking(30 * 60);
    arrived.withTheFamily = true;
    CheckVerdict("back with the family outranks the clock",
                 ReadRejoinWalk(arrived), RejoinWalk::BackWithTheFamily);
    CheckBool("and it is an ending", RejoinWalkEnds(ReadRejoinWalk(arrived)), true);

    RejoinWalkFacts early = StillWalking(8);
    early.withTheFamily = true;
    CheckVerdict("and it does not need the clock's permission",
                 ReadRejoinWalk(early), RejoinWalk::BackWithTheFamily);
}

void ALeaseWithNobodyToHoldItEnds()
{
    // Not in the world this poll: logged out, mid-teardown, or the worldserver
    // bounced. Answered first, ahead of every other reading, because a name that
    // does not resolve has no map to compare and no strategy to hand back.
    RejoinWalkFacts gone;
    gone.seen = false;
    gone.withTheFamily = true;
    gone.walkingForSeconds = 8;
    gone.backstopSeconds = BACKSTOP_SECONDS;
    CheckVerdict("a member nobody can see ends the lease",
                 ReadRejoinWalk(gone), RejoinWalk::Gone);
    CheckBool("and that is an ending", RejoinWalkEnds(ReadRejoinWalk(gone)), true);

    // The default-constructed facts are the same answer, which is the safe
    // reading of "nothing is known" for a decision that hands a lease back.
    CheckVerdict("and so does knowing nothing at all",
                 ReadRejoinWalk(RejoinWalkFacts()), RejoinWalk::Gone);
}

void TheClockIsAClockAndNotAnAccident()
{
    // Exactly at the backstop is over: the comparison is >=, so a lease can
    // never sit one second short of its own limit forever on a clock that only
    // ticks when a poll happens to land.
    CheckVerdict("one second under the clock keeps walking",
                 ReadRejoinWalk(StillWalking(BACKSTOP_SECONDS - 1)), RejoinWalk::Keep);
    CheckVerdict("exactly on it gives up",
                 ReadRejoinWalk(StillWalking(BACKSTOP_SECONDS)), RejoinWalk::GaveUp);

    // A backstop of zero is not a clock, and a walk is not ended by one. This is
    // the failure mode a defaulted duration always has: read as a deadline, a
    // zero would end every walk on its first poll, which is the old defect with
    // a different cause.
    RejoinWalkFacts noClock = StillWalking(30 * 60);
    noClock.backstopSeconds = 0;
    CheckVerdict("a backstop of zero never gives up",
                 ReadRejoinWalk(noClock), RejoinWalk::Keep);

    RejoinWalkFacts negative = StillWalking(30 * 60);
    negative.backstopSeconds = -1;
    CheckVerdict("and neither does a negative one",
                 ReadRejoinWalk(negative), RejoinWalk::Keep);
}

} // namespace

int main()
{
    TheMeasuredRunIsOneMemberShort();
    AGhostIsNamedRatherThanWalked();
    TwoStrandedAtOnceAreBothWalked();
    WhatIsNotThisDoorsBusiness();
    AWholePartyInsideAsksForNothing();
    TheDoorHasToBeReachableBeforeAnybodyIsAimedAtIt();
    TheWalkOutlivesTheRunThatAskedForIt();
    ArrivingEndsItWhateverTheClockSays();
    ALeaseWithNobodyToHoldItEnds();
    TheClockIsAClockAndNotAnAccident();
    return failures ? 1 : 0;
}
