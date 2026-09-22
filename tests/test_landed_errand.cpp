/*
 * What the travel drive does with an aim it let go of and could not clear
 * (#558).
 *
 * The live failure this pins. The pass outside the worldserver writes
 * `travel_npc` and is the only thing that clears it. The travel drive released
 * the errand - which skips the column write for an aim this book never claimed,
 * correctly, but also erases the book's memory of the errand - and the next
 * poll met the same standing aim as a NEW errand. Measured on the dev realm for
 * the Alliance leader:
 *
 *   at a vendor, every five seconds
 *     at the 'vendor' ... held there for up to 300s
 *     reached 'vendor' (creature 2084) - errand done, releasing
 *     travel release ... skipped the column write
 *     released from its trade hold - it has been sent somewhere else
 *   and no sell row issued for over an hour while the family's bags stayed full
 *
 *   on an `at:` aim, nine times in ten minutes
 *     travel release ... skipped the column write - errand 'at:1:...'
 *   each followed by the same walk into the same mountainside
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using OverseerDecisions::LandedErrand;
using OverseerDecisions::LandedErrandStep;

namespace
{

int failures = 0;

int64_t const CEILING = 15 * 60;

char const* Name(LandedErrand step)
{
    switch (step)
    {
        case LandedErrand::NotLanded: return "NotLanded";
        case LandedErrand::StandDown: return "StandDown";
        case LandedErrand::Resume:    return "Resume";
    }
    return "?";
}

void Check(char const* what, LandedErrand got, LandedErrand want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

// THE VENDOR HALF OF THE DEFECT. Arrived, hold up, aim not yet cleared by its
// writer. Walking it again is what took the hold down every five seconds.
void ACounterLandingStandsDownWhileHeld()
{
    char const* const aims[] = {"vendor", "banker", "repair", "auctioneer"};
    for (char const* aim : aims)
        Check(aim, LandedErrandStep(aim, aim, true, true, 5, CEILING),
              LandedErrand::StandDown);
}

// THE HOLD IS THE VISIT. Once it is gone the aim is an ordinary errand again at
// once, not after the ceiling, so a character that drifted off the counter with
// sales still outstanding is walked back to it.
void ACounterLandingResumesWhenTheHoldIsGone()
{
    Check("vendor, hold gone", LandedErrandStep("vendor", "vendor", true, false, 5, CEILING),
          LandedErrand::Resume);
}

// THE OTHER HALF OF THE DEFECT, AND WHY IT IS NOT ABOUT VENDORS. The `at:` aim
// measured being walked into a mountainside nine times in ten minutes, and an
// arrival at a creature that is not a counter: both are let go of, neither
// carries a hold, and neither may be walked again on the next poll.
void ANonCounterLandingStandsDownUntilTheCeiling()
{
    char const* const aims[] = {"at:1:6781.4,-4661.2,723.9", "profession trainer",
                                "flight master:12", "16227"};
    for (char const* aim : aims)
    {
        Check(aim, LandedErrandStep(aim, aim, false, false, 5, CEILING),
              LandedErrand::StandDown);
        Check(aim, LandedErrandStep(aim, aim, false, false, CEILING - 1, CEILING),
              LandedErrand::StandDown);
    }
}

// THE BOUND. An aim nobody ever clears costs one walk per ceiling, and cannot
// latch a character in place.
void ANonCounterLandingResumesAtTheCeiling()
{
    Check("at:, at ceiling",
          LandedErrandStep("at:0:1,2,3", "at:0:1,2,3", false, false, CEILING, CEILING),
          LandedErrand::Resume);
}

// THE WRITER MOVED ON. A new aim is somewhere else to be, whatever the last
// one was and however recently it was let go of.
void ARewrittenAimResumes()
{
    Check("vendor -> banker, held",
          LandedErrandStep("vendor", "banker", true, true, 5, CEILING),
          LandedErrand::Resume);
    Check("at: -> at:",
          LandedErrandStep("at:0:1,2,3", "at:0:4,5,6", false, false, 5, CEILING),
          LandedErrand::Resume);
}

// NOTHING REMEMBERED IS NOTHING CHANGED. Every errand that was never let go of
// uncleared reads exactly as it did before this existed.
void NothingLandedIsAnOrdinaryErrand()
{
    Check("fresh vendor", LandedErrandStep("", "vendor", false, false, 0, CEILING),
          LandedErrand::NotLanded);
    Check("fresh vendor with a hold", LandedErrandStep("", "vendor", true, true, 0, CEILING),
          LandedErrand::NotLanded);
    Check("fresh at:", LandedErrandStep("", "at:0:1,2,3", false, false, 0, CEILING),
          LandedErrand::NotLanded);
}

}  // namespace

int main()
{
    ACounterLandingStandsDownWhileHeld();
    ACounterLandingResumesWhenTheHoldIsGone();
    ANonCounterLandingStandsDownUntilTheCeiling();
    ANonCounterLandingResumesAtTheCeiling();
    ARewrittenAimResumes();
    NothingLandedIsAnOrdinaryErrand();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("landed errand: all passed\n");
    return EXIT_SUCCESS;
}
