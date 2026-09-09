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
using OverseerDecisions::FloorUnderfoot;
using OverseerDecisions::LargeSurfaceMismatchNeedsRecovery;
using OverseerDecisions::MeasuredToBeFalling;
using OverseerDecisions::NearTheVoidPlane;
using OverseerDecisions::ReadingStandsOnTheGround;
using OverseerDecisions::SomeInstrumentFoundGround;
using OverseerDecisions::VOID_PLANE_Z;
using OverseerDecisions::TerrainRecoveryMayInspect;
using OverseerDecisions::TerrainReading;
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

void TheMeasuredGapIsRecovered()
{
    Check("35 yards below the surface without a navmesh",
          BelowTerrainNeedsRecovery(60.f, 95.f, true, false, 10.f), true);
}

void DeliberatelyAirborneStatesAreLeftAlone()
{
    Check("ordinary living character is inspectable",
          TerrainRecoveryMayInspect(true, false, false, false, false, false, false,
                                    false), true);
    Check("dead character", TerrainRecoveryMayInspect(
              false, false, false, false, false, false, false, false), false);
    Check("teleport in progress", TerrainRecoveryMayInspect(
              true, true, false, false, false, false, false, false), false);
    Check("taxi flight", TerrainRecoveryMayInspect(
              true, false, true, false, false, false, false, false), false);
    Check("free flight", TerrainRecoveryMayInspect(
              true, false, false, true, false, false, false, false), false);
    Check("falling", TerrainRecoveryMayInspect(
              true, false, false, false, true, false, false, false), false);
    Check("swimming", TerrainRecoveryMayInspect(
              true, false, false, false, false, true, false, false), false);
    Check("transport", TerrainRecoveryMayInspect(
              true, false, false, false, false, false, true, false), false);
    Check("vehicle", TerrainRecoveryMayInspect(
              true, false, false, false, false, false, false, true), false);
}

// #323. THE STAND-DOWN ABOVE KEEPS ITS REASON AND STOPS ASKING THE FLAG.
//
// A scripted shaft drop is a real descent and is still stood down for. A
// character standing still under the terrain with a flag nothing will ever
// clear is not, and that is the population 48 of the 50 sampled void deaths
// came from. The fall baseline guard already tells these apart every poll;
// this is that answer, named once so the mask and the gate cannot drift.
void AMeasuredDescentIsStillAFall()
{
    // The guard looked, the core would charge, and it declined to put the
    // baseline back under the feet - which it only declines to do for a
    // character it measured losing height faster than a walk.
    Check("a real descent", MeasuredToBeFalling(true, false, false), true);
    Check("and it stands the recovery drive down",
          TerrainRecoveryMayInspect(true, false, false, false,
                                    MeasuredToBeFalling(true, false, false),
                                    false, false, false), false);
}

void AStuckFlagOverAStandingCharacterIsNotAFall()
{
    // The guard measured 1.63 and 1.88 yards per second on the phantom deaths
    // (#281) and rebased. That is a walk, so the drive gets to look - which is
    // the whole of this change.
    Check("the baseline went back under its feet",
          MeasuredToBeFalling(true, false, true), false);
    Check("so the recovery drive is allowed to look at it",
          TerrainRecoveryMayInspect(true, false, false, false,
                                    MeasuredToBeFalling(true, false, true),
                                    false, false, false), true);
}

// AN ABSENCE OF EVIDENCE IS NOT EVIDENCE. A guard that did not look measured
// nothing, and nothing is not a fall - the same asymmetry #291 chose when it
// decided an unknown must let the guard RUN rather than stand it down.
void AGuardThatDidNotLookMeasuredNoFall()
{
    Check("the guard stood down, so there is no measurement",
          MeasuredToBeFalling(false, false, false), false);
    Check("and a rebase it never made is not one either",
          MeasuredToBeFalling(false, false, true), false);
    // Every state that stops the guard looking is already a stand-down of the
    // recovery's own, so this cannot open a gate those keep shut.
    Check("a swimmer is still declined by its own reason",
          TerrainRecoveryMayInspect(true, false, false, false,
                                    MeasuredToBeFalling(false, false, false),
                                    true, false, false), false);
    Check("and so is a corpse",
          TerrainRecoveryMayInspect(false, false, false, false,
                                    MeasuredToBeFalling(false, false, false),
                                    false, false, false), false);
}

// A character the core will not charge for a fall has no measurement behind
// the rebase, because the guard returns before taking one.
void AFreeFallIsNotAMeasuredOne()
{
    Check("hover, feather fall or a fly aura",
          MeasuredToBeFalling(true, true, false), false);
    Check("and the rebase flag says nothing either way there",
          MeasuredToBeFalling(true, true, true), false);
}

// AND THE GATE ITSELF IS UNCHANGED. This decides what to PASS as `falling`; it
// does not alter what TerrainRecoveryMayInspect does with it, which is what
// keeps the eight arguments lined up with the stand-down mask.
void TheGateStillDeclinesOnEveryFlagItAlwaysDid()
{
    Check("a fall stands the drive down",
          TerrainRecoveryMayInspect(true, false, false, false, true, false, false,
                                    false), false);
    Check("and nothing wrong lets it look",
          TerrainRecoveryMayInspect(true, false, false, false, false, false, false,
                                    false), true);
}

void TheBoundaryIsARecovery()
{
    Check("exactly the declared gap",
          BelowTerrainNeedsRecovery(60.f, 70.f, true, false, 10.f), true);
}

void AnUnknownSurfaceSaysNothing()
{
    Check("invalid surface reading",
          BelowTerrainNeedsRecovery(60.f, 95.f, false, false, 10.f), false);
}

void AnOrdinaryHeightDifferenceIsLeftAlone()
{
    Check("sub-threshold gap",
          BelowTerrainNeedsRecovery(60.f, 69.f, true, false, 10.f), false);
    Check("surface below the character",
          BelowTerrainNeedsRecovery(60.f, 40.f, true, false, 10.f), false);
}

void ARealInteriorHasAPathAndIsLeftAlone()
{
    Check("cave or building with a local navmesh",
          BelowTerrainNeedsRecovery(60.f, 95.f, true, true, 10.f), false);
}

void ALargeMismatchOverridesMisleadingPolygon()
{
    Check("large gap with misleading lower polygon",
          LargeSurfaceMismatchNeedsRecovery(60.f, 95.f, true, true, 25.f), true);
    Check("ordinary interior gap keeps its polygon",
          LargeSurfaceMismatchNeedsRecovery(80.f, 95.f, true, true, 25.f), false);
    Check("unknown surface never authorizes recovery",
          LargeSurfaceMismatchNeedsRecovery(60.f, 95.f, false, true, 25.f), false);
}


// ---------------------------------------------------------------------------
// THE LIVE LOOP, 2026-09-05. 204 recoveries in 396 minutes of one worldserver,
// one every 1.9 minutes, all five family members, for the whole uptime. The
// readings below are transcribed from those log lines and not invented; the
// tests underneath pin what they should have meant.
// ---------------------------------------------------------------------------

OverseerDecisions::TerrainRecoveryLimits const LIVE_LIMITS{
    10.f,   // minimumGap, as the adapter passes
    25.f,   // overrideGap, as the adapter passes
    0.5f,   // liftClearance
    600,    // forgetSeconds
    250.f,  // episodeRadius
    2.f,    // footingReach, as the adapter passes (#296)
    250.f   // voidCatchYards, as the adapter passes (#188)
};

// THE READING EVERY CASE BELOW USES UNLESS IT SAYS OTHERWISE: no floor was
// found under the character. That is what the void, a hole in the terrain and
// a drop of more than fifty yards all look like from the adapter probe, and it
// is the reading under which every test written before #296 was measured. The
// new guard declines nothing on it, so those cases still assert exactly what
// they always asserted.
TerrainReading Floorless(TerrainReading r)
{
    r.floorBelowValid = false;
    return r;
}

