/*
 * A member still going forward on its surveyed route is not stalled.
 *
 * Measured 2026-09-23 on the dev realm. The Horde leader reached the Ragefire
 * Chasm staging point in Orgrimmar's Cleft of Shadow; his three followers were
 * on the level above, 176 to 205 yards out and 44 to 69 yards up, walking a
 * surveyed way down of over six hundred yards that first goes around. The
 * staging watchdog read the straight-line gap, saw no descent for ninety
 * seconds, and closed the run as "stopped descending". A route position that
 * moved on is progress.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::RouteCursorAdvanced;

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

}  // namespace

int main()
{
    Check("moving from point 12 to 13 is progress", RouteCursorAdvanced(12, 13));
    Check("standing on point 13 is not", !RouteCursorAdvanced(13, 13));
    Check("a new route starting over at 0 is not progress by itself",
          !RouteCursorAdvanced(40, 0));
    Check("the first reading of a route is not progress", !RouteCursorAdvanced(-1, 0));
    Check("no route is not progress", !RouteCursorAdvanced(5, -1));
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
