/*
 * The bounded action taken when a dungeon run stops progressing.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter measures boss credit, movement, and party activity; this
 * test pins the safety rule that decides whether the run may be interrupted.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonClearStallAction;
using OverseerDecisions::DungeonClearStallDecision;

namespace
{

int failures = 0;

void Check(char const* what, DungeonClearStallAction got,
           DungeonClearStallAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void HealthyProgressIsNeverInterrupted()
{
    Check("boss progress", DungeonClearStallDecision(true, false, false, true, 3, 3),
          DungeonClearStallAction::Nothing);
    Check("party busy", DungeonClearStallDecision(false, true, false, true, 3, 3),
          DungeonClearStallAction::Nothing);
    Check("movement progress", DungeonClearStallDecision(false, false, true, true, 3, 3),
          DungeonClearStallAction::Nothing);
    Check("not stalled", DungeonClearStallDecision(false, false, false, false, 3, 3),
          DungeonClearStallAction::Nothing);
}

void StalledRunGetsBoundedSkips()
{
    Check("first skip", DungeonClearStallDecision(false, false, false, true, 0, 3),
          DungeonClearStallAction::Skip);
    Check("last skip", DungeonClearStallDecision(false, false, false, true, 2, 3),
          DungeonClearStallAction::Skip);
}

void ExhaustedRunIsExtracted()
{
    Check("at bound", DungeonClearStallDecision(false, false, false, true, 3, 3),
          DungeonClearStallAction::Extract);
    Check("past bound", DungeonClearStallDecision(false, false, false, true, 4, 3),
          DungeonClearStallAction::Extract);
    Check("zero bound", DungeonClearStallDecision(false, false, false, true, 0, 0),
          DungeonClearStallAction::Extract);
}

void ABusyHoldThatExpiredNoLongerProtectsTheRun()
{
    // #382: the adapter used to pass a literal `false` here, because its own
    // busy loop returned before this policy was ever consulted - so the branch
    // above was unreachable from the world and the hold it modelled had no
    // ceiling. `partyBusy` is now the BOUNDED hold rather than the raw reading,
    // which is what makes these two cases different at all: a party that still
    // looks busy but has looked busy too long is judged on what can be seen.
    Check("hold still believed", DungeonClearStallDecision(false, true, false, true, 0, 3),
          DungeonClearStallAction::Nothing);
    Check("hold expired, first skip",
          DungeonClearStallDecision(false, false, false, true, 0, 3),
          DungeonClearStallAction::Skip);
    Check("hold expired, skips spent",
          DungeonClearStallDecision(false, false, false, true, 3, 3),
          DungeonClearStallAction::Extract);
}

} // namespace

int main()
{
    HealthyProgressIsNeverInterrupted();
    StalledRunGetsBoundedSkips();
    ExhaustedRunIsExtracted();
    ABusyHoldThatExpiredNoLongerProtectsTheRun();
    return failures ? 1 : 0;
}