// A floor at the character own feet, `drop` yards below them. A positive drop
// puts it under the character; a negative one puts the reported surface just
// above the reported position, which is what the adapter probe does on level
// ground because it searches from the feet plus a collision height.
TerrainReading OnAFloor(TerrainReading r, float drop)
{
    r.floorBelowValid = true;
    r.floorBelowZ = r.z - drop;
    return r;
}

// The live incidents all happened at real coordinates, and the episode rules
// now read them, so the tests name a place. `Here` is one arbitrary spot that
// several cases share when the place is not the point; the cases about place
// name their own.
TerrainReading Here(float z, float surfaceAboveZ, bool hasLocalNavmesh,
                    uint32_t mapId = 0, float x = -9058.3f, float y = -45.4f)
{
    TerrainReading r;
    r.mapId = mapId;
    r.x = x;
    r.y = y;
    r.z = z;
    r.surfaceAboveZ = surfaceAboveZ;
    r.surfaceValid = true;
    r.hasLocalNavmesh = hasLocalNavmesh;
    return r;
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

void CheckNear(char const* what, float got, float want)
{
    float const d = got - want;
    if ((d < 0.f ? -d : d) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

// 'Og' recovered at map 0 position (-9058.3, -45.4, 88.6), surface z 116.8.
// The Northshire road under its arch; 79 of the 204 landed inside a 6-by-7
// yard patch there, all five characters, over six hours.
void TheArchOnTheNorthshireRoadIsNotAFallThroughTheWorld()
{
    // Detour found ground at the character's own feet. It is walking under an
    // arch, and the only correct thing to do with it is nothing.
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(88.6f, 116.8f, true), LIVE_LIMITS, 1000);
    CheckRemedy("z 88.6, surface 116.8, WITH a local polygon", v.remedy,
                TerrainRemedy::GiveUp);

    // And it is said exactly once. The old rule moved this character 79 times.
    for (time_t t = 1001; t < 1100; ++t)
    {
        TerrainRecoveryVerdict const again = TerrainRecoveryStep(state, Here(88.6f, 116.8f, true), LIVE_LIMITS, t);
        CheckRemedy("the same arch, one poll later", again.remedy,
                    TerrainRemedy::Nothing);
    }
}

// The same shape with the navmesh answer the log claimed it had. A character
// with no polygon under it goes UP, to the surface at its own x and y, and
// keeps its errand.
void NoPolygonIsLiftedToTheSurfaceAboveIt()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 1000);
    CheckRemedy("z 88.6, surface 116.8, no local navmesh", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("the lift lands just above the surface it read", v.liftZ, 117.3f);
}

// 'Grug' recovered at (-10504.7, 1035.7, 60.5), surface z 97.9, ten seconds
// after "sent to 'vendor' - creature 491 at 39 yards" and two minutes before
// it sold ten items to that same vendor. It was standing next to the NPC,
// under the Sentinel Hill tower, on ground it demonstrably could walk.
void TheVendorUnderTheTowerIsNeverDisplaced()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(60.5f, 97.9f, true), LIVE_LIMITS, 1000);
    CheckRemedy("standing at a vendor under a 37-yard tower", v.remedy,
                TerrainRemedy::GiveUp);
    Check("a live polygon is never displaced",
          v.remedy != TerrainRemedy::LiftToSurface, true);
}

// THE REGRESSION THIS WHOLE CHANGE EXISTS FOR. The condition that fired 204
// times, presented 204 times, must not produce 204 remedies.
//
// AND THE BOUND IS A RATE, NOT A TOTAL (#188). This used to assert exactly one
// lift for the whole six hours, which is what killed four characters on
// 2026-09-09: the condition never goes clear for a character genuinely under
// the world, so the forget window never opened and "one lift ever" was the
// real policy. The rate is what #188 was actually about - 204 remedies in 396
// minutes, one every 1.9 minutes - so the rate is what is pinned. One per ten
// minutes is a fifth of the loop that had to be stopped, and every one of
// those 204 was a character standing on the ground, which the gate above now
// declines before a rung is ever reached.
void ARepeatedConditionIsABoundedSeriesAndThenSilence()
{
    TerrainRecoveryState state;
    int lifts = 0, giveUps = 0, nothings = 0;
    // Every 14 seconds, which was the measured walk-back interval, for the
    // 396 minutes the live worldserver ran.
    for (time_t t = 0; t < 396 * 60; t += 14)
    {
        TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, t);
        switch (v.remedy)
        {
            case TerrainRemedy::LiftToSurface: ++lifts; break;
            case TerrainRemedy::GiveUp:        ++giveUps; break;
            case TerrainRemedy::Nothing:       ++nothings; break;
        }
    }
    // 396 minutes, one ladder per ten of them, so at most forty of each and
    // nowhere near the 204 that had to be stopped.
    Check("the series is bounded well under the live loop", lifts <= 40, true);
    Check("and the give-up never outruns the lift", giveUps <= lifts, true);
    Check("and it is silent between the ladders", nothings > 1600, true);
    // AND IT DOES COME BACK, which is the half the old assertion forbade. A
    // character under the world for six hours is offered a remedy through all
    // six, not once at the start and then never again.
    Check("the ladder is re-armed rather than retired", lifts > 20, true);
}

// The memory must survive the clean poll that every remedy itself produces,
// or the ladder never leaves its first rung and the series is unbounded again
// with extra steps.
void ARemedyThatDidNotStickClimbsRatherThanRepeating()
{
    TerrainRecoveryState state;
    CheckRemedy("first occurrence",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 100).remedy,
                TerrainRemedy::LiftToSurface);
    // The lift moved it, so the very next poll is clean. That is not the end
    // of the episode.
    CheckRemedy("the poll right after the lift",
                TerrainRecoveryStep(state, Here(117.3f, 117.3f, true), LIVE_LIMITS, 101).remedy,
                TerrainRemedy::Nothing);
    // Fourteen seconds later it is back under the arch. The lift did not
    // stick, so this module cannot fix this character where it stands, and
    // that is where the ladder ENDS: it used to escalate to the bind point
    // here, which is #188.
    CheckRemedy("back in the same condition fourteen seconds later",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 115).remedy,
                TerrainRemedy::GiveUp);
    CheckRemedy("and then it stops",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 129).remedy,
                TerrainRemedy::Nothing);
    CheckRemedy("and stays stopped",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 143).remedy,
                TerrainRemedy::Nothing);
}

// A character that really was fine for a long time gets the full ladder again.
// The bound is on an episode, not on a character's whole life.
void AQuietSpellEndsTheEpisode()
{
    TerrainRecoveryState state;
    CheckRemedy("first occurrence",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 100).remedy,
                TerrainRemedy::LiftToSurface);
    // Ten minutes of nothing wrong.
    for (time_t t = 101; t <= 100 + 600; ++t)
        TerrainRecoveryStep(state, Here(117.3f, 117.3f, true), LIVE_LIMITS, t);
    CheckRemedy("a genuinely new episode starts at the first rung",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 100 + 601).remedy,
                TerrainRemedy::LiftToSurface);
}

// Invalid data grants no permission to move a character, and above all no
// permission to lift one to a sentinel height. Both of the core's invalid
// height values are far below any floor in the world, so a lift computed from
// one would drop the character further than the condition claimed it had
// already fallen.
void AnUnknownSurfaceNeverProducesALift()
{
    TerrainRecoveryState state;
    TerrainReading unknown = Here(88.6f, -200000.f, false);
    unknown.surfaceValid = false;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, unknown, LIVE_LIMITS, 1000);
    CheckRemedy("invalid surface reading", v.remedy, TerrainRemedy::Nothing);
    CheckNear("and no height is offered with it", v.liftZ, 0.f);
}

