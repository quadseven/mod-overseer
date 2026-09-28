/*
 * A dungeon run whose leader is off the dungeon map is walked out after a
 * bound, not held forever.
 *
 * 2026-09-28, dev realm: the family stood inside Scarlet Monastery from 01:15
 * to 01:32Z with CLEARING's stall clock held because the leader was not on
 * the map.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::LeaderAwayEndsRun;

namespace
{
int failures = 0;
void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    time_t const t0 = 1790558100;  // 2026-09-28 01:15:00 UTC
    Check(!LeaderAwayEndsRun(0, t0 + 1000, 180), "a leader that is not away ends nothing");
    Check(!LeaderAwayEndsRun(t0, t0 + 180, 180), "at the bound the hold still stands");
    Check(LeaderAwayEndsRun(t0, t0 + 181, 180), "past the bound the run is walked out");
    Check(LeaderAwayEndsRun(t0, t0 + 17 * 60, 180), "seventeen minutes is well past it");
    Check(!LeaderAwayEndsRun(t0, t0 - 30, 180), "a clock stepped backwards ends nothing");
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("leader away: all checks passed\n");
    return 0;
}
