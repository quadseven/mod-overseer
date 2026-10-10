/*
 * An evacuation walk that has not got anybody out in ten minutes has failed,
 * whichever release ended it, and the member hearths out.
 *
 * THE READING THIS EXISTS FOR, measured on the dev realm 2026-10-10. Four
 * members of the Horde family stood inside Shadowfang Keep (map 33) about 34
 * yards from the exit door, areatrigger 194, from 01:46 to 10:04 America/New_York
 * while campaign 74's run sat in REPAIRING. Every twenty minutes the leader's
 * walk to 'trigger:194' ended "(failed) after 1203s - the travel drive (travel
 * backstop: no nearer for minutes)" and the next poll aimed it at the same door
 * again. The hearth that a failed evacuation earns since 2026-10-08 never came,
 * because it was keyed on the travel drive's GROUND give-up only, and the
 * release that fired was the backstop. After a worldserver restart the same 34
 * yards were walked in 25 seconds, so the door and the ground are fine; the
 * walk had simply never started, and nothing bounded that.
 *
 * What is pinned here: the 2026-10-08 ground give-up still counts; a walk that
 * has run the bound without getting the member out counts too, however it was
 * released; a walk inside the bound with no give-up keeps walking; and a walk
 * that never began cannot fail.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DecideEvacuationWay;
using OverseerDecisions::EVACUATION_WALK_BOUND_SECONDS;
using OverseerDecisions::EvacuationWalkFailed;
using OverseerDecisions::EvacuationWay;
using OverseerDecisions::ExitHearthStep;

namespace
{

int failures = 0;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

}  // namespace

int main()
{
    std::int64_t const bound = EVACUATION_WALK_BOUND_SECONDS;

    // The bound is shorter than the travel backstop (twenty minutes), so a walk
    // that never starts is hearthed before its first backstop release, and long
    // enough for a party deep inside to walk to the door.
    CheckBool("the bound is under the twenty-minute travel backstop", bound < 20 * 60, true);
    CheckBool("the bound leaves a long walk out its time", bound >= 5 * 60, true);

    // THE MEASURED CASE: aimed at 05:46:30Z, released only by the backstop, no
    // ground give-up at all. By the bound it has failed.
    std::int64_t const since = 1760075190;
    CheckBool("a walk that ran the bound with no give-up has failed",
              EvacuationWalkFailed(since, 0, since + bound, bound), true);
    CheckBool("eight hours in, it has long failed",
              EvacuationWalkFailed(since, 0, since + 8 * 3600, bound), true);
    CheckBool("and the member hearths when it can cast",
              DecideEvacuationWay(EvacuationWalkFailed(since, 0, since + bound, bound),
                                  ExitHearthStep::Cast) == EvacuationWay::Hearth,
              true);

    // Inside the bound, a walk with no give-up is left to walk.
    CheckBool("a walk one second inside the bound has not failed",
              EvacuationWalkFailed(since, 0, since + bound - 1, bound), false);
    CheckBool("a walk just begun has not failed",
              EvacuationWalkFailed(since, 0, since, bound), false);
    CheckBool("and it is not interrupted for a stone",
              DecideEvacuationWay(EvacuationWalkFailed(since, 0, since + 60, bound),
                                  ExitHearthStep::Cast) == EvacuationWay::Walk,
              true);

    // 2026-10-08 is unchanged: a ground give-up during the walk fails it at
    // once, and one from before the walk does not.
    CheckBool("a ground give-up during the walk still fails it at once",
              EvacuationWalkFailed(since, since + 40, since + 41, bound), true);
    CheckBool("a ground give-up from before the walk does not",
              EvacuationWalkFailed(since, since - 100, since + 41, bound), false);

    // A walk that never began has nothing to fail, and a clock stepped back
    // never fails one.
    CheckBool("a walk that never began cannot fail",
              EvacuationWalkFailed(0, 0, since + 8 * 3600, bound), false);
    CheckBool("a clock stepped backwards does not fail a walk",
              EvacuationWalkFailed(since, 0, since - 30, bound), false);

    // A member that cannot hearth keeps its walk even after the bound: there
    // is nothing else to try without a teleport.
    CheckBool("a failed walk with no usable stone keeps walking",
              DecideEvacuationWay(true, ExitHearthStep::Impossible) == EvacuationWay::Walk,
              true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_evacuation_walk_bound: all checks passed\n");
    return 0;
}