// An ordinary character on open ground is not touched and keeps no memory.
void AnOrdinaryCharacterIsLeftAloneAndForgotten()
{
    TerrainRecoveryState state;
    CheckRemedy("surface two yards above the character",
                TerrainRecoveryStep(state, Here(60.f, 62.f, true), LIVE_LIMITS, 1000).remedy,
                TerrainRemedy::Nothing);
    Check("nothing is remembered about it", state.attempts == 0u, true);
}

// The original incident, #174: the party alive at z 59-61 under a city surface
// around z 95, with no walkable polygon at their height. That still recovers,
// and now it recovers by going up to the floor it was under rather than to a
// bind point on the other side of the map.
void TheOriginalCityIncidentStillRecovers()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(60.f, 95.f, false), LIVE_LIMITS, 1000);
    CheckRemedy("35 yards below a city floor with no polygon", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("lifted onto that floor", v.liftZ, 95.5f);
}

// A caller that passes no forget window gets the old unbounded behaviour, and
// has to have written a zero to get it. Pinned so that a future reader can see
// that the bound lives in the limits and not in a hidden default.
void AZeroForgetWindowIsTheOldUnboundedBehaviourAndSaysSo()
{
    OverseerDecisions::TerrainRecoveryLimits noMemory = LIVE_LIMITS;
    noMemory.forgetSeconds = 0;
    TerrainRecoveryState state;
    int lifts = 0;
    for (time_t t = 0; t < 100; t += 2)
    {
        if (TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), noMemory, t)
                .remedy == TerrainRemedy::LiftToSurface)
            ++lifts;
        // The remedy works for exactly one poll, as every remedy does.
        TerrainRecoveryStep(state, Here(117.3f, 117.3f, true), noMemory, t + 1);
    }
    Check("a zero forget window forgets every episode immediately",
          lifts == 50, true);
}

// A warning about an arch is not a remedy, so it must not use up the lift a
// real fall-through would need a minute later.
void AWarningDoesNotSpendTheLiftARealFallWouldNeed()
{
    TerrainRecoveryState state;
    CheckRemedy("walking under the arch",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, true), LIVE_LIMITS, 100).remedy,
                TerrainRemedy::GiveUp);
    // Sixty seconds later, at the same place, with no polygon under it.
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 160);
    CheckRemedy("and then it really does go under the world", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("still lifted to the surface, not sent anywhere", v.liftZ, 117.3f);
}


// ---------------------------------------------------------------------------
// THE SCRIPTED FALL, Wailing Caverns, 2026-09-05. mod-dungeon-clear drops the
// party down a shaft as a measured traversal step. One second into the drop
// this module moved the tank, and one second after that the other four logged
// "follow-tank: released (DC tank gone)".
//
//   14:25:27  [dungeon-clear] Grug DropInHole: MoveFall from (-49.5,47.6,-29.0)
//   14:25:28  overseer: 'Grug' ... map 43 position (-49.5, 47.6, -39.8),
//             surface z 6.6, no local navmesh
//
// Same x and y, 10.8 yards below the lip it left, one second in.
// ---------------------------------------------------------------------------

// The adapter's own composition, in the order mod_overseer.cpp asks it: may
// this module have an opinion at all, and only then what the readings mean.
// Written out here so the live shape can be asked as ONE question - what does
// this module DO about a character in this state - which is the question that
// was got wrong.
//
// THE FIFTH ARGUMENT IS `descending && !nearThePlane` (#188), which is what
// the adapter now passes. A measured descent stands this drive down
// everywhere except the last stretch above the kill plane, where standing
// down is the death: six characters fell through that stand-down on
// 2026-09-08/09 and the core killed every one of them at full health. The
// other seven stand-downs are untouched and are still asked here in the same
// order.
TerrainRecoveryVerdict Poll(TerrainRecoveryState& state, bool falling,
                            TerrainReading const& reading, time_t now)
{
    bool const nearThePlane =
        NearTheVoidPlane(reading.z, LIVE_LIMITS.voidCatchYards);
    if (!TerrainRecoveryMayInspect(/*alive*/ true, /*teleporting*/ false,
                                   /*inFlight*/ false, /*flying*/ false,
                                   falling && !nearThePlane, /*inWater*/ false,
                                   /*onTransport*/ false, /*onVehicle*/ false))
        return TerrainRecoveryVerdict{};
    return TerrainRecoveryStep(state, reading, LIVE_LIMITS, now);
}

// The shaft in Wailing Caverns: map 43, the lip at (-49.5, 47.6, -29.0) and
// the floor at -105.83, with the surface probe answering 6.6 from far above.
TerrainReading Shaft(float z, bool hasLocalNavmesh = false)
{
    return Here(z, 6.6f, hasLocalNavmesh, 43, -49.5f, 47.6f);
}

