/*
 * The plane under the city is not ground the character may stand on.
 *
 * On 2026-09-29 the operator watched the family's leader ride over a flat brown
 * plain with a blank minimap and asked why he was under the world. He was. The
 * map's height field for the tiles under and beside Stormwind is a flat
 * placeholder at z 59.457 (measured from the worldserver's own .map files: the
 * value at every position sampled below, to three decimals), the city itself
 * lives in WMO geometry above it, and the navmesh has polygons on the
 * placeholder because the mesh is built from that terrain. So a character that
 * has left the city floor for the placeholder reads as standing on walkable
 * ground: Detour finds a polygon at its feet, the footing fan holds on flat
 * ground, and the drive logged
 *
 *   'Grug' at map 0 position (-8445.8, 490.3, 59.5) reads 58.3 yards under a
 *   surface at z 117.8, but this module finds ground at its own feet ... this
 *   is the detector being wrong
 *
 * at 20:11:24Z, then went quiet about him for 600 seconds while he rode a
 * straight line at z 59.457 for two thousand yards. The detector was not
 * wrong. It asked whether there is ground and never which ground.
 *
 * These cases pin the instrument that does: a character standing on the raw
 * terrain (its z within a stride of the terrain-only height) where the terrain
 * is featureless (a ring of terrain samples all at that height) on an open map
 * is on the plane, whatever a navmesh says. And they pin the recovery: back to
 * the last ground it stood on, on the same map, bounded, and loud.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::OnTheHiddenPlane;
using OverseerDecisions::PlaneRingIsFlat;
using OverseerDecisions::ProvenBelowTheWorld;
using OverseerDecisions::TerrainReading;
using OverseerDecisions::TerrainRecoveryLimits;
using OverseerDecisions::TerrainRecoveryState;
using OverseerDecisions::TerrainRecoveryStep;
using OverseerDecisions::TerrainRecoveryVerdict;
using OverseerDecisions::TerrainRemedy;
using OverseerDecisions::TerrainRemedyEndsTheErrand;
using OverseerDecisions::TerrainSample;

namespace
{

int failures = 0;

char const* Name(TerrainRemedy remedy)
{
    switch (remedy)
    {
        case TerrainRemedy::Nothing:            return "Nothing";
        case TerrainRemedy::LiftToSurface:      return "LiftToSurface";
        case TerrainRemedy::GiveUp:             return "GiveUp";
        case TerrainRemedy::NotFalling:         return "NotFalling";
        case TerrainRemedy::ReturnToLastGround: return "ReturnToLastGround";
        case TerrainRemedy::SetDown:       return "SetDown";
    }
    return "?";
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckRemedy(char const* what, TerrainRemedy got, TerrainRemedy want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const d = got > want ? got - want : want - got;
    if (d < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

// The limits the adapter passes; the last three are the plane's own.
TerrainRecoveryLimits const LIVE{
    10.f,   // minimumGap
    25.f,   // overrideGap
    0.5f,   // liftClearance
    600,    // forgetSeconds
    250.f,  // episodeRadius
    2.f,    // footingReach
    250.f,  // voidCatchYards
    30.f,   // maxLiftYards
    5,      // fallingWindowSeconds
    2.f,    // planeReach
    900,    // lastGroundMaxAgeSeconds
    2       // maxPlaneReturns
};

float const PLANE_Z = 59.459f;

// Grug standing on the placeholder under the Trade District: the exact reading
// of 20:11:24Z, with a navmesh polygon at his feet and the footing fan holding,
// which is what made the old rule call it ground.
TerrainReading OnThePlane()
{
    TerrainReading r;
    r.mapId = 0;
    r.x = -8445.8f;
    r.y = 490.3f;
    r.z = 59.5f;
    r.surfaceAboveZ = 117.8f;
    r.surfaceValid = true;
    r.hasLocalNavmesh = true;
    r.footingHolds = true;
    r.floorBelowValid = false;
    r.movementMeasured = true;
    r.falling = false;
    r.terrainValid = true;
    r.terrainZ = PLANE_Z;
    r.planeFlat = true;
    r.onOpenMap = true;
    return r;
}

// Grug on the street: a layer 37 yards over the raw terrain, open sky, the
// place he stood a minute before.
TerrainReading OnTheStreet()
{
    TerrainReading r = OnThePlane();
    r.x = -8811.0f;
    r.y = 660.0f;
    r.z = 96.6f;
    r.surfaceAboveZ = 96.6f;
    return r;
}

// The Northshire road under an arch: real hills, the terrain is not featureless.
TerrainReading UnderAnArchOnRealTerrain()
{
    TerrainReading r = OnThePlane();
    r.x = -9057.f;
    r.y = -47.f;
    r.z = 88.6f;
    r.surfaceAboveZ = 116.f;
    r.terrainZ = 88.5f;
    r.planeFlat = false;
    return r;
}

void TheMeasuredReadingIsThePlane()
{
    Check("the 20:11:24Z reading is on the hidden plane",
          OnTheHiddenPlane(OnThePlane(), LIVE), true);
    Check("and it is proven below the world for every other drive",
          ProvenBelowTheWorld(OnThePlane(), LIVE), true);
}

void TheOpenExpanseWithNothingOverheadIsStillThePlane()
{
    // Two thousand yards of the ride had no surface over it at all: the plain
    // the operator saw. No cover is needed to be on the plane.
    TerrainReading r = OnThePlane();
    r.x = -7775.1f;
    r.y = -1.6f;
    r.surfaceValid = false;
    Check("open expanse, no surface overhead", OnTheHiddenPlane(r, LIVE), true);
}

void ThingsThatAreNotThePlane()
{
    Check("street layer 37 yards over the terrain",
          OnTheHiddenPlane(OnTheStreet(), LIVE), false);
    Check("arch over real, uneven terrain",
          OnTheHiddenPlane(UnderAnArchOnRealTerrain(), LIVE), false);

    TerrainReading inst = OnThePlane();
    inst.onOpenMap = false;
    Check("an instance map, whose terrain is a placeholder by design",
          OnTheHiddenPlane(inst, LIVE), false);

    TerrainReading unmeasured = OnThePlane();
    unmeasured.terrainValid = false;
    Check("no terrain height read is not evidence",
          OnTheHiddenPlane(unmeasured, LIVE), false);

    TerrainReading unflat = OnThePlane();
    unflat.planeFlat = false;
    Check("uneven terrain around the feet", OnTheHiddenPlane(unflat, LIVE), false);

    TerrainReading swimmer = OnThePlane();
    swimmer.z = PLANE_Z + 5.f;
    Check("more than a stride above the terrain", OnTheHiddenPlane(swimmer, LIVE),
          false);

    TerrainReading old;
    Check("a reading that measured nothing (the old behaviour)",
          OnTheHiddenPlane(old, LIVE), false);
}

void TheRingIsFlatOnlyWhenEverySampleIs()
{
    std::vector<TerrainSample> flat(8, TerrainSample{true, PLANE_Z});
    Check("eight samples at the feet's height", PlaneRingIsFlat(flat, PLANE_Z, 0.25f),
          true);

    std::vector<TerrainSample> hill = flat;
    hill[3].z = PLANE_Z + 4.f;
    Check("one sample four yards up", PlaneRingIsFlat(hill, PLANE_Z, 0.25f), false);

    std::vector<TerrainSample> gap = flat;
    gap[5].valid = false;
    Check("a sample the core could not read", PlaneRingIsFlat(gap, PLANE_Z, 0.25f),
          false);

    Check("no ring at all", PlaneRingIsFlat({}, PLANE_Z, 0.25f), false);
    Check("a zero tolerance disables it", PlaneRingIsFlat(flat, PLANE_Z, 0.f), false);
}

void ItReturnsHimToTheGroundHeStoodOn()
{
    TerrainRecoveryState state;
    // A minute earlier: on the street, open sky.
    CheckRemedy("the street is quiet",
                TerrainRecoveryStep(state, OnTheStreet(), LIVE, 1000).remedy,
                TerrainRemedy::Nothing);
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, OnThePlane(), LIVE, 1060);
    CheckRemedy("on the plane he is sent back", v.remedy,
                TerrainRemedy::ReturnToLastGround);
    Check("same map", v.groundMapId == 0, true);
    CheckNear("same street x", v.groundX, -8811.f);
    CheckNear("same street y", v.groundY, 660.f);
    CheckNear("the street's z", v.groundZ, 96.6f);
    Check("and it ends the errand that walked him there",
          TerrainRemedyEndsTheErrand(v.remedy, true), true);
}

void NoGroundIsRecordedFromThePlaneOrAFall()
{
    TerrainRecoveryState state;
    TerrainRecoveryStep(state, OnThePlane(), LIVE, 1000);   // may act; not recorded
    TerrainRecoveryStep(state, OnThePlane(), LIVE, 1001);
    TerrainReading falling = OnTheStreet();
    falling.falling = true;
    TerrainRecoveryState other;
    TerrainRecoveryStep(other, falling, LIVE, 1000);
    // With nothing recorded the only remedy left is the lift, and it is not
    // the plane pretending to be ground.
    TerrainRecoveryState fresh;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(fresh, OnThePlane(), LIVE, 1000);
    CheckRemedy("no ground on record: lift to the surface over him", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("to the surface plus clearance", v.liftZ, 118.3f);
    TerrainRecoveryVerdict const w =
        TerrainRecoveryStep(other, OnThePlane(), LIVE, 1002);
    Check("a fall's position was never recorded as ground",
          w.remedy != TerrainRemedy::ReturnToLastGround, true);
}

void GroundOnAnotherMapOrLongAgoIsNotUsed()
{
    TerrainRecoveryState otherMap;
    TerrainReading street = OnTheStreet();
    street.mapId = 1;
    TerrainRecoveryStep(otherMap, street, LIVE, 1000);
    Check("ground on another map is not a place to send him",
          TerrainRecoveryStep(otherMap, OnThePlane(), LIVE, 1010).remedy !=
              TerrainRemedy::ReturnToLastGround,
          true);

    TerrainRecoveryState stale;
    TerrainRecoveryStep(stale, OnTheStreet(), LIVE, 1000);
    Check("ground from an hour ago is not a place to send him",
          TerrainRecoveryStep(stale, OnThePlane(), LIVE, 1000 + 3600).remedy !=
              TerrainRemedy::ReturnToLastGround,
          true);
}

void TheBoundIsTwoThenLoudThenQuiet()
{
    TerrainRecoveryState state;
    TerrainRecoveryStep(state, OnTheStreet(), LIVE, 1000);
    CheckRemedy("first", TerrainRecoveryStep(state, OnThePlane(), LIVE, 1010).remedy,
                TerrainRemedy::ReturnToLastGround);
    CheckRemedy("second", TerrainRecoveryStep(state, OnThePlane(), LIVE, 1012).remedy,
                TerrainRemedy::ReturnToLastGround);
    CheckRemedy("third is the loud give-up",
                TerrainRecoveryStep(state, OnThePlane(), LIVE, 1014).remedy,
                TerrainRemedy::GiveUp);
    CheckRemedy("and then it is quiet, not a teleport a second",
                TerrainRecoveryStep(state, OnThePlane(), LIVE, 1016).remedy,
                TerrainRemedy::Nothing);
    // A cooldown and not a retirement: past the forget window it acts again.
    CheckRemedy("past the forget window the ladder is re-armed",
                TerrainRecoveryStep(state, OnThePlane(), LIVE, 1014 + 601).remedy,
                TerrainRemedy::ReturnToLastGround);
}

void RealTerrainUnderAnArchIsUntouched()
{
    // The #725 and #262 cases: real ground, a roof over it, a polygon at the
    // feet. Nothing the plane rung does may move him.
    TerrainRecoveryState state;
    TerrainRecoveryStep(state, OnTheStreet(), LIVE, 1000);
    for (int t = 0; t < 30; ++t)
    {
        TerrainRecoveryVerdict const v = TerrainRecoveryStep(
            state, UnderAnArchOnRealTerrain(), LIVE, 1010 + t);
        Check("an arch over real terrain is never returned or lifted",
              v.remedy != TerrainRemedy::ReturnToLastGround &&
                  v.remedy != TerrainRemedy::LiftToSurface,
              true);
    }
}

}  // namespace

int main()
{
    TheMeasuredReadingIsThePlane();
    TheOpenExpanseWithNothingOverheadIsStillThePlane();
    ThingsThatAreNotThePlane();
    TheRingIsFlatOnlyWhenEverySampleIs();
    ItReturnsHimToTheGroundHeStoodOn();
    NoGroundIsRecordedFromThePlaneOrAFall();
    GroundOnAnotherMapOrLongAgoIsNotUsed();
    TheBoundIsTwoThenLoudThenQuiet();
    RealTerrainUnderAnArchIsUntouched();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("hidden plane: ok\n");
    return 0;
}
