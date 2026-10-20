/*
 * Where an adopted dungeon run starts, without AzerothCore (#165).
 *
 * A member found inside an instance is adopted as a run. Before this, the only
 * question asked was whether the whole roster was inside: yes started CLEARING,
 * no started STAGED_INSIDE and walked everybody outside back to the door. That
 * is right for an early entrant into a fresh instance and wrong for a member
 * left behind in a CLEARED one. Measured 2026-10-20: a Blackfathom run credited
 * every encounter (mask 239 of 239), the walk out and the hearth both failed for
 * one member, and every adoption after that walked the other four back at a
 * door into an instance with nothing left in it, closed 'split_failed' after 15
 * minutes, and adopted it again.
 *
 * So the instance's own completion is asked first. A cleared instance is left
 * through EXIT whoever is inside; anything else keeps the old answer.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonCompletion;
using OverseerDecisions::DungeonRunAdoptAt;
using OverseerDecisions::DungeonRunAdoptPhase;

namespace
{

int failures = 0;

char const* Name(DungeonRunAdoptPhase phase)
{
    switch (phase)
    {
        case DungeonRunAdoptPhase::StagedInside: return "STAGED_INSIDE";
        case DungeonRunAdoptPhase::Clearing:     return "CLEARING";
        case DungeonRunAdoptPhase::Exiting:      return "EXIT";
    }
    return "?";
}

void Check(char const* what, DungeonRunAdoptPhase got, DungeonRunAdoptPhase want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void AClearedInstanceIsLeftNotStagedInto()
{
    // The live case: one member inside, four outside, every encounter credited.
    Check("one member left inside a cleared instance",
          DungeonRunAdoptAt(false, DungeonCompletion::Complete),
          DungeonRunAdoptPhase::Exiting);
    // And the whole party inside a cleared one goes the same way, rather than
    // through a CLEARING that has nothing to clear.
    Check("everybody inside a cleared instance",
          DungeonRunAdoptAt(true, DungeonCompletion::Complete),
          DungeonRunAdoptPhase::Exiting);
}

void AnInstanceWithBossesLeftKeepsTheOldAnswer()
{
    Check("an early entrant into a fresh instance",
          DungeonRunAdoptAt(false, DungeonCompletion::NotYet),
          DungeonRunAdoptPhase::StagedInside);
    Check("everybody inside a fresh instance",
          DungeonRunAdoptAt(true, DungeonCompletion::NotYet),
          DungeonRunAdoptPhase::Clearing);
}

void AMapThatCreditsNothingIsNotReadAsCleared()
{
    // Unknowable is not Complete: a map with no encounter list would otherwise
    // be walked out of the moment anybody was found in it.
    Check("split party, completion unknowable",
          DungeonRunAdoptAt(false, DungeonCompletion::Unknowable),
          DungeonRunAdoptPhase::StagedInside);
    Check("whole party, completion unknowable",
          DungeonRunAdoptAt(true, DungeonCompletion::Unknowable),
          DungeonRunAdoptPhase::Clearing);
}

}  // namespace

int main()
{
    AClearedInstanceIsLeftNotStagedInto();
    AnInstanceWithBossesLeftKeepsTheOldAnswer();
    AMapThatCreditsNothingIsNotReadAsCleared();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("dungeon adopt cleared: all checks passed\n");
    return 0;
}