void AScriptedFallIsNeverRecovered()
{
    // The live readings, one second into DropInHole.
    TerrainRecoveryState falling;
    CheckRemedy("mid-fall down the Wailing Caverns shaft",
                Poll(falling, true, Shaft(-39.8f), 1000).remedy,
                TerrainRemedy::Nothing);
    Check("and nothing is remembered about a state we may not judge",
          falling.attempts == 0u && !falling.saidOnGround, true);

    // The whole fall, at the poll cadence, all the way down to the floor at
    // -105.83. Not one of them may produce a remedy.
    TerrainRecoveryState whole;
    for (float z = -29.0f; z > -105.83f; z -= 10.8f)
        CheckRemedy("every second of the drop",
                    Poll(whole, true, Shaft(z), 1000).remedy,
                    TerrainRemedy::Nothing);

    // AND THE GUARD IS THE ONLY THING STANDING THERE. The same readings from a
    // character that is NOT falling still get the lift, so this is a stand-down
    // on the character's state and not a quiet weakening of the rule.
    TerrainRecoveryState standing;
    TerrainRecoveryVerdict const v = Poll(standing, false, Shaft(-39.8f), 1000);
    CheckRemedy("the same readings, not falling", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("lifted to the surface as before", v.liftZ, 7.1f);
}

void EveryDeliberatelyAirborneStateStandsDown()
{
    Check("ordinary living character is inspectable",
          TerrainRecoveryMayInspect(true, false, false, false, false, false, false, false),
          true);
    Check("falling", TerrainRecoveryMayInspect(
              true, false, false, false, true, false, false, false), false);
}

// THE OTHER HALF OF THE SAME INCIDENT, and the reason the tank got the bind
// point rather than a lift. The ladder had a rung left over from a lift on map
// 1 at 13:48:14 (-594.3, -2014.6, 61.0), and 37 minutes later at 14:25:28 on
// map 43 that rung chose the bind-point fallback for a completely unrelated
// incident, which is what ejected him from the instance. That fallback is gone
// (#188) and the anchor still matters: what a leftover rung now steals is the
// LIFT a real fall is entitled to.
void ARungDoesNotFollowACharacterToAnotherIncident()
{
    // The lift that set the rung: map 1, 13:48:14.
    TerrainRecoveryState state;
    CheckRemedy("the lift that sets the rung",
                TerrainRecoveryStep(state, Here(61.0f, 80.5f, false, 1, -594.3f,
                                                -2014.6f),
                                    LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);

    // The condition holds continuously from then on, so nothing this rule can
    // measure about TIME will end the episode. That is the trap: an episode
    // that only ends on a clean poll cannot end where there are none, and a
    // cave is such a place.
    for (time_t t = 1; t < 2220; ++t)
        TerrainRecoveryStep(state, Here(15.4f, 45.9f, true, 1, -594.3f, -2014.6f),
                            LIVE_LIMITS, t);

    // 14:25:28, map 43. A different map is a different incident, so this gets
    // the first rung - a LIFT - and never the bind point that ejected the tank.
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, Shaft(-39.8f), LIVE_LIMITS, 2220);
    CheckRemedy("a new map is a new incident", v.remedy,
                TerrainRemedy::LiftToSurface);
}

// Distance ends an episode too. Nothing this module does moves a character in
// x or y any more, so what this reads is the character's own wandering, and the
// radius has to stay larger than the 140-yard walk back measured from the bind
// point, or every repetition would look like a first occurrence and nothing
// would be bounded.
void DistanceEndsAnEpisodeButAWalkBackDoesNot()
{
    TerrainRecoveryState nearby;
    CheckRemedy("the first occurrence",
                TerrainRecoveryStep(nearby, Here(88.6f, 116.8f, false),
                                    LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);
    // The measured walk back from the leader's bind point was 140 yards.
    CheckRemedy("a 140-yard walk back is the SAME incident",
                TerrainRecoveryStep(nearby, Here(88.6f, 116.8f, false, 0, -9058.3f,
                                                 -185.4f),
                                    LIVE_LIMITS, 14).remedy,
                TerrainRemedy::GiveUp);

    TerrainRecoveryState distant;
    CheckRemedy("the first occurrence",
                TerrainRecoveryStep(distant, Here(88.6f, 116.8f, false),
                                    LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);
    CheckRemedy("but a thousand yards away is a different one",
                TerrainRecoveryStep(distant, Here(88.6f, 116.8f, false, 0, -8058.3f,
                                                  -45.4f),
                                    LIVE_LIMITS, 14).remedy,
                TerrainRemedy::LiftToSurface);
}

// ---------------------------------------------------------------------------
// #188, THE CROSS-CONTINENT ESCALATION. Captured whole on the dev realm
// 2026-09-05, one character, sixteen seconds:
//
//   16:46:58  'Grog' below the world at map 1 (1202.6, -707.3, 72.3),
//             surface z 97.7, no local navmesh; LIFTED to z 98.2
//   16:47:03  'Grog' the condition is back, so this is the fallback:
//             sent to the leader's bind point
//   16:47:14  'Grog' STILL below the world at MAP 0 (-8902.6, -162.6, 81.9),
//             surface z 128.0, local navmesh PRESENT
//
// Every roster bind row is map 0 (-8950, -132). The party ended that minute
// two in Elwynn, two in the Barrens and one offline in Stonetalon.
// ---------------------------------------------------------------------------

// The northern Barrens readings, transcribed. The second occurrence used to be
// the bind teleport; it is now the end of the ladder.
void TheBarrensLadderEndsInAGiveUpAndNotAnOcean()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const lift =
        TerrainRecoveryStep(state, Here(72.3f, 97.7f, false, 1, 1202.6f, -707.3f),
                            LIVE_LIMITS, 0);
    CheckRemedy("16:46:58, the first occurrence", lift.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("lifted to the surface at its own x and y", lift.liftZ, 98.2f);

    // 16:47:03. Five seconds later, one and a half yards away, same condition.
    TerrainRecoveryVerdict const second =
        TerrainRecoveryStep(state, Here(73.1f, 95.8f, false, 1, 1204.1f, -708.5f),
                            LIVE_LIMITS, 5);
    CheckRemedy("16:47:03, the lift did not stick", second.remedy,
                TerrainRemedy::GiveUp);
    Check("and it is not a displacement of any kind",
          second.remedy != TerrainRemedy::LiftToSurface, true);
}

// The far end of that teleport, which is the whole of #188's title. Grog reads
// as 46 yards below a surface at Northshire, WITH a live polygon at his feet -
// so he is standing on the ground and the probe found the abbey roof. It is
// also the bind point's own neighbourhood, which is why the fallback fed the
// detector that chose it.
void TheAbbeyRoofIsNotACharacterUnderStormwind()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, Here(81.9f, 128.0f, true, 0, -8902.6f, -162.6f),
                            LIVE_LIMITS, 1000);
    CheckRemedy("16:47:14, map 0, local navmesh PRESENT", v.remedy,
                TerrainRemedy::GiveUp);
    Check("a character on a live polygon is never moved",
          v.remedy != TerrainRemedy::LiftToSurface, true);
    // And it does not spend a rung, so a real fall here still gets its lift.
    Check("a warning is not a remedy", state.attempts == 0u, true);
}

// ---------------------------------------------------------------------------
// #296. THE DETECTOR ONLY EVER LOOKED UP.
//
// Every reading above is about the sixty yards over a character head or about
// what Detour makes of the neighbourhood, and "no local navmesh" went straight
// to the lift with no second opinion at all. Two lifts on 2026-09-07 prove
// that branch wrong on its own terms, because the give-up on the very next
// rung reports the position the lift started from:
//
//   03:50:29  'Grug' below the world at map 0 (-3827.9, -831.9, 10.1),
//             surface z 26.1, no local navmesh; LIFTED to z 26.6
//   03:50:36  'Grug' STILL below the world at map 0 (-3828.1, -831.9, 10.1),
//             surface z 26.1
//
//   04:26:55  'Grug' below the world at map 0 (-4895.6, -1004.7, 503.9),
//             surface z 515.0; LIFTED to z 515.5
//   04:26:57  'Grug' STILL below the world at map 0 (-4895.4, -1004.7, 503.9),
//             surface z 514.8
//
// Same x, same y, same z, to a tenth of a yard, seconds apart. The character
// was lifted off a floor and fell back onto it. There is ground at its feet in
// both, and nothing on the reading could see it.
// ---------------------------------------------------------------------------

void AFloorUnderTheFeetIsReadAsAFloorAndARoofIsNot()
{
    Check("a floor half a yard down", FloorUnderfoot(10.1f, 9.6f, true, 2.f),
          true);
    Check("a floor exactly at the feet",
          FloorUnderfoot(10.1f, 10.1f, true, 2.f), true);
    // The adapter probes from the feet PLUS a collision height, so on level
    // ground it can answer slightly above the reported position. That is a
    // floor underfoot and not an absence of one.
    Check("a surface a foot ABOVE the feet, which the probe can return",
          FloorUnderfoot(10.1f, 10.4f, true, 2.f), true);
    // And the bound is on the magnitude for exactly this reason: a surface far
    // overhead is the false positive this whole rule is about, and a predicate
    // that accepted one would have re-implemented the bug it fixes.
    Check("the Ironforge ceiling is not a floor",
          FloorUnderfoot(503.9f, 551.6f, true, 2.f), false);
    Check("ground thirty yards down is something it is falling toward",
          FloorUnderfoot(60.f, 30.f, true, 2.f), false);
    Check("no floor found is not a floor",
          FloorUnderfoot(10.1f, 9.6f, false, 2.f), false);
    Check("a reach of zero is a caller not asking",
          FloorUnderfoot(10.1f, 9.6f, true, 0.f), false);
}

void EitherInstrumentIsEnoughAndNeitherIsRequired()
{
    TerrainReading const bare = Floorless(Here(10.1f, 26.1f, false));
    Check("no polygon and no floor is not on the ground",
          ReadingStandsOnTheGround(bare, 2.f), false);
    Check("a floor alone is enough",
          ReadingStandsOnTheGround(OnAFloor(bare, 0.1f), 2.f), true);

    TerrainReading const meshed = Floorless(Here(88.6f, 116.8f, true));
    Check("a walkable polygon alone is still enough",
          ReadingStandsOnTheGround(meshed, 2.f), true);

    // #262 stands: a polygon nobody can step off is not ground. With no floor
    // under it this is still the pocket beside the Wailing Caverns ramp.
    TerrainReading pocket = Floorless(Here(44.97f, 79.4f, true));
    pocket.footingHolds = false;
    Check("a polygon no direction can be walked off is not ground",
          ReadingStandsOnTheGround(pocket, 2.f), false);
}

// 03:50:29. No local navmesh, sixteen yards under something, and a floor at
// its feet. Before #296 this was a lift.
//
// AND THE ANSWER IS SILENCE, NOT A WARNING, which is not a weaker result. A
// gap under the 25-yard override on a character that is on the ground has
// never held the condition at all - that is what the Detour branch has always
// done with the same reading, and the give-up sentence exists for the ones
// large enough to be worth a person going to look. The property under test is
// the one the deaths care about: nothing is moved.
void TheLiftThatFellStraightBackOntoItsOwnFloorIsDeclined()
{
    TerrainReading const there =
        Here(10.1f, 26.1f, false, 0, -3827.9f, -831.9f);
    TerrainRecoveryState before;
    CheckRemedy("03:50:29 as it was read, with no floor probe at all",
                TerrainRecoveryStep(before, Floorless(there), LIVE_LIMITS, 0)
                    .remedy,
                TerrainRemedy::LiftToSurface);

    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, OnAFloor(there, 0.1f), LIVE_LIMITS, 0);
    Check("03:50:29, a floor at its feet: nothing is moved",
          v.remedy != TerrainRemedy::LiftToSurface, true);
    CheckRemedy("and a sixteen-yard gap on the ground is not worth a line",
                v.remedy, TerrainRemedy::Nothing);
    // A reading that does not hold spends no rung, so a character that really
    // does fall through the world a minute later still gets the whole ladder.
    Check("and it spends no rung", state.attempts == 0u, true);
}

