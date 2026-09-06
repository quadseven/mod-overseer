/*
 * Whether a dungeon run is finished, and what word that puts on its row (#226).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter builds the expected mask out of the same DungeonEncounter
 * list the core credits kills against, and reads the completed mask off the
 * instance save; this test pins what those two numbers mean, and in particular
 * that a map which credits nothing is never read as finished.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonCompletion;
using OverseerDecisions::DungeonRunCompletion;
using OverseerDecisions::DungeonRunExitOutcome;

namespace
{

int failures = 0;

char const* Name(DungeonCompletion c)
{
    switch (c)
    {
        case DungeonCompletion::Unknowable: return "Unknowable";
        case DungeonCompletion::NotYet:     return "NotYet";
        case DungeonCompletion::Complete:   return "Complete";
    }
    return "?";
}

void Check(char const* what, DungeonCompletion got, DungeonCompletion want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got, want);
    ++failures;
}

void AMapThatCreditsNothingIsNeverFinished()
{
    // THE TRAP THIS ENUM EXISTS FOR. Zero bits are all set, so a bool-returning
    // version of this would report a finished dungeon the instant the party
    // walked in, which is exactly why the boss-state route was rejected.
    Check("no expectation, nothing done", DungeonRunCompletion(0, 0),
          DungeonCompletion::Unknowable);
    Check("no expectation, bits set anyway", DungeonRunCompletion(0, 255),
          DungeonCompletion::Unknowable);
}

void EveryCreditedEncounterMeansComplete()
{
    // The measured Wailing Caverns clear: eight encounters, indices 0 to 7, all
    // credited, on a script that never calls SetBossState.
    Check("wailing caverns, all eight", DungeonRunCompletion(255, 255),
          DungeonCompletion::Complete);
    Check("a single encounter", DungeonRunCompletion(1, 1),
          DungeonCompletion::Complete);
    // Non-contiguous indices are ordinary: the bit is `1 << encounterIndex` and
    // nothing promises those are packed from zero.
    Check("sparse indices", DungeonRunCompletion(0b10010001, 0b10010001),
          DungeonCompletion::Complete);
}

void OneEncounterShortIsNotFinished()
{
    Check("seven of eight", DungeonRunCompletion(255, 127), DungeonCompletion::NotYet);
    Check("nothing done yet", DungeonRunCompletion(255, 0), DungeonCompletion::NotYet);
    // The last boss alone missing is the case that matters most: it is what a
    // party looks like when it has cleared everything on the way in.
    Check("only the last one left", DungeonRunCompletion(255, 0b01111111),
          DungeonCompletion::NotYet);
    Check("only the first done", DungeonRunCompletion(0b10010001, 0b00000001),
          DungeonCompletion::NotYet);
}

void BitsTheMapDoesNotCreditAreIgnored()
{
    // The save is written by the core and outlives this module's opinions. A
    // bit from a difficulty this run is not on says nothing about this run, and
    // must not be able to make a run look unfinished OR finished on its own.
    Check("extra bits, expectation met", DungeonRunCompletion(0b0011, 0b1111),
          DungeonCompletion::Complete);
    Check("extra bits, expectation not met", DungeonRunCompletion(0b0011, 0b1101),
          DungeonCompletion::NotYet);
}

void TheOutcomeWordPrefersProofOverInference()
{
    CheckWord("plain walk out", DungeonRunExitOutcome(false, false), "left");
    CheckWord("watchdog gave up", DungeonRunExitOutcome(false, true), "stalled");
    CheckWord("proved finished", DungeonRunExitOutcome(true, false), "complete");
    // Both at once is real: the watchdog can spend its skips on the last pull
    // of a dungeon that then finishes. The mask is a fact, the stall is an
    // inference from not having moved, so the fact wins.
    CheckWord("finished after a stall", DungeonRunExitOutcome(true, true), "complete");
}

void TheNewWordStillCountsAsARun()
{
    // 'complete' must be on the entered side of the campaign counter, or a
    // finished run would stop filling the slot it just earned. This is the
    // property #225's deny-list gives by default, pinned here so that a later
    // edit to that list has to break this test to break the campaign.
    if (!OverseerDecisions::DungeonRunEnteredTheInstance("complete"))
    {
        std::printf("FAIL complete does not count as a run that happened\n");
        ++failures;
    }
    if (OverseerDecisions::DungeonRunTrailingFailures({"complete", "staging_failed"}) != 0u)
    {
        std::printf("FAIL a complete run does not break the failure streak\n");
        ++failures;
    }
}

} // namespace

int main()
{
    AMapThatCreditsNothingIsNeverFinished();
    EveryCreditedEncounterMeansComplete();
    OneEncounterShortIsNotFinished();
    BitsTheMapDoesNotCreditAreIgnored();
    TheOutcomeWordPrefersProofOverInference();
    TheNewWordStillCountsAsARun();
    if (!failures)
        std::printf("a finished dungeon is provable, and an unknowable one is not finished\n");
    return failures ? 1 : 0;
}
