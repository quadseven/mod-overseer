/*
 * Layered ground is not the underside of the world (#725).
 *
 * On 2026-09-26 the operator watched 'Zug' "falling flying" against the rock
 * wall in Orgrimmar near the Cleft of Shadow. The worldserver log for the same
 * minutes shows why: every one of these characters read as tens of yards under
 * a surface, and the ones with no navmesh polygon Detour could see were lifted
 * straight up and fell back down.
 *
 *   02:29:01 'Zug' read as below the world at map 1 position
 *            (1830.7, -3956.4, 19.1), surface z 47.0 (27.9 yards up), no local
 *            navmesh; LIFTED straight up to z 47.5 at the same x/y
 *   02:28:51 'Zork' STILL reads as below the world at map 1 position
 *            (1929.9, -3998.1, 157.6), surface z 192.3 (34.8 yards up)
 *   02:29:02 'Oz' is not aimed at 'Uzza' because the terrain has no ground
 *            within reach of where that leader is standing
 *            (map 1, 1940.6, -3766.7, 55.0)
 *
 * None of them was falling. These cases pin that a character that is not
 * falling is standing on something, that a missing navmesh is unknown rather
 * than evidence, and that a lift is only the last resort for a fall that has
 * gone on, with nothing under it and without rising, for a sustained window.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ClassifyUnmeasuredLeaderGround;
using OverseerDecisions::ProvenBelowTheWorld;
using OverseerDecisions::TerrainReading;
using OverseerDecisions::TerrainRecoveryLimits;
using OverseerDecisions::TerrainRecoveryState;
using OverseerDecisions::TerrainRecoveryStep;
using OverseerDecisions::TerrainRemedy;
using OverseerDecisions::TerrainRemedyEndsTheErrand;
using OverseerDecisions::UnmeasuredLeaderGround;

namespace
{

int failures = 0;

char const* Name(TerrainRemedy remedy)
{
    switch (remedy)
    {
        case TerrainRemedy::Nothing:       return "Nothing";
        case TerrainRemedy::LiftToSurface: return "LiftToSurface";
        case TerrainRemedy::GiveUp:        return "GiveUp";
        case TerrainRemedy::NotFalling:    return "NotFalling";
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

// The limits the adapter passes, including the 30-yard lift cap and the
// five-second falling window.
TerrainRecoveryLimits const LIVE{
    10.f,   // minimumGap
    25.f,   // overrideGap
    0.5f,   // liftClearance
    600,    // forgetSeconds
    250.f,  // episodeRadius
    2.f,    // footingReach
    250.f,  // voidCatchYards
    30.f,   // maxLiftYards
    5       // fallingWindowSeconds
};

// A reading as the adapter now builds it: the movement state is measured, no
// polygon Detour could see, no floor any probe found.
TerrainReading Measured(float x, float y, float z, float surfaceZ, bool falling)
{
    TerrainReading r;
    r.mapId = 1;
    r.x = x;
    r.y = y;
    r.z = z;
    r.surfaceAboveZ = surfaceZ;
    r.surfaceValid = true;
    r.hasLocalNavmesh = false;
    r.footingHolds = true;
    r.floorBelowValid = false;
    r.movementMeasured = true;
    r.falling = falling;
    return r;
}

// 02:29:01. The lift the operator watched fall back down the rock wall.
void ZugMidWalkIsNotLifted()
{
    TerrainRecoveryState state;
    TerrainReading const zug = Measured(1830.7f, -3956.4f, 19.1f, 47.0f, false);
    CheckRemedy("Zug, 27.9 yards under z 47.0, not falling, no navmesh",
                TerrainRecoveryStep(state, zug, LIVE, 1000).remedy,
                TerrainRemedy::NotFalling);
    CheckRemedy("and said once per episode, not every poll",
                TerrainRecoveryStep(state, zug, LIVE, 1001).remedy,
                TerrainRemedy::Nothing);
    for (time_t t = 1002; t < 1060; ++t)
        CheckRemedy("a minute of the same reading never lifts",
                    TerrainRecoveryStep(state, zug, LIVE, t).remedy,
                    TerrainRemedy::Nothing);
    Check("the note ends no errand",
          TerrainRemedyEndsTheErrand(TerrainRemedy::NotFalling, false), false);
}

// 02:28:51-52. Over the lift cap, so the old step gave up loudly on every
// poll and called it "STILL reads as below the world".
void ZorkClimbingThroughRockIsNotBelowTheWorld()
{
    TerrainRecoveryState state;
    CheckRemedy("Zork, 34.8 yards under z 192.3, not falling",
                TerrainRecoveryStep(
                    state, Measured(1929.9f, -3998.1f, 157.6f, 192.3f, false),
                    LIVE, 2000).remedy,
                TerrainRemedy::NotFalling);
    CheckRemedy("and one second later, 55.7 under z 218.0",
                TerrainRecoveryStep(
                    state, Measured(1930.2f, -3992.8f, 162.3f, 218.0f, false),
                    LIVE, 2001).remedy,
                TerrainRemedy::Nothing);
}

// The other three Orgrimmar readings had a polygon and were already left
// alone. They still are.
void TheOnTheGroundReadingsAreUnchanged()
{
    struct { float x, y, z, surface; } const cases[] = {
        {1904.1f, -4634.0f, 33.4f, 67.5f},
        {1735.6f, -4445.3f, 38.2f, 88.3f},
        {1926.8f, -4245.7f, 42.0f, 68.4f},
    };
    for (auto const& c : cases)
    {
        TerrainRecoveryState state;
        TerrainReading r = Measured(c.x, c.y, c.z, c.surface, false);
        r.hasLocalNavmesh = true;
        CheckRemedy("Zug with a polygon at its feet is reported, not moved",
                    TerrainRecoveryStep(state, r, LIVE, 3000).remedy,
                    TerrainRemedy::GiveUp);
        Check("and that report ends no errand",
              TerrainRemedyEndsTheErrand(TerrainRemedy::GiveUp, true), false);
    }
}

// A fall is left to finish. Only a fall that has gone on for the whole window,
// with nothing under it and without the body rising, gets the lift, and only
// once before the give-up.
void ASustainedFallIsTheOnlyOrdinaryLift()
{
    TerrainRecoveryState state;
    time_t t = 4000;
    for (; t < 4005; ++t)
        CheckRemedy("falling inside the window: let the fall finish",
                    TerrainRecoveryStep(
                        state, Measured(1830.7f, -3956.4f, 19.1f, 47.0f, true),
                        LIVE, t).remedy,
                    TerrainRemedy::Nothing);
    CheckRemedy("falling for the whole window, nothing under it: last resort",
                TerrainRecoveryStep(
                    state, Measured(1830.7f, -3956.4f, 19.0f, 47.0f, true),
                    LIVE, t).remedy,
                TerrainRemedy::LiftToSurface);
    CheckRemedy("and a lift that did not stick is not repeated",
                TerrainRecoveryStep(
                    state, Measured(1830.7f, -3956.4f, 18.9f, 47.0f, true),
                    LIVE, t + 1).remedy,
                TerrainRemedy::GiveUp);
}

// A body that has risen since the window opened is climbing, whatever its
// flags say (a player's falling flag is never cleared server-side, #291).
void ARisingBodyIsNotFalling()
{
    TerrainRecoveryState state;
    float z = 140.f;
    for (time_t t = 5000; t < 5020; ++t, z += 1.f)
        CheckRemedy("flag on, but rising a yard a second: never lifted",
                    TerrainRecoveryStep(
                        state, Measured(1940.4f, -3825.4f, z, z + 24.5f, true),
                        LIVE, t).remedy,
                    TerrainRemedy::Nothing);
}

// A poll the drive did not take (a stand-down) is not time anybody measured.
void AGapInThePollsRestartsTheWindow()
{
    TerrainRecoveryState state;
    TerrainReading const r = Measured(1830.7f, -3956.4f, 19.1f, 47.0f, true);
    CheckRemedy("window opens", TerrainRecoveryStep(state, r, LIVE, 6000).remedy,
                TerrainRemedy::Nothing);
    CheckRemedy("twenty unmeasured seconds later the window starts again",
                TerrainRecoveryStep(state, r, LIVE, 6020).remedy,
                TerrainRemedy::Nothing);
    CheckRemedy("and still has to run its whole length",
                TerrainRecoveryStep(state, r, LIVE, 6024).remedy,
                TerrainRemedy::Nothing);
    CheckRemedy("before the last resort",
                TerrainRecoveryStep(state, r, LIVE, 6025).remedy,
                TerrainRemedy::LiftToSurface);
}

// The void catch answers a different question and keeps its own bound: a
// character 250 yards over the kill plane is caught whatever its flags say.
void TheVoidCatchIsUntouched()
{
    TerrainRecoveryState state;
    CheckRemedy("inside the band, not falling: still caught",
                TerrainRecoveryStep(
                    state, Measured(1830.7f, -3956.4f, -300.f, -260.f, false),
                    LIVE, 7000).remedy,
                TerrainRemedy::LiftToSurface);
}

// A reading without the movement state measured behaves as it always did, so
// nothing written before this change moves.
void AnUnmeasuredReadingKeepsTheOldLadder()
{
    TerrainRecoveryState state;
    TerrainReading r = Measured(1830.7f, -3956.4f, 19.1f, 47.0f, false);
    r.movementMeasured = false;
    CheckRemedy("unmeasured: the old first rung",
                TerrainRecoveryStep(state, r, LIVE, 8000).remedy,
                TerrainRemedy::LiftToSurface);
}

// What the other drives read as "terrain recovery owns this character".
void OwnershipNeedsEvidence()
{
    Check("Zug not falling is not proven below the world",
          ProvenBelowTheWorld(Measured(1830.7f, -3956.4f, 19.1f, 47.0f, false),
                              LIVE),
          false);
    Check("the same reading while falling is",
          ProvenBelowTheWorld(Measured(1830.7f, -3956.4f, 19.1f, 47.0f, true),
                              LIVE),
          true);
    TerrainReading floor = Measured(1830.7f, -3956.4f, 19.1f, 47.0f, true);
    floor.floorBelowValid = true;
    floor.floorBelowZ = 18.4f;
    Check("a floor within a stride is never below the world",
          ProvenBelowTheWorld(floor, LIVE), false);
    TerrainReading unmeasured = Measured(1830.7f, -3956.4f, 19.1f, 47.0f, false);
    unmeasured.movementMeasured = false;
    Check("an unmeasured reading keeps the old answer",
          ProvenBelowTheWorld(unmeasured, LIVE), true);
}

// 02:29:02. The leader the catch-up aim called below the world.
void TheLeaderAimReadsTheLeaderNotTheMissingReading()
{
    Check("Uzza at (1940.6, -3766.7, 55.0), not falling, no polygon: unmeasurable",
          ClassifyUnmeasuredLeaderGround(false, false) ==
              UnmeasuredLeaderGround::Unmeasurable,
          true);
    Check("not falling with a polygon at its feet: aim at the leader",
          ClassifyUnmeasuredLeaderGround(false, true) ==
              UnmeasuredLeaderGround::AimAtLeader,
          true);
    Check("falling: wait for it to land, polygon or not",
          ClassifyUnmeasuredLeaderGround(true, true) ==
              UnmeasuredLeaderGround::WaitForLanding,
          true);
}

}  // namespace

int main()
{
    ZugMidWalkIsNotLifted();
    ZorkClimbingThroughRockIsNotBelowTheWorld();
    TheOnTheGroundReadingsAreUnchanged();
    ASustainedFallIsTheOnlyOrdinaryLift();
    ARisingBodyIsNotFalling();
    AGapInThePollsRestartsTheWindow();
    TheVoidCatchIsUntouched();
    AnUnmeasuredReadingKeepsTheOldLadder();
    OwnershipNeedsEvidence();
    TheLeaderAimReadsTheLeaderNotTheMissingReading();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("layered ground is not the underside of the world\n");
    return EXIT_SUCCESS;
}