// 04:26:55, inside Ironforge, eleven yards under the ceiling.
void TheIronforgeFloorIsNotACharacterUnderIronforge()
{
    TerrainReading const there =
        Here(503.9f, 515.0f, false, 0, -4895.6f, -1004.7f);
    TerrainRecoveryState before;
    CheckRemedy("04:26:55 as it was read",
                TerrainRecoveryStep(before, Floorless(there), LIVE_LIMITS, 0)
                    .remedy,
                TerrainRemedy::LiftToSurface);

    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, OnAFloor(there, 0.0f), LIVE_LIMITS, 0);
    Check("04:26:55, standing on the Ironforge floor: nothing is moved",
          v.remedy != TerrainRemedy::LiftToSurface, true);
    Check("and it spends no rung", state.attempts == 0u, true);
}

// 04:05:19, a Stormwind street fifty-two yards under something. This one is
// past the override, so it IS worth a line, and it is the case #262 could not
// hold: Detour reported a polygon, no bearing out of it passed the footing
// fan, and the override lifted the character anyway.
//
//   04:05:19  'Grog' below the world at map 0 (-8626.1, -143.9, 86.4),
//             surface z 138.9 (52.5 up), a local polygon, but no direction out
//             of here holds (#262); LIFTED straight up to z 139.4
//
// A floor at its feet settles it: the thing fifty-two yards up is a roof.
void TheStormwindStreetUnderABridgeIsSaidOutLoudAndNotLifted()
{
    TerrainReading there = Here(86.4f, 138.9f, true, 0, -8626.1f, -143.9f);
    there.footingHolds = false;

    TerrainRecoveryState before;
    CheckRemedy("04:05:19 as it was read, the override beating #262",
                TerrainRecoveryStep(before, Floorless(there), LIVE_LIMITS, 0)
                    .remedy,
                TerrainRemedy::LiftToSurface);

    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, OnAFloor(there, 0.2f), LIVE_LIMITS, 0);
    CheckRemedy("04:05:19, a floor at its feet under a bridge", v.remedy,
                TerrainRemedy::GiveUp);
    Check("and nothing is moved", v.remedy != TerrainRemedy::LiftToSurface,
          true);
    // The same rule the Detour answer follows: a warning is not a remedy.
    Check("a warning does not spend the lift rung", state.attempts == 0u, true);
}

// THE OTHER HALF, AND THE ONE THAT MATTERS MORE. This guard is only worth
// having if it still lets a real fall be recovered, so the case it must NOT
// take is a character genuinely under the world with nothing beneath it.
//
//   03:56:49  'Grug' below the world at map 0 (-4776.7, -901.7, 427.7),
//             surface z 483.2 (55.4 up), no local navmesh; LIFTED to z 483.7
//
// 427.7 is seventy-five yards below the Ironforge floor. The probe finds no
// floor in reach, so the reading declines nothing and the ladder runs.
void ACharacterWithNothingUnderItStillGetsItsLift()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(
        state, Floorless(Here(427.7f, 483.2f, false, 0, -4776.7f, -901.7f)),
        LIVE_LIMITS, 0);
    CheckRemedy("03:56:49, genuinely below the world", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("lifted to the surface at its own x and y", v.liftZ, 483.7f);
}

// AND A FLOOR IT IS ALREADY FALLING PAST IS NOT A FLOOR IT IS STANDING ON.
// The reach is a stride on purpose: a guard that reached far enough to find
// the ground a falling character is heading for would decline every recovery
// there has ever been.
void GroundFarBelowDoesNotDeclineARecovery()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(
        state, OnAFloor(Here(60.f, 95.f, false, 0, -9058.3f, -45.4f), 30.f),
        LIVE_LIMITS, 0);
    CheckRemedy("the original city incident, with terrain thirty yards down",
                v.remedy, TerrainRemedy::LiftToSurface);
}

// A ZERO REACH IS THE PRE-#296 BEHAVIOUR, and it has to be writable for the
// same reason a zero forget window is: a caller turning the guard off should
// have to say so rather than discover it.
void AZeroFootingReachIsTheOldBehaviourAndSaysSo()
{
    OverseerDecisions::TerrainRecoveryLimits before = LIVE_LIMITS;
    before.footingReach = 0.f;
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(
        state, OnAFloor(Here(10.1f, 26.1f, false, 0, -3827.9f, -831.9f), 0.1f),
        before, 0);
    CheckRemedy("with the guard off, the floor is invisible and it lifts",
                v.remedy, TerrainRemedy::LiftToSurface);
}

// THE INVARIANT, ASKED THE WAY THE INCIDENT ASKS IT. The adapter turns a
// verdict into a position: a lift keeps the map, the x and the y and changes
// only z; anything else changes nothing. Drive the ladder through the whole
// live trace and assert the map id never moves. This is the property the enum
// now makes unrepresentable, checked at the level a party split cares about.
struct Where
{
    uint32_t mapId;
    float x, y, z;
};

Where Apply(Where at, TerrainRecoveryVerdict const& v)
{
    if (v.remedy == TerrainRemedy::LiftToSurface)
        at.z = v.liftZ;   // same map, same x, same y: that is what a lift IS
    return at;
}

void NoRecoveryMayEverChangeAMap()
{
    // The northern Barrens cluster, every reading the incident logged, run
    // over and over with the condition never going false.
    float const zs[] = {40.9f, 21.4f, 59.6f, 72.3f, 73.1f};
    float const surfaces[] = {86.3f, 58.6f, 112.4f, 97.7f, 95.8f};
    float const xs[] = {1161.7f, 1146.4f, 1140.4f, 1202.6f, 1204.1f};
    float const ys[] = {-633.1f, -627.2f, -638.0f, -707.3f, -708.5f};

    TerrainRecoveryState state;
    Where at{1, xs[0], ys[0], zs[0]};
    int moves = 0;
    for (time_t t = 0; t < 4000; ++t)
    {
        size_t const i = static_cast<size_t>(t) % 5;
        at.x = xs[i];
        at.y = ys[i];
        at.z = zs[i];
        // Alternate the navmesh answer so both branches are exercised.
        TerrainReading const r =
            Here(zs[i], surfaces[i], (t % 3) == 0, at.mapId, at.x, at.y);
        Where const after = Apply(at, TerrainRecoveryStep(state, r, LIVE_LIMITS, t));
        Check("a recovery never changes the map", after.mapId == at.mapId, true);
        Check("a recovery never changes x", after.x == at.x, true);
        Check("a recovery never changes y", after.y == at.y, true);
        if (after.z != at.z)
            ++moves;
        at = after;
    }
    // And it did really exercise the moving branch, so the three checks above
    // are not passing because nothing ever happened.
    Check("the lift did fire during that run", moves > 0, true);
}

