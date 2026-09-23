/*
 * A catch-up walk past the flight threshold flies or holds, and never walks.
 *
 * Measured 2026-09-23 on the dev realm. The leader stood in Silithus near
 * Cenarion Hold and four followers were in Winterspring. 'Ugga' was sent to
 * 'at:1:-6792.72,515.752,0.43336', 11,505 yards off, and routed round 41
 * surveyed nodes and 35,358 yards of walking legs. 'Og' died twice to
 * Hederine elites on that ground. The catch-up walk has a line where it
 * starts (FOLLOW_CATCH_UP_YARDS) and none where it stops being a walk.
 *
 * The line is the flight threshold (TRAVEL_FLIGHT_MIN_YARDS, 1500 yards): the
 * distance at which this module already calls a trip a journey worth a flight.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::FarCatchUpStaysHeld;
using OverseerDecisions::FarCatchUpStep;
using OverseerDecisions::FarCatchUpStepWord;
using OverseerDecisions::FarCatchUpWalk;

namespace
{

int failures = 0;

constexpr float LIMIT = 1500.f;
constexpr time_t RETRY = 15 * 60;

void Step(char const* what, FarCatchUpStep got, FarCatchUpStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, FarCatchUpStepWord(got),
                FarCatchUpStepWord(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// THE MEASUREMENT. Flight decided, nothing flew, 11,505 yards: held.
void UggaIsHeldRatherThanWalkedAcrossKalimdor()
{
    Step("11505 yards, flight decided and refused",
         FarCatchUpWalk(11505.f, LIMIT, false, true, true), FarCatchUpStep::Hold);
    Step("one yard past the line", FarCatchUpWalk(LIMIT + 1.f, LIMIT, false, true, true),
         FarCatchUpStep::Hold);
}

// Inside the line nothing changes: the catch-up walks exactly as before.
void InsideTheLineTheWalkIsUnchanged()
{
    Step("at the line", FarCatchUpWalk(LIMIT, LIMIT, false, true, true),
         FarCatchUpStep::Walk);
    Step("a 600 yard catch-up", FarCatchUpWalk(600.f, LIMIT, false, false, false),
         FarCatchUpStep::Walk);
}

// A flight carried it: the rest is the walk from the landing.
void AFlightThatLandedLeavesAWalk()
{
    Step("2000 yards from the landing", FarCatchUpWalk(2000.f, LIMIT, true, true, true),
         FarCatchUpStep::Walk);
}

// ConsiderFlight refuses anyone not carrying the mover and leaves the decision
// unspent. The first poll of every catch-up is that poll, so it is issued to
// take the mover, and the flight is decided on the next one.
void TheFirstPollTakesTheMoverSoTheFlightCanBeDecided()
{
    Step("not carrying the mover yet", FarCatchUpWalk(11505.f, LIMIT, false, false, false),
         FarCatchUpStep::Grant);
    Step("carrying it, but the flight was not decided (a fight)",
         FarCatchUpWalk(11505.f, LIMIT, false, false, true), FarCatchUpStep::Wait);
}

void AnUnsetLimitHolds()
{
    Step("a zero limit", FarCatchUpWalk(40.f, 0.f, false, true, true), FarCatchUpStep::Hold);
}

void TheHoldEndsWhenTheLeaderComesBackOrTheRetryIsDue()
{
    Check("still 11505 yards off, one minute in", FarCatchUpStaysHeld(11505.f, LIMIT, 60, RETRY),
          true);
    Check("the leader walked back within the line", FarCatchUpStaysHeld(1200.f, LIMIT, 60, RETRY),
          false);
    Check("fifteen minutes on, the flight is asked again",
          FarCatchUpStaysHeld(11505.f, LIMIT, RETRY, RETRY), false);
    Check("a gap nobody measured (another map) stays held",
          FarCatchUpStaysHeld(-1.f, LIMIT, 60, RETRY), true);
}

void EveryStepHasAWord()
{
    for (int i = 0; i < 16; ++i)
    {
        FarCatchUpStep const step = FarCatchUpWalk((i & 1) ? 11505.f : 40.f, LIMIT,
                                                   (i & 2) != 0, (i & 4) != 0, (i & 8) != 0);
        if (std::string(FarCatchUpStepWord(step)) == "unknown")
        {
            std::printf("FAIL a step with no word\n");
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    UggaIsHeldRatherThanWalkedAcrossKalimdor();
    InsideTheLineTheWalkIsUnchanged();
    AFlightThatLandedLeavesAWalk();
    TheFirstPollTakesTheMoverSoTheFlightCanBeDecided();
    AnUnsetLimitHolds();
    TheHoldEndsWhenTheLeaderComesBackOrTheRetryIsDue();
    EveryStepHasAWord();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a catch-up past the flight threshold flies or holds, and never walks\n");
    return EXIT_SUCCESS;
}
