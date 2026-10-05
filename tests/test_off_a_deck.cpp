/*
 * A character in the air beside a transport is set down, not left to fall.
 *
 * The Horde leader fell out of the Orgrimmar to Undercity zeppelin three times
 * on its first leg over Tirisfal Glades, map 0, and died of the fall each time.
 * The terrain drive read him one poll before every death:
 *
 *   2026-10-03 19:55:32 (1993.9, 737.2, 135.2) under z 166.5, no local navmesh,
 *            the nearest floor more than a stride away, NOT FALLING - dead at
 *            z 38.0 at 19:55:37
 *   2026-10-03 22:31:58 (2000.7, 760.7, 135.3) under z 170.6, the same - dead
 *            at z 39.0 at 22:32:02
 *   2026-10-05 00:51:34 (1994.0, 737.5, 135.3) under z 165.0, the same - dead
 *            at z 37.9 at 00:51:39, "fell 98.0 yards"
 *
 * and logged "NOTHING IS BEING MOVED" each time. The raw terrain at those x and
 * y is z 38.0, so the floor under him was 97 yards down, which kills from full
 * health. These cases pin that such a reading is set down onto that floor, once,
 * and that nothing the not-falling rule (#725) protects is touched: no
 * transport, a polygon, a survivable drop, a fall already under way.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FallDamageShare;
using OverseerDecisions::LethalFallYards;
using OverseerDecisions::OffADeckInTheAir;
using OverseerDecisions::TerrainReading;
using OverseerDecisions::TerrainRecoveryLimits;
using OverseerDecisions::TerrainRecoveryState;
using OverseerDecisions::TerrainRecoveryStep;
using OverseerDecisions::TerrainRecoveryVerdict;
using OverseerDecisions::TerrainRemedy;
using OverseerDecisions::TerrainRemedyEndsTheErrand;

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
        case TerrainRemedy::SetDown:            return "SetDown";
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

// The limits the adapter passes, with the set-down at the drop that kills
// from full health, written the way the adapter writes it.
TerrainRecoveryLimits Live()
{
    TerrainRecoveryLimits l;
    l.minimumGap = 10.f;
    l.overrideGap = 25.f;
    l.liftClearance = 0.5f;
    l.forgetSeconds = 600;
    l.episodeRadius = 250.f;
    l.footingReach = 2.f;
    l.voidCatchYards = 250.f;
    l.maxLiftYards = 30.f;
    l.fallingWindowSeconds = 5;
    l.planeReach = 2.f;
    l.lastGroundMaxAgeSeconds = 900;
    l.maxPlaneReturns = 2;
    l.setDownDropYards = (1.f - OverseerDecisions::FALL_DAMAGE_INTERCEPT) /
                         OverseerDecisions::FALL_DAMAGE_SLOPE;
    return l;
}

// A reading as the adapter builds it in the air beside the zeppelin: map 0,
// the hull overhead, no polygon, a floor found far below, not falling, and the
// raw terrain measured (it is the floor) but nowhere near the feet.
TerrainReading BesideTheZeppelin(float x, float y, float z, float surfaceZ,
                                 float floorZ)
{
    TerrainReading r;
    r.mapId = 0;
    r.x = x;
    r.y = y;
    r.z = z;
    r.surfaceAboveZ = surfaceZ;
    r.surfaceValid = true;
    r.hasLocalNavmesh = false;
    r.footingHolds = true;
    r.floorBelowValid = true;
    r.floorBelowZ = floorZ;
    r.movementMeasured = true;
    r.falling = false;
    r.terrainValid = true;
    r.terrainZ = floorZ;
    r.planeFlat = false;
    r.onOpenMap = true;
    r.besideTransport = true;
    return r;
}

// The bound is the core's own lethal drop, not a number chosen to fit.
void TheBoundIsTheLethalDrop()
{
    float const bound = Live().setDownDropYards;
    Check("the adapter's expression is LethalFallYards",
          std::fabs(bound - LethalFallYards()) < 0.01f, true);
    Check("which costs the whole of max health",
          FallDamageShare(bound) >= 0.999f, true);
    Check("and the 97-yard drop is past it", 135.3f - 38.0f >= bound, true);
}

// The three readings that each came one poll before a death.
void TheThreeDropsAreSetDown()
{
    struct { float x, y, z, surface; time_t t; } const drops[] = {
        {1993.9f, 737.2f, 135.2f, 166.5f, 1000},
        {2000.7f, 760.7f, 135.3f, 170.6f, 2000},
        {1994.0f, 737.5f, 135.3f, 165.0f, 3000},
    };
    for (auto const& d : drops)
    {
        TerrainRecoveryState state;
        TerrainReading const r = BesideTheZeppelin(d.x, d.y, d.z, d.surface, 38.0f);
        Check("the reading is in the air beside a transport",
              OffADeckInTheAir(r, Live().setDownDropYards), true);
        TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, r, Live(), d.t);
        CheckRemedy("not falling, 97 yards over the floor, beside the zeppelin",
                    v.remedy, TerrainRemedy::SetDown);
        Check("set down onto the measured floor", std::fabs(v.liftZ - 38.0f) < 0.01f,
              true);
        CheckRemedy("once per episode: a set-down that did not hold is noted, not repeated",
                    TerrainRecoveryStep(state, r, Live(), d.t + 1).remedy,
                    TerrainRemedy::NotFalling);
        CheckRemedy("and then quiet",
                    TerrainRecoveryStep(state, r, Live(), d.t + 2).remedy,
                    TerrainRemedy::Nothing);
    }
    Check("a set-down keeps the errand",
          TerrainRemedyEndsTheErrand(TerrainRemedy::SetDown, false), false);
}

// Layered ground with no transport in reach is #725's case and stays a note:
// the Horde leader mid-walk in Orgrimmar, and the same drop with nothing near.
void NoTransportIsTheLayeredGroundRule()
{
    TerrainRecoveryState state;
    TerrainReading r = BesideTheZeppelin(1994.0f, 737.5f, 135.3f, 165.0f, 38.0f);
    r.besideTransport = false;
    CheckRemedy("no transport within reach: the not-falling note",
                TerrainRecoveryStep(state, r, Live(), 4000).remedy,
                TerrainRemedy::NotFalling);
}

// A transport at its tower. The Undercity tower top reads 54 yards over the
// terrain (map 0 (2216.2, 287.5, 88.9), floor z 35.1): a hard fall but not a
// lethal one, and a step between a tower and a deck must never be pulled to the
// ground.
void ASurvivableDropBesideATowerIsLeftAlone()
{
    TerrainRecoveryState state;
    TerrainReading const r = BesideTheZeppelin(2216.2f, 287.5f, 88.9f, 143.7f, 35.1f);
    Check("54 yards is under the bound", OffADeckInTheAir(r, Live().setDownDropYards),
          false);
    CheckRemedy("beside the docked zeppelin, survivable drop: the note",
                TerrainRecoveryStep(state, r, Live(), 5000).remedy,
                TerrainRemedy::NotFalling);
}

// Each of the other inputs declines on its own.
void EveryOtherInputDeclines()
{
    float const bound = Live().setDownDropYards;
    TerrainReading base = BesideTheZeppelin(1994.0f, 737.5f, 135.3f, 165.0f, 38.0f);

    TerrainReading polygon = base;
    polygon.hasLocalNavmesh = true;
    Check("a polygon Detour found", OffADeckInTheAir(polygon, bound), false);

    TerrainReading falling = base;
    falling.falling = true;
    Check("already falling: the falling window's business",
          OffADeckInTheAir(falling, bound), false);

    TerrainReading noFloor = base;
    noFloor.floorBelowValid = false;
    Check("no floor measured: nowhere to set it down", OffADeckInTheAir(noFloor, bound),
          false);

    TerrainReading unmeasured = base;
    unmeasured.movementMeasured = false;
    Check("movement not measured", OffADeckInTheAir(unmeasured, bound), false);

    Check("a zero bound disables it", OffADeckInTheAir(base, 0.f), false);

    TerrainReading byDefault;
    byDefault.z = 135.3f;
    byDefault.floorBelowValid = true;
    byDefault.floorBelowZ = 38.0f;
    byDefault.movementMeasured = true;
    Check("an unmeasured transport declines", OffADeckInTheAir(byDefault, bound), false);
}

// A limits struct written before this change has no bound, so it never sets
// anybody down.
void OldLimitsNeverSetDown()
{
    TerrainRecoveryLimits old = Live();
    old.setDownDropYards = 0.f;
    TerrainRecoveryState state;
    CheckRemedy("no bound: the reading is the not-falling note it always was",
                TerrainRecoveryStep(
                    state, BesideTheZeppelin(1994.0f, 737.5f, 135.3f, 165.0f, 38.0f),
                    old, 6000).remedy,
                TerrainRemedy::NotFalling);
}

}  // namespace

int main()
{
    TheBoundIsTheLethalDrop();
    TheThreeDropsAreSetDown();
    NoTransportIsTheLayeredGroundRule();
    ASurvivableDropBesideATowerIsLeftAlone();
    EveryOtherInputDeclines();
    OldLimitsNeverSetDown();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a character in the air beside a transport is set down\n");
    return EXIT_SUCCESS;
}