// ---------------------------------------------------------------------------
// #188, 2026-09-09. THE GIVE-UP WAS AN ABANDONMENT AND IT KILLED FOUR.
//
// Three hours on the dev realm: 47 below-the-world reports, 5 escalations to
// "OUT OF REMEDIES ... GIVING UP until it has been clear for 600s", and 4
// deaths recorded as crossing the void plane. The five give-up coordinates,
// all map 1:
//
//   (  412.5, -2117.4, 147.6)  surface z 185.0   37.3 under
//   (  252.9, -2253.9, 205.2)  surface z 217.6   12.4 under
//   ( 1396.5, -2797.9, 119.7)  surface z 140.8   21.1 under
//   ( 1370.0, -2823.6, 191.2)  surface z 202.7   11.5 under
//   ( -747.8, -2056.5, 106.8)  surface z 127.4   20.6 under
//
// The last is about 40 yards from (-705.0, -2045.0, 66.45), the approach point
// the Wailing Caverns staging corridor ends at, so this sits on the campaign's
// own route rather than somewhere the family has no business being.
// ---------------------------------------------------------------------------

// The give-up's own sentence promised a 600 second window, and for the one
// population it was ever said about that window could not open: `lastHeld` is
// refreshed by every poll the condition is true on, and the condition is
// permanently true under the world. So "giving up until it has been clear"
// meant "giving up".
void TheGiveUpIsACooldownAndNotAnAbandonment()
{
    // (412.5, -2117.4, 147.6), surface z 185.0. The largest of the five.
    TerrainReading const there =
        Floorless(Here(147.6f, 185.0f, false, 1, 412.5f, -2117.4f));

    TerrainRecoveryState state;
    CheckRemedy("the first occurrence gets its lift",
                TerrainRecoveryStep(state, there, LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);
    CheckRemedy("the lift did not stick, so the ladder ends",
                TerrainRecoveryStep(state, there, LIVE_LIMITS, 14).remedy,
                TerrainRemedy::GiveUp);

    // Ten minutes of the condition being true every second, which is what a
    // character actually under the world produces. Not one poll of it may be
    // a remedy, and not one of them may push the window ahead of itself.
    int remedies = 0;
    for (time_t t = 15; t < 614; ++t)
        if (TerrainRecoveryStep(state, there, LIVE_LIMITS, t).remedy !=
            TerrainRemedy::Nothing)
            ++remedies;
    Check("it stays quiet for the whole window", remedies == 0, true);

    // AND THEN IT COMES BACK. This is the assertion the four deaths bought.
    CheckRemedy("ten minutes after the give-up, the ladder is re-armed",
                TerrainRecoveryStep(state, there, LIVE_LIMITS, 614).remedy,
                TerrainRemedy::LiftToSurface);
}

// The window is measured from the last REMEDY and not from the last time the
// condition held, which is the whole of the bug. A rule keyed on `lastHeld`
// re-reads a permanently true condition as permanently recent.
void TheWindowIsMeasuredFromTheLastRemedyAndNotTheLastHold()
{
    TerrainReading const there =
        Floorless(Here(106.8f, 127.4f, false, 1, -747.8f, -2056.5f));

    TerrainRecoveryState state;
    TerrainRecoveryStep(state, there, LIVE_LIMITS, 0);    // lift
    TerrainRecoveryStep(state, there, LIVE_LIMITS, 14);   // give-up
    // Every single poll for ten minutes holds, so `lastHeld` is always now.
    // A window keyed on it can never elapse, which is what happened live.
    for (time_t t = 15; t < 614; ++t)
        TerrainRecoveryStep(state, there, LIVE_LIMITS, t);
    // Under the old rule this character was still out of remedies here, and
    // stayed that way for as long as it lived.
    CheckRemedy("a continuously true condition still gets another ladder",
                TerrainRecoveryStep(state, there, LIVE_LIMITS, 614).remedy,
                TerrainRemedy::LiftToSurface);
}

// The Wailing Caverns approach, which is why this is a campaign blocker and
// not an occasional loss: the staging corridor ends at (-705.0, -2045.0) and
// this give-up is 40 yards from it, so the family walks members past here
// every run by design.
void TheGiveUpOnTheCampaignRouteIsRetriedRatherThanLeft()
{
    TerrainReading const there =
        Floorless(Here(106.8f, 127.4f, false, 1, -747.8f, -2056.5f));
    TerrainRecoveryState state;
    int lifts = 0;
    // One hour of standing under the world beside the approach point.
    for (time_t t = 0; t < 3600; ++t)
        if (TerrainRecoveryStep(state, there, LIVE_LIMITS, t).remedy ==
            TerrainRemedy::LiftToSurface)
            ++lifts;
    Check("an hour under the world is six chances and not one", lifts == 6,
          true);
}

// A warning about an arch is not a remedy, so the re-arm has nothing to give
// back there. Repeating that line every ten minutes would have cost a reader
// 79 lines a night at one Northshire coordinate and bought them nothing.
void TheReArmGivesBackTheRemedyAndNotTheWarning()
{
    TerrainRecoveryState state;
    CheckRemedy("the arch, said once",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, true),
                                    LIVE_LIMITS, 0).remedy,
                TerrainRemedy::GiveUp);
    int said = 0;
    for (time_t t = 1; t < 3600; ++t)
        if (TerrainRecoveryStep(state, Here(88.6f, 116.8f, true), LIVE_LIMITS, t)
                .remedy != TerrainRemedy::Nothing)
            ++said;
    Check("and never said again for the whole hour", said == 0, true);
}

// ---------------------------------------------------------------------------
// #188 / #262. TWO CHARACTERS THIRTY YARDS APART, OPPOSITE VERDICTS.
//
//   'Grog' at (1396.5, -2797.9, 119.7), surface z 140.8, a local polygon but
//          no direction out of here holds. OUT OF REMEDIES, and then dead.
//   'Og'   at (1392.9, -2823.3, 110.1), surface z 141.7, ground at its own
//          feet. STANDING ON THE GROUND, nothing is being moved.
//
// Detour reported a polygon for both. The footing fan is the only instrument
// that separated them, and the fan answers a question about walking.
// ---------------------------------------------------------------------------

void AFootingRefusalIsNotEvidenceThatTheGroundIsMissing()
{
    // Grog. A polygon Detour found, and no bearing out of it that holds.
    TerrainReading grog =
        Floorless(Here(119.7f, 140.8f, true, 1, 1396.5f, -2797.9f));
    grog.footingHolds = false;
    Check("the #262 correction still takes the 'on the ground' away",
          ReadingStandsOnTheGround(grog, 2.f), false);
    Check("but an instrument did find ground here",
          SomeInstrumentFoundGround(grog, 2.f), true);

    // Og, thirty yards away, with the fan holding.
    TerrainReading const og =
        Floorless(Here(110.1f, 141.7f, true, 1, 1392.9f, -2823.3f));
    Check("and its neighbour reads as on the ground",
          ReadingStandsOnTheGround(og, 2.f), true);
    Check("by the same instrument", SomeInstrumentFoundGround(og, 2.f), true);

    // A character genuinely under the world: neither instrument carries it,
    // so the loud sentence is still available for the case it was written for.
    TerrainReading const void_ =
        Floorless(Here(427.7f, 483.2f, false, 0, -4776.7f, -901.7f));
    Check("nothing found ground for a character under the world",
          SomeInstrumentFoundGround(void_, 2.f), false);

    // A floor alone carries it, with no Detour answer at all.
    Check("a floor at the feet is an instrument finding ground",
          SomeInstrumentFoundGround(
              OnAFloor(Floorless(Here(10.1f, 26.1f, false)), 0.1f), 2.f),
          true);
}

