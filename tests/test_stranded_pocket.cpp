/*
 * A step down must also be a step back, and a polygon nobody can walk off is
 * not ground anybody is standing on. mod-overseer#262.
 *
 * WHAT WAS MEASURED. Four of the five family members stood at map 1
 * (-605.64, -2106.66, 44.97), beside the Wailing Caverns approach ramp, for
 * the whole of two 12 minute staging attempts, and could not move at all.
 * Read off the navmesh tile the core itself loads, at their exact x and y:
 *
 *   - the ONLY navmesh surface is at z 79.39, which is 34.4 yards above their
 *     heads, and it is in a different connected component from the dungeon
 *     door;
 *   - the nearest navmesh vertex of any component is 3.8 yards away in three
 *     dimensions, 1.4 along and 3.5 UP, and it is an isolated patch;
 *   - the nearest ground genuinely connected to the door is 11.3 yards away,
 *     7.7 along and 8.3 up, a gradient of 1.08 against the module's one
 *     measured walking gradient of 20 in 60.
 *
 * The travel drive said, once per errand, that "there is no direction out of
 * where it stands that does not step off something". The recovery drive said
 * that Detour "finds walkable ground at its own feet (local navmesh PRESENT),
 * so it is STANDING ON THE GROUND" and moved nobody. Three staging corrections
 * were applied and none of them helped.
 *
 * TWO RULES COME OUT OF THAT AND THEY ARE DIFFERENT RULES. The footing check
 * approved descents whose climb back it would refuse, which is how a character
 * gets into a pocket at all; and the recovery drive read "a polygon is nearby"
 * as "this character is standing on the ground", which is why nothing came to
 * get it out. This file pins both.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FootingSampleHolds;
using OverseerDecisions::StandingOnTheGround;
using OverseerDecisions::TerrainReading;
using OverseerDecisions::TerrainRecoveryLimits;
using OverseerDecisions::TerrainRecoveryState;
using OverseerDecisions::TerrainRecoveryStep;
using OverseerDecisions::TerrainRecoveryVerdict;
using OverseerDecisions::TerrainRemedy;

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

char const* Name(TerrainRemedy r)
{
    switch (r)
    {
        case TerrainRemedy::Nothing:       return "Nothing";
        case TerrainRemedy::LiftToSurface: return "LiftToSurface";
        case TerrainRemedy::GiveUp:        return "GiveUp";
    }
    return "?";
}

void CheckRemedy(char const* what, TerrainRemedy got, TerrainRemedy want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

// The adapter's own two bounds: TRAVEL_GROUND_DROP_YARDS, ten because ten is
// under MIN_FALL_DMG_DIST and so costs no health, and TRAVEL_GROUND_RISE_YARDS,
// eight because more than eight over one four-yard stride is a rock face
// rather than a slope.
constexpr float DROP = 10.f;
constexpr float RISE = 8.f;

// ---------------------------------------------------------------------------
// THE ONE WAY DOOR
// ---------------------------------------------------------------------------

// The whole defect in one assertion. Every stride the check approves has to be
// a stride it would approve in reverse, or walking it is a trap.
void EveryApprovedStrideIsApprovedBackwards()
{
    // Tenths of a yard, well past both bounds in both directions.
    for (int tenths = -200; tenths <= 200; ++tenths)
    {
        float const toZ = 100.f + static_cast<float>(tenths) / 10.f;
        bool const forward = FootingSampleHolds(100.f, toZ, DROP, RISE);
        bool const back = FootingSampleHolds(toZ, 100.f, DROP, RISE);
        if (forward == back)
            continue;
        std::printf("FAIL stride to z %.1f is walkable %s but its reverse is %s\n",
                    static_cast<double>(toZ), forward ? "yes" : "no",
                    back ? "yes" : "no");
        ++failures;
        return;
    }
    Check("every approved stride is approved backwards", true, true);
}

// The exact asymmetry that was there before, named so a future change that
// reintroduces it fails here rather than in a staging attempt. Nine yards down
// used to pass, because the drop bound was ten; nine yards up never could,
// because the rise bound is eight.
void ANineYardDescentIsRefusedBecauseTheClimbBackWouldBe()
{
    Check("nine yards down", FootingSampleHolds(100.f, 91.f, DROP, RISE), false);
    Check("nine yards up", FootingSampleHolds(100.f, 109.f, DROP, RISE), false);
    Check("the two answers now agree",
          FootingSampleHolds(100.f, 91.f, DROP, RISE) ==
              FootingSampleHolds(100.f, 109.f, DROP, RISE),
          true);
}

// The bound is the smaller of the pair, and it is an existing measured number
// rather than a new one. Eight is still comfortably under the height at which
// the core starts charging for a fall, so a drop this approves is still free.
void TheBoundIsTheSmallerOfThePairInBothDirections()
{
    Check("exactly eight down", FootingSampleHolds(100.f, 92.f, DROP, RISE), true);
    Check("exactly eight up", FootingSampleHolds(100.f, 108.f, DROP, RISE), true);
    Check("a shade over eight down",
          FootingSampleHolds(100.f, 91.9f, DROP, RISE), false);
    Check("a shade over eight up",
          FootingSampleHolds(100.f, 108.1f, DROP, RISE), false);
    Check("flat ground", FootingSampleHolds(100.f, 100.f, DROP, RISE), true);
}

// Ordinary walking is not what this bounds. A four-yard stride that gains or
// loses a yard or two is every hillside in the world and has to stay walkable.
void OrdinaryGroundIsStillWalkable()
{
    Check("a gentle rise", FootingSampleHolds(44.97f, 46.2f, DROP, RISE), true);
    Check("a gentle fall", FootingSampleHolds(46.2f, 44.97f, DROP, RISE), true);
    Check("a steep but walkable ramp",
          FootingSampleHolds(16.8f, 22.5f, DROP, RISE), true);
}

// The pocket's own numbers. The way out is 8.3 yards up over 7.7 along, which
// is two strides of the sampled step, so the surface moves about 4.15 yards
// per stride: walkable in both directions, and the reason the pocket is not
// escaped is the 34 yards of overhead geometry rather than this bound. The
// point of the assertion is that the fix does NOT claim to have made the climb
// out illegal; it makes the descent into it symmetric with the climb, which is
// a rule about every pocket and not about this one.
void TheMeasuredWayOutIsNotWhatThisBoundRefuses()
{
    Check("one stride of the measured climb out",
          FootingSampleHolds(44.97f, 49.12f, DROP, RISE), true);
    Check("the whole 34 yard rise to the only surface at their feet",
          FootingSampleHolds(44.97f, 79.39f, DROP, RISE), false);
}

// ---------------------------------------------------------------------------
// A POLYGON NOBODY CAN WALK OFF
// ---------------------------------------------------------------------------

void APolygonIsOnlyGroundWhileSomeDirectionHolds()
{
    Check("a polygon and a way out of here",
          StandingOnTheGround(true, true), true);
    Check("a polygon and no direction out of here",
          StandingOnTheGround(true, false), false);
}

// The correction only ever runs one way. No polygon stays no polygon however
// well the footing holds, so this can never invent an "on the ground" Detour
// did not report, and a caller that cannot measure footing keeps the old
// behaviour exactly.
void FootingNeverInventsAPolygon()
{
    Check("no polygon, footing holds", StandingOnTheGround(false, true), false);
    Check("no polygon, no footing", StandingOnTheGround(false, false), false);
    TerrainReading unmeasured;
    Check("a reading nobody measured footing for defaults to holding",
          unmeasured.footingHolds, true);
}

// ---------------------------------------------------------------------------
// THE DRIVE, ON THE READINGS THAT WERE ACTUALLY TAKEN
// ---------------------------------------------------------------------------

TerrainRecoveryLimits const LIVE_LIMITS{
    10.f,   // minimumGap, as the adapter passes
    25.f,   // overrideGap, as the adapter passes
    0.5f,   // liftClearance
    600,    // forgetSeconds
    250.f   // episodeRadius
};

// Map 1 (-605.64, -2106.66, 44.97), the only surface at those coordinates at
// z 79.39, and Detour reporting a polygon because an isolated patch sits 3.5
// yards over their heads.
TerrainReading Pocket(bool footingHolds)
{
    TerrainReading r;
    r.mapId = 1;
    r.x = -605.64f;
    r.y = -2106.66f;
    r.z = 44.97f;
    r.surfaceAboveZ = 79.39f;
    r.surfaceValid = true;
    r.hasLocalNavmesh = true;
    r.footingHolds = footingHolds;
    return r;
}

// What the drive did on the night, and what it should have done. The two calls
// differ in one bool and nothing else.
void ThePocketBesideTheRampIsRecoveredRatherThanCongratulated()
{
    {
        TerrainRecoveryState state;
        TerrainRecoveryVerdict const v =
            TerrainRecoveryStep(state, Pocket(true), LIVE_LIMITS, 1000);
        CheckRemedy("a polygon and somewhere to step is still left alone",
                    v.remedy, TerrainRemedy::GiveUp);
        Check("and still moves nobody",
              v.remedy != TerrainRemedy::LiftToSurface, true);
    }
    {
        TerrainRecoveryState state;
        TerrainRecoveryVerdict const v =
            TerrainRecoveryStep(state, Pocket(false), LIVE_LIMITS, 1000);
        CheckRemedy("no direction out of the pocket", v.remedy,
                    TerrainRemedy::LiftToSurface);
        // 79.39 plus the half yard of clearance, so the character lands ON the
        // surface rather than inside it.
        Check("lifted onto the only surface at its own x and y",
              v.liftZ > 79.88f && v.liftZ < 79.90f, true);
    }
}

// A stranding is 12 minutes of nothing followed by a backstop. One poll a
// second for those twelve minutes must produce one lift, one give-up and then
// silence, exactly like every other episode: the fix is a correction to a
// reading, not a new licence to move people.
void TwelveMinutesInThePocketIsStillABoundedLadder()
{
    TerrainRecoveryState state;
    int lifts = 0, giveUps = 0, nothings = 0;
    for (time_t t = 0; t < 12 * 60; ++t)
    {
        switch (TerrainRecoveryStep(state, Pocket(false), LIVE_LIMITS, t).remedy)
        {
            case TerrainRemedy::LiftToSurface: ++lifts; break;
            case TerrainRemedy::GiveUp:        ++giveUps; break;
            case TerrainRemedy::Nothing:       ++nothings; break;
        }
    }
    Check("one lift for the whole staging attempt", lifts == 1, true);
    Check("one give-up for the whole staging attempt", giveUps == 1, true);
    Check("and silence for the rest of it", nothings > 700, true);
}

// THE REGRESSION GUARD, and it is the important half. #188 removed 204
// recoveries in six hours by trusting a polygon at the feet; 79 of those were
// one 6-by-7 yard patch of the Northshire road under an arch, and one was a
// character standing beside a vendor it went on to sell ten items to. Those
// characters could all walk. The footing fan says so, and they stay untouched.
void TheArchOnTheNorthshireRoadIsStillNotAFallThroughTheWorld()
{
    TerrainReading arch;
    arch.mapId = 0;
    arch.x = -9057.f;
    arch.y = -47.f;
    arch.z = 88.6f;
    arch.surfaceAboveZ = 116.8f;   // the arch overhead
    arch.surfaceValid = true;
    arch.hasLocalNavmesh = true;
    arch.footingHolds = true;      // it is standing on a road

    TerrainRecoveryState state;
    int lifts = 0;
    for (time_t t = 0; t < 396 * 60; t += 14)
        if (TerrainRecoveryStep(state, arch, LIVE_LIMITS, t).remedy ==
            TerrainRemedy::LiftToSurface)
            ++lifts;
    Check("nobody on the Northshire road is ever lifted", lifts == 0, true);

    TerrainReading vendor = arch;
    vendor.x = -10504.7f;
    vendor.y = 1035.7f;
    vendor.z = 60.5f;
    vendor.surfaceAboveZ = 97.9f;   // the Sentinel Hill tower floor
    TerrainRecoveryState vendorState;
    CheckRemedy("the vendor under the tower is still never displaced",
                TerrainRecoveryStep(vendorState, vendor, LIVE_LIMITS, 1000).remedy,
                TerrainRemedy::GiveUp);
    Check("and it is not a lift",
          TerrainRecoveryStep(vendorState, vendor, LIVE_LIMITS, 1001).remedy !=
              TerrainRemedy::LiftToSurface,
          true);
}

// An unreadable surface still authorizes nothing, however the footing reads.
// Invalid data grants no permission to move a character, and adding a second
// input must not have opened a way round that.
void AnUnknownSurfaceStillMovesNobody()
{
    TerrainReading r = Pocket(false);
    r.surfaceValid = false;
    r.surfaceAboveZ = -200000.f;
    TerrainRecoveryState state;
    CheckRemedy("no surface reading, no direction out",
                TerrainRecoveryStep(state, r, LIVE_LIMITS, 1000).remedy,
                TerrainRemedy::Nothing);
}

// A recovery may never change a map, and this one does not: it is the same
// map, the same x and the same y, only higher. Asserted here as well as in
// test_terrain_recovery.cpp because this file adds a new way to REACH the
// lift, and the closure has to hold on the new path too.
void ThePocketLiftIsStillTheSameMapAndTheSameXAndY()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, Pocket(false), LIVE_LIMITS, 1000);
    CheckRemedy("the pocket remedy", v.remedy, TerrainRemedy::LiftToSurface);
    Check("a lift carries a height and nothing else",
          v.liftZ > Pocket(false).z, true);
}

}  // namespace

int main()
{
    EveryApprovedStrideIsApprovedBackwards();
    ANineYardDescentIsRefusedBecauseTheClimbBackWouldBe();
    TheBoundIsTheSmallerOfThePairInBothDirections();
    OrdinaryGroundIsStillWalkable();
    TheMeasuredWayOutIsNotWhatThisBoundRefuses();

    APolygonIsOnlyGroundWhileSomeDirectionHolds();
    FootingNeverInventsAPolygon();

    ThePocketBesideTheRampIsRecoveredRatherThanCongratulated();
    TwelveMinutesInThePocketIsStillABoundedLadder();
    TheArchOnTheNorthshireRoadIsStillNotAFallThroughTheWorld();
    AnUnknownSurfaceStillMovesNobody();
    ThePocketLiftIsStillTheSameMapAndTheSameXAndY();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a step down is a step back, and a polygon nobody can walk "
                "off is not ground\n");
    return EXIT_SUCCESS;
}
