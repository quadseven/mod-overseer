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
using OverseerDecisions::LargeSurfaceMismatchNeedsRecovery;
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
    250.f   // episodeRadius
};

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
        case TerrainRemedy::SendToBind:    return "SendToBind";
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
          v.remedy != TerrainRemedy::SendToBind &&
              v.remedy != TerrainRemedy::LiftToSurface,
          true);
}

// THE REGRESSION THIS WHOLE CHANGE EXISTS FOR. The condition that fired 204
// times, presented 204 times, must not produce 204 remedies.
void ARepeatedConditionIsABoundedSeriesAndThenSilence()
{
    TerrainRecoveryState state;
    int lifts = 0, binds = 0, giveUps = 0, nothings = 0;
    // Every 14 seconds, which was the measured walk-back interval, for the
    // 396 minutes the live worldserver ran.
    for (time_t t = 0; t < 396 * 60; t += 14)
    {
        TerrainRecoveryVerdict const v = TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, t);
        switch (v.remedy)
        {
            case TerrainRemedy::LiftToSurface: ++lifts; break;
            case TerrainRemedy::SendToBind:    ++binds; break;
            case TerrainRemedy::GiveUp:        ++giveUps; break;
            case TerrainRemedy::Nothing:       ++nothings; break;
        }
    }
    Check("at most one lift for an unbroken episode", lifts == 1, true);
    Check("at most one bind for an unbroken episode", binds == 1, true);
    Check("at most one give-up for an unbroken episode", giveUps == 1, true);
    Check("and silence for the rest of the six hours", nothings > 1600, true);
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
    // Fourteen seconds later it is back under the arch.
    CheckRemedy("back in the same condition fourteen seconds later",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 115).remedy,
                TerrainRemedy::SendToBind);
    CheckRemedy("and again after that",
                TerrainRecoveryStep(state, Here(88.6f, 116.8f, false), LIVE_LIMITS, 129).remedy,
                TerrainRemedy::GiveUp);
    CheckRemedy("and then it stops",
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
TerrainRecoveryVerdict Poll(TerrainRecoveryState& state, bool falling,
                            TerrainReading const& reading, time_t now)
{
    if (!TerrainRecoveryMayInspect(/*alive*/ true, /*teleporting*/ false,
                                   /*inFlight*/ false, /*flying*/ false,
                                   falling, /*inWater*/ false,
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
// map 43 that rung chose SendToBind for a completely unrelated incident. The
// bind teleport is what ejected him from the instance.
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
    Check("and above all it is not the bind point",
          v.remedy != TerrainRemedy::SendToBind, true);
}

// Distance ends an episode too, so a character thrown 1,900 yards by the
// fallback is not still inside the incident it was thrown out of. The radius
// has to stay larger than the 140-yard walk back from the bind point, or every
// repetition would look like a first occurrence and nothing would be bounded.
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
                TerrainRemedy::SendToBind);

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
    ARungDoesNotFollowACharacterToAnotherIncident();
    DistanceEndsAnEpisodeButAWalkBackDoesNot();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the below-terrain recovery decision and its remedy hold\n");
    return EXIT_SUCCESS;
}