// THE ASYMMETRY, PINNED. This can only ever be weaker than the corrected
// answer, never stronger: anything the corrected rule calls on the ground,
// this one does too. A version that could disagree the other way would be a
// second, quieter "on the ground" test, which is exactly the drift the fold
// on ReadingStandsOnTheGround exists to prevent.
void TheUncorrectedAnswerIsNeverStrongerThanTheCorrectedOne()
{
    for (int mesh = 0; mesh < 2; ++mesh)
        for (int footing = 0; footing < 2; ++footing)
            for (int floor = 0; floor < 2; ++floor)
            {
                TerrainReading r = Here(88.6f, 116.8f, mesh != 0);
                r.footingHolds = footing != 0;
                r.floorBelowValid = floor != 0;
                r.floorBelowZ = r.z - 0.1f;
                if (ReadingStandsOnTheGround(r, 2.f))
                    Check("on the ground implies an instrument found ground",
                          SomeInstrumentFoundGround(r, 2.f), true);
            }
}

// And Grog still gets the ladder. Suppressing the give-up outright would put
// this character back in #262's pocket, where four sat for 24 minutes while
// the module congratulated them. What must not happen is the abandonment.
void TheCharacterTheDetectorIsWrongAboutStillGetsItsLift()
{
    TerrainReading grog =
        Floorless(Here(119.7f, 140.8f, true, 1, 1396.5f, -2797.9f));
    grog.footingHolds = false;

    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v =
        TerrainRecoveryStep(state, grog, LIVE_LIMITS, 0);
    CheckRemedy("a pocket is still worth a lift", v.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("straight up, at its own x and y", v.liftZ, 141.3f);
    CheckRemedy("and the ladder still ends in a give-up",
                TerrainRecoveryStep(state, grog, LIVE_LIMITS, 14).remedy,
                TerrainRemedy::GiveUp);
    CheckRemedy("which expires like every other one",
                TerrainRecoveryStep(state, grog, LIVE_LIMITS, 614).remedy,
                TerrainRemedy::LiftToSurface);
}

// ---------------------------------------------------------------------------
// #188. THE KILL PLANE, AND THE WINDOW BEFORE IT.
//
// Six void deaths on map 1, 2026-09-08/09, one character, killer 'self', every
// one at full health and out of combat:
//
//   time      x      y       z at death   last sampled z
//   03:57:14  1400   -2800   -528         -287
//   03:51:26  1397   -2798   -528         -468
//   03:44:37   418   -2121   -509         -359
//   02:37:54  1541   -2411   -506         -265
//   23:13:00  -792   -2037   -524         -313
//   23:08:23  -597   -2292   -517         -366
//
// The last sampled column is the point: there is a poll at which the character
// is hundreds of yards below the world and STILL ALIVE. The module was
// standing down through every one of them.
// ---------------------------------------------------------------------------

void TheBandIsTheLastStretchAboveThePlaneAndNothingElse()
{
    Check("the plane itself", NearTheVoidPlane(VOID_PLANE_Z, 250.f), true);
    Check("below the plane, not yet killed", NearTheVoidPlane(-528.f, 250.f),
          true);
    Check("the top of the band is inclusive", NearTheVoidPlane(-250.f, 250.f),
          true);
    Check("and one yard above it is not in it",
          NearTheVoidPlane(-249.f, 250.f), false);
    // Every one of the six last-sampled positions is inside the band, which is
    // what the depth was chosen to guarantee: the worst measured fall between
    // two polls was 241 yards.
    float const lastSampled[] = {-287.f, -468.f, -359.f, -265.f, -313.f, -366.f};
    for (float z : lastSampled)
        Check("the last sample before a void death is inside the band",
              NearTheVoidPlane(z, 250.f), true);
    // AND NOTHING THE ROSTER LEGITIMATELY STANDS ON IS. The deepest measured
    // floor is the bottom of the Wailing Caverns shaft mod-dungeon-clear drops
    // the party down.
    Check("the bottom of the scripted shaft is not in the band",
          NearTheVoidPlane(-105.83f, 250.f), false);
    Check("nor the lip it is dropped from", NearTheVoidPlane(-29.0f, 250.f),
          false);
    Check("nor anywhere overland", NearTheVoidPlane(119.7f, 250.f), false);
    Check("a zero band is a caller not asking",
          NearTheVoidPlane(-468.f, 0.f), false);
}

// 03:51:26. The character was at -468 with the terrain at its own x and y
// around 140. The ordinary sixty-yard probe finds nothing from there, which is
// why there was never a height to lift it to; the adapter's deep probe is what
// supplies this reading, and this is what the rule does with it.
void ACharacterFallingOutOfTheWorldIsCaughtAboveThePlane()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(
        state, Floorless(Here(-468.f, 140.8f, false, 1, 1397.f, -2798.f)),
        LIVE_LIMITS, 0);
    CheckRemedy("caught above the plane", v.remedy, TerrainRemedy::LiftToSurface);
    CheckNear("onto the surface at its own x and y", v.liftZ, 141.3f);
}

