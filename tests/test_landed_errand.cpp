/*
 * What the travel drive does with an aim it has arrived at and could not clear
 * (#558).
 *
 * The live failure this pins. The pass outside the worldserver writes
 * `travel_npc = 'vendor'` and is the only thing that clears it. The travel
 * drive reached the vendor, took the counter hold, and released - which skips
 * the column write for an aim this book never claimed, correctly, but also
 * erases the book's memory of the errand. The next poll met the same standing
 * aim as a NEW errand, and the new-errand branch took the counter hold down.
 * Measured on the dev realm every five seconds for the Alliance leader:
 *
 *     travelling to 'vendor'
 *     at the 'vendor' ... held there for up to 300s
 *     reached 'vendor' (creature 2084) - errand done, releasing
 *     travel release ... skipped the column write
 *     released from its trade hold - it has been sent somewhere else
 *
 * and no sell row issued for over an hour while the family's bags stayed full.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::LandedErrand;
using OverseerDecisions::LandedErrandStep;

namespace
{

int failures = 0;

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

// THE DEFECT ITSELF. Arrived at the vendor, the pass that wrote the aim has not
// cleared it yet, and the counter hold is up. Walking it again is what took the
// hold down every five seconds.
void ALandedCounterAimStandsDownWhileHeld()
{
    char const* const aims[] = {"vendor", "banker", "repair", "auctioneer"};
    for (char const* aim : aims)
        Check(aim, LandedErrandStep(aim, aim, true), LandedErrand::StandDown);
}

// THE BOUND. Once the hold is gone the aim is an ordinary errand again, so an
// aim nobody ever clears costs one hold window per cycle and cannot latch the
// character in place.
void ALandedAimResumesWhenTheHoldIsGone()
{
    Check("vendor, hold gone", LandedErrandStep("vendor", "vendor", false),
          LandedErrand::Resume);
}

// THE WRITER MOVED ON. A new aim is somewhere else to be, and the new-errand
// branch is right to release the hold for it.
void ARewrittenAimResumes()
{
    Check("vendor -> banker", LandedErrandStep("vendor", "banker", true),
          LandedErrand::Resume);
    Check("vendor -> at:", LandedErrandStep("vendor", "at:0:1,2,3", true),
          LandedErrand::Resume);
}

// NOTHING REMEMBERED IS NOTHING CHANGED. Every errand that never landed
// uncleared reads exactly as it did before this existed.
void NothingLandedIsAnOrdinaryErrand()
{
    Check("fresh vendor", LandedErrandStep("", "vendor", false),
          LandedErrand::NotLanded);
    Check("fresh vendor with a hold", LandedErrandStep("", "vendor", true),
          LandedErrand::NotLanded);
}

}  // namespace

int main()
{
    ALandedCounterAimStandsDownWhileHeld();
    ALandedAimResumesWhenTheHoldIsGone();
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
