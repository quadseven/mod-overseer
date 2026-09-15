/*
 * NextFishDriveStep, decided without a world.
 *
 * WHAT THIS PINS. The one property that actually matters for a standing
 * errand: once "master fishing" is on for a job='fish' character, this
 * function must say Nothing every single poll after - never re-issue TurnOn
 * (mod-playerbots has no reason to reject a redundant ChangeStrategy, but a
 * decision that fires every tick forever is not idempotent, it is merely
 * harmless, and this project's whole professions test exists to keep
 * "harmless but pointless" out of its drives). And the skill gate: a
 * character with job='fish' and no Fishing skill yet must get Nothing, not
 * TurnOn - see the comment on NextFishDriveStep for why turning the strategy
 * on early would be silent and useless rather than merely early.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FishDriveStep;
using OverseerDecisions::NextFishDriveStep;

namespace
{

int failures = 0;

char const* StepName(FishDriveStep step)
{
    switch (step)
    {
        case FishDriveStep::Nothing: return "Nothing";
        case FishDriveStep::TurnOn:  return "TurnOn";
        case FishDriveStep::TurnOff: return "TurnOff";
    }
    return "?";
}

void Expect(char const* what, FishDriveStep got, FishDriveStep want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected %s, got %s\n", what, StepName(want), StepName(got));
}

}  // namespace

int main()
{
    // job != 'fish', never started: nothing to do, nothing to undo.
    Expect("not assigned, not running",
           NextFishDriveStep(/*wantsFishing=*/false, /*strategyIsOn=*/false, /*knowsFishing=*/true),
           FishDriveStep::Nothing);

    // job != 'fish', still running: the character was reassigned mid-errand
    // and the strategy has to be told to stop, or mod-playerbots keeps
    // wandering it toward water for a job that no longer says to.
    Expect("reassigned away while running",
           NextFishDriveStep(/*wantsFishing=*/false, /*strategyIsOn=*/true, /*knowsFishing=*/true),
           FishDriveStep::TurnOff);

    // job='fish', not running yet, but the skill is not there - see
    // CanFishValue::Calculate in mod-playerbots (FishValues.cpp): it refuses
    // outright below skill value 0, so turning the strategy on here would
    // not make anybody fish, only poll uselessly. TrainFishingOnArrival's
    // job, not DriveFish's.
    Expect("assigned but does not know fishing yet",
           NextFishDriveStep(/*wantsFishing=*/true, /*strategyIsOn=*/false, /*knowsFishing=*/false),
           FishDriveStep::Nothing);

    // job='fish', not running, skill known: the one case that actually
    // starts anything.
    Expect("assigned, idle, knows fishing",
           NextFishDriveStep(/*wantsFishing=*/true, /*strategyIsOn=*/false, /*knowsFishing=*/true),
           FishDriveStep::TurnOn);

    // job='fish', ALREADY running: idempotence. This is the state a
    // character sits in for the entire rest of a standing errand and it must
    // read as Nothing on every single one of those polls, not TurnOn again.
    Expect("already fishing, still assigned",
           NextFishDriveStep(/*wantsFishing=*/true, /*strategyIsOn=*/true, /*knowsFishing=*/true),
           FishDriveStep::Nothing);

    // Learning Fishing mid-run cannot un-start something already started -
    // knowsFishing only ever gates TurnOn, never TurnOff. Exercised because
    // the natural bug here is checking knowsFishing before strategyIsOn and
    // getting an errand that "stops" the moment a stale read reports the
    // skill missing.
    Expect("already fishing, and a stale read claims no skill",
           NextFishDriveStep(/*wantsFishing=*/true, /*strategyIsOn=*/true, /*knowsFishing=*/false),
           FishDriveStep::Nothing);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::printf("test_fish_drive: all cases passed\n");
    return EXIT_SUCCESS;
}