// AND THE LADDER'S BOUND DOES NOT APPLY DOWN THERE. This is the shape that
// actually happened: the character reaches the give-up standing under the
// world, and then falls. Under the old rule it was out of remedies before the
// fall began.
void ASpentLadderStillCatchesTheFall()
{
    TerrainReading const above =
        Floorless(Here(119.7f, 140.8f, false, 1, 1396.5f, -2797.9f));
    TerrainRecoveryState state;
    CheckRemedy("the lift", TerrainRecoveryStep(state, above, LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);
    CheckRemedy("the give-up",
                TerrainRecoveryStep(state, above, LIVE_LIMITS, 14).remedy,
                TerrainRemedy::GiveUp);
    CheckRemedy("and nothing while it stands there",
                TerrainRecoveryStep(state, above, LIVE_LIMITS, 20).remedy,
                TerrainRemedy::Nothing);

    // It falls. Same x and y, so the episode and its spent ladder are intact.
    TerrainRecoveryVerdict const caught = TerrainRecoveryStep(
        state, Floorless(Here(-359.f, 140.8f, false, 1, 1396.5f, -2797.9f)),
        LIVE_LIMITS, 25);
    CheckRemedy("an out-of-remedies character is still caught", caught.remedy,
                TerrainRemedy::LiftToSurface);
    CheckNear("onto the surface at its own x and y", caught.liftZ, 141.3f);
}

// ONE CATCH PER ENTRY INTO THE BAND, which is the whole of that rung's bound.
// A catch that worked leaves the band before the next poll, because the core's
// gravity covers under ten yards in the first second of a fall. A catch that
// moved nothing does not, and must not become a teleport a second.
void TheCatchFiresOncePerEntryIntoTheBand()
{
    TerrainReading const deep =
        Floorless(Here(-359.f, 140.8f, false, 1, 1396.5f, -2797.9f));

    TerrainRecoveryState state;
    CheckRemedy("the catch", TerrainRecoveryStep(state, deep, LIVE_LIMITS, 0).remedy,
                TerrainRemedy::LiftToSurface);
    // It did not move: a hundred more polls at the same depth.
    int again = 0;
    for (time_t t = 1; t < 100; ++t)
        if (TerrainRecoveryStep(state, deep, LIVE_LIMITS, t).remedy !=
            TerrainRemedy::Nothing)
            ++again;
    Check("a catch that changed nothing is not repeated", again == 0, true);

    // It worked: one poll on the surface, and the character is out of the
    // band. The next fall is a new entry and gets its own catch.
    TerrainRecoveryStep(state, Floorless(Here(131.f, 140.8f, true, 1, 1396.5f,
                                              -2797.9f)),
                        LIVE_LIMITS, 100);
    CheckRemedy("a second fall is caught again",
                TerrainRecoveryStep(state, deep, LIVE_LIMITS, 101).remedy,
                TerrainRemedy::LiftToSurface);
}

// THE ORDERING THAT KEEPS THE CATCH FROM BEING A LICENCE. A character with a
// floor at its own feet at that depth is standing on a legitimate deep
// interior, and the on-the-ground gate is read BEFORE the band.
void ACharacterStandingOnADeepFloorIsNeverCaught()
{
    TerrainRecoveryState state;
    TerrainRecoveryVerdict const v = TerrainRecoveryStep(
        state, OnAFloor(Here(-359.f, 140.8f, false, 1, 1396.5f, -2797.9f), 0.1f),
        LIVE_LIMITS, 0);
    Check("a floor at the feet beats the band",
          v.remedy != TerrainRemedy::LiftToSurface, true);
    Check("and no catch is remembered against it", !state.caughtBelow, true);
}

// AND THE SCRIPTED DROP IS UNTOUCHED, which is the stand-down this band is
// carved out of. mod-dungeon-clear's shaft runs from -29.0 to -105.83, so no
// part of it is inside the band and the drive's own falling stand-down still
// covers all of it.
void TheScriptedShaftDropIsNowhereNearTheBand()
{
    for (float z = -29.0f; z > -105.83f; z -= 10.8f)
        Check("no second of the Wailing Caverns drop is in the band",
              NearTheVoidPlane(z, 250.f), false);
    // So the drive still declines it, exactly as before, through the whole
    // drop and not only at its start.
    TerrainRecoveryState falling;
    for (float z = -29.0f; z > -105.83f; z -= 10.8f)
        CheckRemedy("and the stand-down still holds through the whole drop",
                    Poll(falling, true, Shaft(z), 1000).remedy,
                    TerrainRemedy::Nothing);
}

// AND THE STAND-DOWN IS WHAT WAS KILLING THEM, asked through the adapter's own
// composition so the two halves of the change are tested together: a character
// measured falling, which this drive has always declined to look at, inside the
// band, which is the one place declining is fatal.
void AFallingCharacterInsideTheBandIsLookedAtAndCaught()
{
    TerrainRecoveryState state;
    TerrainReading const deep =
        Floorless(Here(-359.f, 140.8f, false, 1, 418.f, -2121.f));

    // Before this change the fifth argument was `descending` alone, so the
    // drive returned here without ever taking a reading.
    Check("the drive is allowed to look inside the band",
          TerrainRecoveryMayInspect(true, false, false, false,
                                    /*descending && !nearThePlane*/ false,
                                    false, false, false),
          true);
    CheckRemedy("and it catches the fall", Poll(state, true, deep, 0).remedy,
                TerrainRemedy::LiftToSurface);

    // The same character at the same instant, one yard above the band, is
    // still declined. The band is the whole of what changed.
    TerrainRecoveryState above;
    CheckRemedy("one yard above the band it is declined as before",
                Poll(above, true,
                     Floorless(Here(-249.f, 140.8f, false, 1, 418.f, -2121.f)), 0)
                    .remedy,
                TerrainRemedy::Nothing);
}

// THE INVARIANT AGAIN, THROUGH THE NEW RUNG. A catch is a lift and a lift is a
// change of z. Nothing #188 forbade is reachable from here.
void ACatchIsStillOnlyAChangeOfHeight()
{
    float const zs[] = {-287.f, -468.f, -359.f, -265.f, -313.f, -366.f};
    float const xs[] = {1400.f, 1397.f, 418.f, 1541.f, -792.f, -597.f};
    float const ys[] = {-2800.f, -2798.f, -2121.f, -2411.f, -2037.f, -2292.f};

    for (size_t i = 0; i < 6; ++i)
    {
        TerrainRecoveryState state;
        Where at{1, xs[i], ys[i], zs[i]};
        Where const after = Apply(
            at, TerrainRecoveryStep(
                    state, Floorless(Here(zs[i], 140.8f, false, 1, xs[i], ys[i])),
                    LIVE_LIMITS, 0));
        Check("a catch never changes the map", after.mapId == at.mapId, true);
        Check("a catch never changes x", after.x == at.x, true);
        Check("a catch never changes y", after.y == at.y, true);
        Check("and it did move the character upward", after.z > at.z, true);
    }
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
    ALargeMismatchOverridesMisleadingPolygon();

    TheArchOnTheNorthshireRoadIsNotAFallThroughTheWorld();
    NoPolygonIsLiftedToTheSurfaceAboveIt();
    TheVendorUnderTheTowerIsNeverDisplaced();
    ARepeatedConditionIsABoundedSeriesAndThenSilence();
    ARemedyThatDidNotStickClimbsRatherThanRepeating();
    AQuietSpellEndsTheEpisode();
    AnUnknownSurfaceNeverProducesALift();
    AnOrdinaryCharacterIsLeftAloneAndForgotten();
    TheOriginalCityIncidentStillRecovers();
    AWarningDoesNotSpendTheLiftARealFallWouldNeed();
    AZeroForgetWindowIsTheOldUnboundedBehaviourAndSaysSo();

    AScriptedFallIsNeverRecovered();
    EveryDeliberatelyAirborneStateStandsDown();

    AMeasuredDescentIsStillAFall();
    AStuckFlagOverAStandingCharacterIsNotAFall();
    AGuardThatDidNotLookMeasuredNoFall();
    AFreeFallIsNotAMeasuredOne();
    TheGateStillDeclinesOnEveryFlagItAlwaysDid();

    ARungDoesNotFollowACharacterToAnotherIncident();
    DistanceEndsAnEpisodeButAWalkBackDoesNot();

    TheBarrensLadderEndsInAGiveUpAndNotAnOcean();
    TheAbbeyRoofIsNotACharacterUnderStormwind();
    NoRecoveryMayEverChangeAMap();

    AFloorUnderTheFeetIsReadAsAFloorAndARoofIsNot();
    EitherInstrumentIsEnoughAndNeitherIsRequired();
    TheLiftThatFellStraightBackOntoItsOwnFloorIsDeclined();
    TheIronforgeFloorIsNotACharacterUnderIronforge();
    TheStormwindStreetUnderABridgeIsSaidOutLoudAndNotLifted();
    ACharacterWithNothingUnderItStillGetsItsLift();
    GroundFarBelowDoesNotDeclineARecovery();
    AZeroFootingReachIsTheOldBehaviourAndSaysSo();

    TheGiveUpIsACooldownAndNotAnAbandonment();
    TheWindowIsMeasuredFromTheLastRemedyAndNotTheLastHold();
    TheGiveUpOnTheCampaignRouteIsRetriedRatherThanLeft();
    TheReArmGivesBackTheRemedyAndNotTheWarning();

    AFootingRefusalIsNotEvidenceThatTheGroundIsMissing();
    TheUncorrectedAnswerIsNeverStrongerThanTheCorrectedOne();
    TheCharacterTheDetectorIsWrongAboutStillGetsItsLift();

    TheBandIsTheLastStretchAboveThePlaneAndNothingElse();
    ACharacterFallingOutOfTheWorldIsCaughtAboveThePlane();
    ASpentLadderStillCatchesTheFall();
    TheCatchFiresOncePerEntryIntoTheBand();
    ACharacterStandingOnADeepFloorIsNeverCaught();
    TheScriptedShaftDropIsNowhereNearTheBand();
    AFallingCharacterInsideTheBandIsLookedAtAndCaught();
    ACatchIsStillOnlyAChangeOfHeight();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the below-terrain recovery decision and its remedy hold\n");
    return EXIT_SUCCESS;
}
