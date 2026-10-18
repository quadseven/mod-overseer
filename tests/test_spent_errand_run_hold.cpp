/*
 * A travel aim the travel drive has already given up on no longer holds a new
 * dungeon run (#762).
 *
 * Measured on the dev realm on 2026-09-27 (UTC), after a worldserver restart:
 * a new run deferred staging at 15:43:05 because its leader still carried the
 * Theramore berth aim from a crossing before the restart. At 16:12:47 the
 * travel drive released that errand as unreachable, but the release skipped
 * the column write - a positional aim nothing in the new process had claimed -
 * so the aim stayed, and the run kept deferring until the operator cleared the
 * column by hand at 16:16:38.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::DungeonRunMayClaimTravel;
using OverseerDecisions::IsTravelGiveUpRelease;
using OverseerDecisions::TravelBackstopReleaseReason;
using OverseerDecisions::TravelErrandBackstopSpent;
using OverseerDecisions::TravelGroundGiveUpReleaseReason;
using OverseerDecisions::TravelNoProgressReleaseReason;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// The two aims measured on 2026-09-27: a berth and a second leader's aim.
std::string const Berth = "at:1:-4010.7,-4727.5,5.5";
std::string const OtherAim = "at:1:1630.48,-4436.13,15.0737";

void TheMeasuredCase()
{
    // The leader carries the berth, the travel drive has given up on exactly
    // that aim, and the column still holds it: the backstop is spent.
    bool const spent = TravelErrandBackstopSpent(Berth, Berth, TravelBackstopReleaseReason);
    Check("the berth released as unreachable and still standing is spent", spent);
    Check("so the run takes the travel wheel instead of deferring for ever",
          DungeonRunMayClaimTravel(true, false, spent));
    // Before the backstop fires there is nothing to tell it from a live walk,
    // and the run still waits on it, exactly as before.
    Check("before the backstop has fired the run still defers",
          !DungeonRunMayClaimTravel(true, false,
                                    TravelErrandBackstopSpent(Berth, "", "")));
}

void EveryGiveUpOfTheTravelDriveCounts()
{
    Check("the ground give-up is a spent backstop",
          TravelErrandBackstopSpent(Berth, Berth, TravelGroundGiveUpReleaseReason));
    Check("the no-progress release is a spent backstop",
          TravelErrandBackstopSpent(Berth, Berth, TravelNoProgressReleaseReason));
    // The crossing's attempt count reads IsTravelGiveUpRelease, and naming the
    // no-progress reason must not change what it counts.
    Check("the crossing's give-up reading is unchanged",
          !IsTravelGiveUpRelease(TravelNoProgressReleaseReason));
}

void OnlyThatAimAndOnlyAGiveUp()
{
    Check("a give-up on another aim does not spend this one",
          !TravelErrandBackstopSpent(Berth, OtherAim, TravelBackstopReleaseReason));
    Check("an escort ending is not the errand's backstop",
          !TravelErrandBackstopSpent(Berth, Berth, "an escort ending (EndOneEscort)"));
    Check("a crossing handing back is not the travel drive's give-up",
          !TravelErrandBackstopSpent(Berth, Berth,
                                     "the campaign crossing (gave up on the berth)"));
    Check("an end that named no aim spends nothing",
          !TravelErrandBackstopSpent(Berth, "", TravelBackstopReleaseReason));
    Check("an empty column is no errand to have spent",
          !TravelErrandBackstopSpent("", "", TravelBackstopReleaseReason));
    // A keyword errand is the bridge's to clear, and a run's claim would not
    // outrank a counter keyword, so the run keeps waiting on it.
    Check("a keyword errand given up on is left to its own writer",
          !TravelErrandBackstopSpent("repair", "repair", TravelBackstopReleaseReason));
    // The inn aim the hearth regroup writes is a point, like the berth.
    std::string const Inn = "at:0:-9459.21,42.08,56.96";
    Check("an inn aim given up on and still standing is spent",
          TravelErrandBackstopSpent(Inn, Inn, TravelNoProgressReleaseReason));
}

void TheOlderRulesStand()
{
    Check("a live errand that is not the run's still holds the run",
          !DungeonRunMayClaimTravel(true, false, false));
    Check("the run's own staging aim is still adopted (#596)",
          DungeonRunMayClaimTravel(true, true, false));
    Check("no errand at all never held it", DungeonRunMayClaimTravel(false, false, false));
    Check("the two-fact reading is unchanged", !DungeonRunMayClaimTravel(true, false));
}

}  // namespace

int main()
{
    TheMeasuredCase();
    EveryGiveUpOfTheTravelDriveCounts();
    OnlyThatAimAndOnlyAGiveUp();
    TheOlderRulesStand();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all spent-errand run-hold checks passed\n");
    return EXIT_SUCCESS;
}
