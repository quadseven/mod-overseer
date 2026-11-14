/*
 * A run the dungeon module stopped on a party wait is armed again within a
 * minute, whatever a member reports (#1483).
 *
 * The busy hold protects a run that is fighting. A run the module has switched
 * off cannot fight, so a member still reporting "looting" on it must not keep
 * the re-arm waiting for the whole busy ceiling plus the stall window.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonClearStoppedRunRearmsNow;
using OverseerDecisions::DungeonClearStopIsInGrace;

namespace
{
int failures = 0;

constexpr time_t T0 = 1000000;
constexpr time_t GRACE = 30;
std::string const WAIT =
    "Dungeon clear stopped: the party never became ready after 180s "
    "(Waiting on Uzza (out of range)).";

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

void ARearmableStopIsArmedAgainWithinTheGrace()
{
    Check("stop just seen", DungeonClearStoppedRunRearmsNow(WAIT, 0, 3, T0, T0, GRACE), false);
    Check("stop seen for the grace", DungeonClearStoppedRunRearmsNow(WAIT, 0, 3, T0, T0 + GRACE, GRACE),
          true);
    Check("stop seen for a minute", DungeonClearStoppedRunRearmsNow(WAIT, 1, 3, T0, T0 + 60, GRACE),
          true);
}

void NothingRecordedYetWaitsForTheCaller()
{
    // The caller stamps the first sighting in the same pass; zero is never old.
    Check("no stamp", DungeonClearStoppedRunRearmsNow(WAIT, 0, 3, 0, T0 + 600, GRACE), false);
}

void TheBoundOfRearmsStillHolds()
{
    Check("third re-arm", DungeonClearStoppedRunRearmsNow(WAIT, 2, 3, T0, T0 + 60, GRACE), true);
    Check("bound spent", DungeonClearStoppedRunRearmsNow(WAIT, 3, 3, T0, T0 + 60, GRACE), false);
}

void OtherStopsKeepTheLadder()
{
    Check("other stop",
          DungeonClearStoppedRunRearmsNow("Dungeon clear stopped: All bosses cleared!", 0, 3, T0,
                                          T0 + 60, GRACE),
          false);
    Check("no stop text", DungeonClearStoppedRunRearmsNow("", 0, 3, T0, T0 + 60, GRACE), false);
}

void AClockSteppedBackwardsIsNotOld()
{
    Check("clock behind stamp", DungeonClearStoppedRunRearmsNow(WAIT, 0, 3, T0 + 100, T0, GRACE),
          false);
}

void TheLadderWaitsOutTheGrace()
{
    // A stall clock already expired must not skip or exit a stopped run before
    // the re-arm's grace is over: the previous code armed it again first.
    Check("stop just seen holds the ladder", DungeonClearStopIsInGrace(WAIT, 0, 3, T0, T0, GRACE), true);
    Check("stop seen for 29s holds the ladder",
          DungeonClearStopIsInGrace(WAIT, 0, 3, T0, T0 + GRACE - 1, GRACE), true);
    Check("grace over, the re-arm is due instead",
          DungeonClearStopIsInGrace(WAIT, 0, 3, T0, T0 + GRACE, GRACE), false);
    Check("no stamp, nothing to wait for", DungeonClearStopIsInGrace(WAIT, 0, 3, 0, T0, GRACE), false);
    Check("bound spent, the ladder runs",
          DungeonClearStopIsInGrace(WAIT, 3, 3, T0, T0 + 1, GRACE), false);
    Check("other stop, the ladder runs",
          DungeonClearStopIsInGrace("Dungeon clear stopped: All bosses cleared!", 0, 3, T0, T0 + 1,
                                    GRACE),
          false);
    Check("clock behind stamp is not a wait", DungeonClearStopIsInGrace(WAIT, 0, 3, T0 + 100, T0, GRACE),
          false);
}
}  // namespace

int main()
{
    ARearmableStopIsArmedAgainWithinTheGrace();
    NothingRecordedYetWaitsForTheCaller();
    TheBoundOfRearmsStillHolds();
    OtherStopsKeepTheLadder();
    AClockSteppedBackwardsIsNotOld();
    TheLadderWaitsOutTheGrace();
    return failures ? 1 : 0;
}
