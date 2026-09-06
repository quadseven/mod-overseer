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
    Check("at most one lift for an unbroken episode", lifts == 1, true);
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

    TheBarrensLadderEndsInAGiveUpAndNotAnOcean();
    TheAbbeyRoofIsNotACharacterUnderStormwind();
    NoRecoveryMayEverChangeAMap();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the below-terrain recovery decision and its remedy hold\n");
    return EXIT_SUCCESS;
}
