/*
 * A long point walk (a dungeon door, thousands of yards off) is not broken up
 * by a town trip for full bags, and a target that has been broken off its
 * bound of times is let go of instead of being walked again (a recurring
 * wedge: the leader is sent to a far door, a bags vendor trip takes the
 * column, the trip ends, the door walk starts over, and the family stalls).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Expect(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

std::string const kDoor = "at:0:1000.0,2000.0,30.0";

DoorWalkFacts Walk(float yards, int64_t deferred = 0, uint32_t restarts = 0)
{
    DoorWalkFacts f;
    f.aim = kDoor;
    f.yardsToAim = yards;
    f.deferredFor = deferred;
    f.restarts = restarts;
    return f;
}

// THE DEFECT: 2104 yards out, the bags vendor trip comes up. It waits.
void ABagsTripWaitsForALongDoorWalk()
{
    Expect("2104 yards from the door defers the vendor trip",
           DoorWalkVersusTownTrip(Walk(2104.f)) == DoorWalkVsErrand::Defer);
    Expect("the walk keeps deferring minute after minute",
           DoorWalkVersusTownTrip(Walk(900.f, 20 * 60)) == DoorWalkVsErrand::Defer);
}

// ...and once the door is reached (a short walk left), the trip is asked as before.
void TheTripIsAskedOnceTheDoorIsReached()
{
    Expect("20 yards from the door is not a long walk",
           DoorWalkVersusTownTrip(Walk(20.f)) == DoorWalkVsErrand::NoWalk);
}

void OnlyALongPointWalkOnTheSameMapDefers()
{
    DoorWalkFacts keyword = Walk(2000.f);
    keyword.aim = "vendor";
    Expect("a counter keyword is no walk to protect",
           DoorWalkVersusTownTrip(keyword) == DoorWalkVsErrand::NoWalk);
    DoorWalkFacts empty = Walk(2000.f);
    empty.aim.clear();
    Expect("an empty column is no walk",
           DoorWalkVersusTownTrip(empty) == DoorWalkVsErrand::NoWalk);
    DoorWalkFacts other = Walk(2000.f);
    other.sameMap = false;
    Expect("a point on another map is not deferred for",
           DoorWalkVersusTownTrip(other) == DoorWalkVsErrand::NoWalk);
}

// A deferral is bounded: past the ceiling the trip goes ahead.
void TheDeferralHasACeiling()
{
    int64_t const ceiling = DOOR_WALK_LIMITS.deferCeilingSeconds;
    Expect("just under the ceiling still defers",
           DoorWalkVersusTownTrip(Walk(1500.f, ceiling - 1)) == DoorWalkVsErrand::Defer);
    Expect("at the ceiling the trip preempts",
           DoorWalkVersusTownTrip(Walk(1500.f, ceiling)) == DoorWalkVsErrand::Preempt);
}

// THE REPRODUCTION: the door walk interrupted by a bags errand, then restarted,
// again and again. The ledger counts each interruption of the one target and
// the third gives the target up.
void TheDoorWalkInterruptedThenRestartedIsBounded()
{
    DoorWalkRestarts ledger;
    int64_t now = 100000;
    int64_t const ceiling = DOOR_WALK_LIMITS.deferCeilingSeconds;
    uint32_t rounds = 0;
    DoorWalkVsErrand verdict = DoorWalkVsErrand::NoWalk;
    for (; rounds < 10; ++rounds)
    {
        verdict = DoorWalkVersusTownTrip(
            Walk(2104.f, ceiling, DoorWalkRestartCount(ledger, kDoor, now)));
        if (verdict != DoorWalkVsErrand::Preempt)
            break;
        NoteDoorWalkPreempted(ledger, kDoor, now);
        now += 10 * 60;
    }
    Expect("the walk is let go of, not looped for ever", verdict == DoorWalkVsErrand::GiveUp);
    Expect("after exactly the bound of interruptions",
           rounds == DOOR_WALK_LIMITS.restartBound);
    Expect("a walk with fewer restarts is still deferred for",
           DoorWalkVersusTownTrip(Walk(2104.f, 0, DOOR_WALK_LIMITS.restartBound - 1)) ==
               DoorWalkVsErrand::Defer);
}

void TheRestartBoundIsPerTarget()
{
    DoorWalkRestarts ledger;
    NoteDoorWalkPreempted(ledger, kDoor, 1000);
    NoteDoorWalkPreempted(ledger, kDoor, 2000);
    Expect("two interruptions of one door", DoorWalkRestartCount(ledger, kDoor, 2100) == 2);
    Expect("another target has none",
           DoorWalkRestartCount(ledger, "at:0:3000.0,4000.0,50.0", 2100) == 0);
    Expect("noting another target starts it from one",
           NoteDoorWalkPreempted(ledger, "at:0:3000.0,4000.0,50.0", 2200) == 1);
    Expect("and the old door's count is gone", DoorWalkRestartCount(ledger, kDoor, 2300) == 0);
}

void AQuietSpanForgetsTheCount()
{
    DoorWalkRestarts ledger;
    NoteDoorWalkPreempted(ledger, kDoor, 1000);
    NoteDoorWalkPreempted(ledger, kDoor, 1100);
    Expect("past the window the count is zero",
           DoorWalkRestartCount(ledger, kDoor, 1100 + DOOR_WALK_RESTART_WINDOW_SECONDS + 1) == 0);
    Expect("and a new interruption is the first again",
           NoteDoorWalkPreempted(ledger, kDoor,
                                 1100 + DOOR_WALK_RESTART_WINDOW_SECONDS + 1) == 1);
}

// A keyword or near column that later becomes a far point gets a fresh
// deferral; an old clock must not make the first trip preempt at once.
void ADeferralClockStartsOnlyForAFarPoint()
{
    std::string aim;
    int64_t since = 0;
    DoorWalkFacts keyword;
    keyword.aim = "vendor";
    Expect("a keyword column has no deferral", DoorWalkDeferredFor(aim, since, keyword, 100) == 0);
    Expect("and leaves no clock behind", aim.empty() && since == 0);

    DoorWalkFacts nearPoint = Walk(20.f);
    Expect("a near point has no deferral", DoorWalkDeferredFor(aim, since, nearPoint, 200) == 0);
    Expect("and leaves no clock behind either", aim.empty() && since == 0);

    // Long after, the same column is a far point: the clock starts now.
    DoorWalkFacts far = Walk(2104.f);
    int64_t const later = 200 + DOOR_WALK_LIMITS.deferCeilingSeconds + 500;
    far.deferredFor = DoorWalkDeferredFor(aim, since, far, later);
    Expect("a far point starts a fresh deferral", far.deferredFor == 0);
    Expect("so the first trip defers rather than preempting",
           DoorWalkVersusTownTrip(far) == DoorWalkVsErrand::Defer);
    Expect("the clock then runs", DoorWalkDeferredFor(aim, since, far, later + 60) == 60);

    // Walking close again resets it.
    DoorWalkDeferredFor(aim, since, nearPoint, later + 120);
    Expect("a near point clears a running clock", aim.empty() && since == 0);
}

}  // namespace

int main()
{
    ABagsTripWaitsForALongDoorWalk();
    TheTripIsAskedOnceTheDoorIsReached();
    OnlyALongPointWalkOnTheSameMapDefers();
    TheDeferralHasACeiling();
    TheDoorWalkInterruptedThenRestartedIsBounded();
    TheRestartBoundIsPerTarget();
    AQuietSpanForgetsTheCount();
    ADeferralClockStartsOnlyForAFarPoint();
    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
