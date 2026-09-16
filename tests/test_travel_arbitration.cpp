/*
 * A dungeon run must not erase an outstanding leader errand.
 *
 * This is deliberately a pure policy test. The world adapter supplies the
 * one fact it already read from the roster; it owns the log and the update.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::DungeonRunMayClaimTravel;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void ALeaderWithAnErrandKeepsIt()
{
    Check("outstanding leader errand is not superseded",
          DungeonRunMayClaimTravel(true), false);
}

void AnIdleLeaderMayStartTheRun()
{
    Check("leader without an errand may be claimed",
          DungeonRunMayClaimTravel(false), true);
}

}  // namespace

int main()
{
    ALeaderWithAnErrandKeepsIt();
    AnIdleLeaderMayStartTheRun();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the dungeon travel arbitration decision holds\n");
    return EXIT_SUCCESS;
}
