/*
 * How long a busy party may hold the clearing watchdog's clock (#382).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter measures who is fighting, looting, sitting, dead or waiting
 * on a resurrect, and stamps when the run last credited a boss or covered
 * thirty yards; this test pins what the age of that progress is allowed to mean
 * while somebody looks busy.
 *
 * The measurement it exists for: a run sat 'active' for 152 minutes with one
 * member flagged in combat and four idle at full health, positions identical to
 * the yard over 95 of them. The hold was correct about what busy MEANS and had
 * no opinion at all about how long it may mean it, so the skips and the
 * extraction underneath it could never be reached.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonClearBusyStillHolds;

namespace
{

int failures = 0;

// A quarter of an hour, which is what the adapter passes. Written here as
// seconds rather than imported, because this test is about the shape of the
// rule and must keep meaning the same thing if the adapter retunes its number.
constexpr time_t CEILING = 15 * 60;

// An arbitrary epoch far from zero, so that an `advancedAt` of 0 is
// distinguished by being the sentinel it is rather than by being early.
constexpr time_t T0 = 1000000;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "holds" : "expired",
                want ? "holds" : "expired");
    ++failures;
}

void NobodyBusyIsNoHold()
{
    // The answer that does not depend on a clock. With nothing holding the
    // patience down, the caller's ordinary ratchet is already the right judge.
    Check("nobody busy", DungeonClearBusyStillHolds(false, 0, T0, CEILING), false);
    Check("nobody busy, run advanced a moment ago",
          DungeonClearBusyStillHolds(false, T0, T0 + 5, CEILING), false);
    Check("nobody busy, run still for hours",
          DungeonClearBusyStillHolds(false, T0, T0 + 10 * CEILING, CEILING), false);
}

void ABusyPartyInsideAMovingRunIsBelieved()
{
    // The whole point of the hold: a boss fight, a wipe, five corpse runs and
    // drinking back to full all sit inside this window and none of them may be
    // counted against the run.
    Check("first poll, nothing stamped yet",
          DungeonClearBusyStillHolds(true, 0, T0, CEILING), true);
    Check("advanced one poll ago", DungeonClearBusyStillHolds(true, T0, T0 + 5, CEILING),
          true);
    Check("advanced halfway through the window",
          DungeonClearBusyStillHolds(true, T0, T0 + CEILING / 2, CEILING), true);
    // Strictly greater, matching Ratchet's own patience test, so the two clocks
    // in this module cannot disagree by one poll about "past the bound".
    Check("advanced exactly a ceiling ago",
          DungeonClearBusyStillHolds(true, T0, T0 + CEILING, CEILING), true);
}

void TheMeasuredStallIsCaught()
{
    // One second past is enough; the run that prompted this was 152 minutes
    // past, and the point is that both are the same answer.
    Check("one second past",
          DungeonClearBusyStillHolds(true, T0, T0 + CEILING + 1, CEILING), false);
    Check("the 152 minute run",
          DungeonClearBusyStillHolds(true, T0, T0 + 152 * 60, CEILING), false);
}

void AFlickeringFlagCannotResetTheCeiling()
{
    // THE REASON THIS TAKES `advancedAt` AND NOT "when the busy stretch began".
    // A member whose combat flag drops for a poll and comes straight back is a
    // plausible shape for the exact fault this bounds, and a clock keyed on that
    // flag would restart every time it blinked - which is the same 152 minutes
    // with more code in front of it.
    //
    // This function cannot be told how long anybody has been busy, only how long
    // the RUN has been still, so a flicker changes nothing: both calls below
    // describe a party that has just this second started looking busy again, and
    // both are judged on a run that has not moved in an hour.
    Check("flag blinked off and back on, run still stuck",
          DungeonClearBusyStillHolds(true, T0, T0 + 60 * 60, CEILING), false);
    Check("and again a poll later",
          DungeonClearBusyStillHolds(true, T0, T0 + 60 * 60 + 5, CEILING), false);

    // The only thing that restores the hold is the run actually advancing, which
    // is the caller re-stamping `advancedAt`.
    Check("until the run advances again",
          DungeonClearBusyStillHolds(true, T0 + 60 * 60, T0 + 60 * 60 + 5, CEILING), true);
}

void TheEdgesThatWouldOtherwiseLiveInSomebodysHead()
{
    // A ceiling of nothing is a bound, not a request for no bound - the same
    // reading a maximumSkips of zero already has in the sibling decision.
    Check("zero ceiling, busy", DungeonClearBusyStillHolds(true, T0, T0 + 1, 0), false);
    Check("zero ceiling, nothing stamped yet",
          DungeonClearBusyStillHolds(true, 0, T0, 0), false);

    // A clock stepped backwards under a running worldserver must cost a wait,
    // never a run: the hold stays believed rather than expiring on a negative.
    Check("clock went backwards",
          DungeonClearBusyStillHolds(true, T0, T0 - 60 * 60, CEILING), true);
}

} // namespace

int main()
{
    NobodyBusyIsNoHold();
    ABusyPartyInsideAMovingRunIsBelieved();
    TheMeasuredStallIsCaught();
    AFlickeringFlagCannotResetTheCeiling();
    TheEdgesThatWouldOtherwiseLiveInSomebodysHead();
    return failures ? 1 : 0;
}
