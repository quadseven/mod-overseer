/*
 * The readings behind a "remained below raw terrain" line, in words (#1604).
 *
 * The line said where a character was and how far under the raw terrain, and
 * nothing about what the other instruments read there. Two places repeated on
 * the live realm and the log could not say whether the detector was wrong (a
 * floor under a cliff, a cave) or a character was really falling. These pin
 * the sentence that carries the readings, so one read of a live log can tell
 * the two apart.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

using OverseerDecisions::TerrainMismatchReadings;
using OverseerDecisions::TerrainReading;

namespace
{

int failures = 0;

void Check(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    %s\n  wanted %s\n", what, got.c_str(),
                want.c_str());
    ++failures;
}

// Place A of #1604 as the ticket gives it: a character at z 83.4 under raw
// terrain far above it.
TerrainReading PlaceA()
{
    TerrainReading r;
    r.mapId = 0;
    r.x = 2821.9f;
    r.y = -2530.1f;
    r.z = 83.4f;
    r.onOpenMap = true;
    r.terrainValid = true;
    r.terrainZ = 180.f;
    r.movementMeasured = true;
    return r;
}

void AFloorUnderACliffReadsAsAFloor()
{
    // The overhead probe reaches sixty yards and found only the character's
    // own floor, so the recovery gate never asked Detour or looked down. The
    // adapter asks both for the line, and the line says that it did.
    TerrainReading r = PlaceA();
    r.surfaceValid = true;
    r.surfaceAboveZ = 83.6f;
    r.hasLocalNavmesh = true;
    r.floorBelowValid = true;
    r.floorBelowZ = 83.1f;
    Check("a floor under a cliff, probed for the line only",
          TerrainMismatchReadings(r, false),
          "overhead surface z 83.6 (0.2 above the character); local navmesh yes; "
          "footing fan not measured; floor under the feet z 83.1 (character 0.3 "
          "above it); falling no; navmesh and floor probed for this line only");
}

void ARealFallReadsAsNothingUnderTheFeet()
{
    TerrainReading r = PlaceA();
    r.surfaceValid = true;
    r.surfaceAboveZ = 131.4f;
    r.falling = true;
    Check("falling under a surface with nothing below",
          TerrainMismatchReadings(r, true),
          "overhead surface z 131.4 (48.0 above the character); local navmesh no; "
          "footing fan not measured; floor under the feet none in reach; "
          "falling yes; navmesh and floor probed by the recovery gate");
}

void APolygonNoDirectionLeavesIsSaidAsSuch()
{
    // The #262 pocket: Detour found a polygon and the footing fan refused
    // every bearing. The fan is only walked by the gate, and only over a
    // polygon, which is the one case its answer is a measurement.
    TerrainReading r = PlaceA();
    r.surfaceValid = true;
    r.surfaceAboveZ = 120.f;
    r.hasLocalNavmesh = true;
    r.footingHolds = false;
    r.floorBelowValid = true;
    r.floorBelowZ = 60.4f;
    Check("a polygon with no way out and a floor a long way down",
          TerrainMismatchReadings(r, true),
          "overhead surface z 120.0 (36.6 above the character); local navmesh yes; "
          "footing fan does not hold; floor under the feet z 60.4 (character 23.0 "
          "above it); falling no; navmesh and floor probed by the recovery gate");

    r.footingHolds = true;
    Check("the same polygon with a direction that holds",
          TerrainMismatchReadings(r, true),
          "overhead surface z 120.0 (36.6 above the character); local navmesh yes; "
          "footing fan holds; floor under the feet z 60.4 (character 23.0 "
          "above it); falling no; navmesh and floor probed by the recovery gate");
}

void AnAbsentReadingIsNeverPrintedAsANumber()
{
    // No overhead surface, a floor found a little ABOVE the feet (the probe
    // starts a stride over them), and a movement state nobody measured. The
    // stale numbers on an invalid reading must not reach the line.
    TerrainReading r = PlaceA();
    r.surfaceValid = false;
    r.surfaceAboveZ = 999.f;
    r.floorBelowValid = true;
    r.floorBelowZ = 84.9f;
    r.movementMeasured = false;
    r.falling = true;
    Check("no overhead surface and a floor above the feet",
          TerrainMismatchReadings(r, false),
          "overhead surface none in reach; local navmesh no; footing fan not "
          "measured; floor under the feet z 84.9 (character 1.5 below it); "
          "falling not measured; navmesh and floor probed for this line only");

    r.floorBelowValid = false;
    r.floorBelowZ = std::numeric_limits<float>::quiet_NaN();
    r.surfaceValid = true;
    r.surfaceAboveZ = 70.f;
    r.movementMeasured = true;
    r.falling = false;
    Check("an overhead surface that is under the character",
          TerrainMismatchReadings(r, true),
          "overhead surface z 70.0 (13.4 below the character); local navmesh no; "
          "footing fan not measured; floor under the feet none in reach; "
          "falling no; navmesh and floor probed by the recovery gate");
}

}  // namespace

int main()
{
    AFloorUnderACliffReadsAsAFloor();
    ARealFallReadsAsNothingUnderTheFeet();
    APolygonNoDirectionLeavesIsSaidAsSuch();
    AnAbsentReadingIsNeverPrintedAsANumber();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the below-terrain line names every reading behind it\n");
    return EXIT_SUCCESS;
}
