/*
 * Recovery from a floor below the world, decided without a world.
 *
 * The live failure this pins was not a dead character. All five roster
 * members were alive and moving together at z 59-61 beneath a city whose
 * walkable surface at those coordinates is around z 95. The wall check can
 * prevent the step that gets there, but once a character is already below
 * geometry every horizontal step can be clear and nothing brings it back.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The adapter
 * asks the map for the readings; this file pins what those readings mean.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::BelowTerrainNeedsRecovery;
using OverseerDecisions::TerrainRecoveryMayInspect;

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

void TheMeasuredGapIsRecovered()
{
    Check("35 yards below the surface without a navmesh",
          BelowTerrainNeedsRecovery(60.f, 95.f, true, false, 30.f), true);
}

void DeliberatelyAirborneStatesAreLeftAlone()
{
    Check("ordinary living character is inspectable",
          TerrainRecoveryMayInspect(true, false, false, false, false, false, false),
          true);
    Check("dead character", TerrainRecoveryMayInspect(
              false, false, false, false, false, false, false), false);
    Check("teleport in progress", TerrainRecoveryMayInspect(
              true, true, false, false, false, false, false), false);
    Check("taxi flight", TerrainRecoveryMayInspect(
              true, false, true, false, false, false, false), false);
    Check("free flight", TerrainRecoveryMayInspect(
              true, false, false, true, false, false, false), false);
    Check("swimming", TerrainRecoveryMayInspect(
              true, false, false, false, true, false, false), false);
    Check("transport", TerrainRecoveryMayInspect(
              true, false, false, false, false, true, false), false);
    Check("vehicle", TerrainRecoveryMayInspect(
              true, false, false, false, false, false, true), false);
}

void TheBoundaryIsARecovery()
{
    Check("exactly the declared gap",
          BelowTerrainNeedsRecovery(60.f, 90.f, true, false, 30.f), true);
}

void AnUnknownSurfaceSaysNothing()
{
    Check("invalid surface reading",
          BelowTerrainNeedsRecovery(60.f, 95.f, false, false, 30.f), false);
}

void AnOrdinaryHeightDifferenceIsLeftAlone()
{
    Check("small gap",
          BelowTerrainNeedsRecovery(60.f, 79.f, true, false, 30.f), false);
    Check("surface below the character",
          BelowTerrainNeedsRecovery(60.f, 40.f, true, false, 30.f), false);
}

void ARealInteriorHasAPathAndIsLeftAlone()
{
    Check("cave or building with a local navmesh",
          BelowTerrainNeedsRecovery(60.f, 95.f, true, true, 30.f), false);
}

}  // namespace

int main()
{
    TheMeasuredGapIsRecovered();
    DeliberatelyAirborneStatesAreLeftAlone();
    TheBoundaryIsARecovery();
    AnUnknownSurfaceSaysNothing();
    AnOrdinaryHeightDifferenceIsLeftAlone();
    ARealInteriorHasAPathAndIsLeftAlone();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the below-terrain recovery decision holds\n");
    return EXIT_SUCCESS;
}
