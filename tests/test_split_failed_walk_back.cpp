/*
 * A member stuck outdoors does not close a run that is fighting (#1472).
 *
 * THE READING THIS EXISTS FOR, measured on the dev realm 2026-11-09 in two
 * Blackrock Depths runs. A caster died in the arena, took the spirit healer on
 * the mountain and walked back toward the door, but its walk stalled about 350
 * yards short. After five minutes with nobody else getting in the run was closed
 * 'split_failed': in one of them only ten seconds after a boss was credited and
 * with four members inside and fighting. The warning line said fifteen minutes.
 *
 * Pinned here: four or more members inside that are fighting, or credited a
 * boss in the last two minutes, are not closed on the ceiling; a stuck walk
 * back to the door is given up on within a bounded time so the stone can be
 * used; and the warning line is said again when the patience it stated changes.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::SplitFailedHeldByProgress;
using OverseerDecisions::StagedInsideWarningStale;
using OverseerDecisions::StrandedWalkBackFailed;

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

constexpr time_t NOW = 1'000'000;
constexpr time_t CEILING = OverseerDecisions::SPLIT_HOLD_FIGHT_CEILING_SECONDS;
}  // namespace

int main()
{
    // The boss fell ten seconds ago with four inside: the 1699079 reading.
    Check("four inside and a boss credited ten seconds ago is not closed",
          SplitFailedHeldByProgress(4, false, NOW - 10, NOW, CEILING));
    Check("five inside and a boss credited ten seconds ago is not closed",
          SplitFailedHeldByProgress(5, false, NOW - 10, NOW, CEILING));
    Check("three inside is the old rule: closed even with a fresh credit",
          !SplitFailedHeldByProgress(3, true, NOW - 10, NOW, CEILING));

    Check("four inside, fighting, last progress five minutes ago, is held",
          SplitFailedHeldByProgress(4, true, NOW - 5 * 60, NOW, CEILING));
    Check("four inside, idle, last progress five minutes ago, is closed",
          !SplitFailedHeldByProgress(4, false, NOW - 5 * 60, NOW, CEILING));
    Check("a fight that has made no progress past the ceiling is closed",
          !SplitFailedHeldByProgress(4, true, NOW - CEILING - 1, NOW, CEILING));
    Check("a combat flag with no progress ever stamped cannot hold forever",
          !SplitFailedHeldByProgress(4, true, 0, NOW, CEILING));
    Check("a clock stepped backwards never holds",
          !SplitFailedHeldByProgress(4, true, NOW + 60, NOW, CEILING));

    // The walk back to the door: refused for a bounded time, then the stone.
    Check("a walk that began two minutes ago has not failed",
          !StrandedWalkBackFailed(NOW - 2 * 60, 0, NOW));
    Check("a walk that has run three minutes has failed",
          StrandedWalkBackFailed(NOW - 3 * 60, 0, NOW));
    Check("a ground give-up after the walk began fails it at once",
          StrandedWalkBackFailed(NOW - 20, NOW - 5, NOW));
    Check("a give-up from before the walk began does not",
          !StrandedWalkBackFailed(NOW - 20, NOW - 60, NOW));
    Check("a walk that never began never fails",
          !StrandedWalkBackFailed(0, 0, NOW));
    Check("the bound is not the run's five minutes",
          OverseerDecisions::STRANDED_WALK_BACK_BOUND_SECONDS < 5 * 60);

    // The warning states the patience the ceiling uses.
    Check("the warning is repeated when the patience shortens",
          StagedInsideWarningStale(15 * 60, 5 * 60));
    Check("and when it lengthens", StagedInsideWarningStale(5 * 60, 15 * 60));
    Check("and not while it is unchanged", !StagedInsideWarningStale(5 * 60, 5 * 60));

    if (failures)
        return 1;
    std::printf("split failed walk back: all checks passed\n");
    return 0;
}
