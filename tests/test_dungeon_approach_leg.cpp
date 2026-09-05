/*
 * A door at the bottom of a ravine is reached by a corridor (mod-overseer#242).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * EVERY COORDINATE BELOW IS MEASURED, off the navmesh tiles the core's own
 * pathfinder reads. The Wailing Caverns staging point is the one the module
 * derives, (-733.71, -2214.91, 16.8179); it and the door sit on map 1 grid
 * 33/36, mmaps/0013336.mmtile. The rim height, 161.93, is the SECOND walkable
 * surface that same tile carries at the staging point's own x and y, and it is
 * the ground the party has stood on twice.
 *
 * The corridor's start, (-705.0, -2045.0, 66.45), is a terrace on the tile
 * NORTH of that one, grid 33/35, mmaps/0013335.mmtile - the corridor crosses
 * the tile boundary, which is why the descent was traced over a block of tiles
 * rather than one. The height is that tile's own lowest walkable surface at
 * that x and y; it carries a second one 45 yards above, so "lowest" is doing
 * work here.
 *
 * The gaps below are those points subtracted, not chosen:
 *
 *   corridor start -> staging point      172.32y out,  49.63y up, 179.32y away
 *   rim over the door -> staging point     0.00y out, 145.11y up, 145.11y away
 *   rim over the door -> corridor start  172.32y out,  95.48y up, 197.00y away
 *   Grug 2026-09-05 18:15 -> staging     497.09y out,  78.87y up, 503.30y away
 *   Grug 2026-09-05 18:15 -> corridor    600.38y out,  29.24y up, 601.10y away
 *
 * The rim being 34 yards NEARER the door than the corridor's start is is the
 * whole reason ApproachLegStep asks the shape and not only the distance.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::ApproachGap;
using OverseerDecisions::ApproachLeg;
using OverseerDecisions::ApproachLegStep;
using OverseerDecisions::ApproachLimits;
using OverseerDecisions::ApproachRoute;
using OverseerDecisions::ApproachRouteState;

namespace
{

int failures = 0;

// The adapter's own numbers: DUNGEON_BARRIER_RADIUS_YARDS, TRAVEL_STEP_YARDS
// and TRAVEL_STEP_VERTICAL_YARDS, the same triple test_dungeon_approach_shape
// writes down. No new threshold is introduced by this decision.
constexpr ApproachLimits LIMITS{10.f, 60.f, 20.f};

// The corridor's start against the staging point, in three dimensions.
constexpr float CORRIDOR_FROM_DOOR = 179.32f;

char const* Name(ApproachLeg leg)
{
    switch (leg)
    {
        case ApproachLeg::Direct:     return "Direct";
        case ApproachLeg::ToWaypoint: return "ToWaypoint";
    }
    return "?";
}

ApproachGap Gap(float horizontal, float vertical)
{
    ApproachGap gap;
    gap.horizontalYards = horizontal;
    gap.verticalYards = vertical;
    gap.measured = true;
    return gap;
}

ApproachRoute Route(ApproachGap const& toWaypoint, ApproachGap const& toStaging)
{
    ApproachRoute route;
    route.hasWaypoint = true;
    route.leaderToWaypoint = toWaypoint;
    route.leaderToStagingPoint = toStaging;
    route.waypointToStagingYards = CORRIDOR_FROM_DOOR;
    return route;
}

void Check(char const* what, ApproachLeg got, ApproachLeg want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// THE THREE DOORS THAT ALREADY WORK. Deadmines, Shadowfang Keep and Stockades
// carry no corridor, and a row with no corridor must behave exactly as it did
// before this decision existed - at every distance, including the ones that
// would otherwise be sent down a corridor.
void ADoorWithNoCorridorIsWalkedAtDirectly()
{
    ApproachRouteState state;
    ApproachRoute route;
    route.hasWaypoint = false;
    route.leaderToStagingPoint = Gap(1029.f, 22.f);
    Check("no corridor, 1029y out", ApproachLegStep(state, route, LIMITS),
          ApproachLeg::Direct);

    route.leaderToStagingPoint = Gap(99.f, 88.3f);
    Check("no corridor, on the Deadmines rim",
          ApproachLegStep(state, route, LIMITS), ApproachLeg::Direct);

    route.leaderToStagingPoint = Gap(0.f, 0.f);
    Check("no corridor, at the door", ApproachLegStep(state, route, LIMITS),
          ApproachLeg::Direct);

    CheckBool("and nothing was ever marked passed", state.waypointPassed, false);
}

// THE APPROACH THAT FAILED. Grug's live position at 18:15 on 2026-09-05, and
// the rim the party stood on at 18:03. Both must be sent down the corridor.
void TheWailingApproachGoesDownTheCorridor()
{
    ApproachRouteState fromTheRoad;
    Check("Grug 497y out and 79y up - out on the Barrens",
          ApproachLegStep(fromTheRoad,
                          Route(Gap(600.38f, 29.24f), Gap(497.09f, 78.87f)), LIMITS),
          ApproachLeg::ToWaypoint);
    CheckBool("and he has not passed the corridor", fromTheRoad.waypointPassed, false);

    // The live stall: 97 yards out and 152 above, ninety seconds without
    // getting nearer, which is what #228 closed the run on.
    ApproachRouteState onTheRim;
    Check("the live stall, 97y out and 152y up",
          ApproachLegStep(onTheRim, Route(Gap(190.f, 85.f), Gap(97.f, 152.f)), LIMITS),
          ApproachLeg::ToWaypoint);
    CheckBool("the rim is not the corridor", onTheRim.waypointPassed, false);
}

// THE CASE THE SHAPE GUARD EXISTS FOR, and the reason the distance test alone
// is not enough. The walkable surface directly over the door is 145 yards from
// the staging point in three dimensions; the corridor's start is 179. The rim
// is NEARER, by 34 yards, and it is the one place the corridor must not be
// skipped.
void TheRimIsNearerTheDoorAndStillNotPastTheCorridor()
{
    ApproachRouteState state;
    ApproachRoute const route = Route(Gap(172.32f, 95.48f), Gap(0.f, 145.11f));

    CheckBool("the rim really is nearer the door than the corridor is",
              CORRIDOR_FROM_DOOR > 145.11f, true);
    Check("nought yards out and 145 up is not past the corridor",
          ApproachLegStep(state, route, LIMITS), ApproachLeg::ToWaypoint);
    CheckBool("and it is not marked passed", state.waypointPassed, false);
}

// THE OTHER HALF OF THE SAME TEST. A party that is genuinely below and inside -
// on the ravine floor 75 yards from the door, which is ground a walk covers -
// is past the corridor and must not be sent back up it.
void GroundInsideTheRavineIsPastTheCorridor()
{
    ApproachRouteState state;
    Check("75y out and 21y up on the ravine floor",
          ApproachLegStep(state, Route(Gap(144.87f, -28.15f), Gap(75.04f, 21.48f)),
                          LIMITS),
          ApproachLeg::Direct);
    CheckBool("and the corridor is marked behind him", state.waypointPassed, true);

    ApproachRouteState atTheDoor;
    Check("standing on the staging point itself",
          ApproachLegStep(atTheDoor, Route(Gap(172.32f, -49.63f), Gap(0.f, 0.f)),
                          LIMITS),
          ApproachLeg::Direct);
    CheckBool("and so is that", atTheDoor.waypointPassed, true);
}

// ARRIVING AT THE CORRIDOR ENDS THE LEG, and arriving means arriving in three
// dimensions: the same rule the barrier and the staging watchdog already use.
void ArrivingAtTheCorridorEndsTheLeg()
{
    ApproachRouteState state;
    Check("9y out and 2y up from the corridor's start",
          ApproachLegStep(state, Route(Gap(9.f, 2.f), Gap(170.f, 48.f)), LIMITS),
          ApproachLeg::Direct);
    CheckBool("the leg is done", state.waypointPassed, true);

    // Ten yards out and a hundred and fifty up is the #217 reading that started
    // all of this. It is not an arrival at the corridor either.
    ApproachRouteState above;
    Check("10y out and 150y above the corridor's start",
          ApproachLegStep(above, Route(Gap(10.f, 150.f), Gap(200.f, 190.f)), LIMITS),
          ApproachLeg::ToWaypoint);
    CheckBool("and that is not an arrival", above.waypointPassed, false);
}

// ONCE WALKED, NOT WALKED AGAIN. The descent takes the leader further from the
// corridor's start than he was when he reached it, for most of its length.
void ThePassedCorridorIsNotWalkedTwice()
{
    ApproachRouteState state;
    Check("he reaches it",
          ApproachLegStep(state, Route(Gap(4.f, 1.f), Gap(176.f, 49.f)), LIMITS),
          ApproachLeg::Direct);
    CheckBool("passed", state.waypointPassed, true);

    // Half way down: 120 yards back up the corridor, and on a stretch that
    // reads Overhead of the door. Without the sticky flag this is a U-turn.
    Check("120y back up the corridor, mid-descent",
          ApproachLegStep(state, Route(Gap(120.f, -14.f), Gap(123.f, 43.f)), LIMITS),
          ApproachLeg::Direct);
    CheckBool("still passed", state.waypointPassed, true);
}

// NO READING IS NOT A LEG. A leader on another map, or not found this poll,
// leaves the choice to the poll that can measure - and does not stick.
void AnUnmeasuredLegIsNotJudged()
{
    ApproachRouteState state;
    ApproachRoute const route = Route(ApproachGap{}, Gap(497.09f, 78.87f));
    Check("no reading on the corridor", ApproachLegStep(state, route, LIMITS),
          ApproachLeg::Direct);
    CheckBool("and nothing is decided by it", state.waypointPassed, false);

    // The next poll measures, and the corridor is walked after all.
    Check("the poll after it measures",
          ApproachLegStep(state, Route(Gap(600.38f, 29.24f), Gap(497.09f, 78.87f)),
                          LIMITS),
          ApproachLeg::ToWaypoint);
}

// A CORRIDOR WHOSE LENGTH IS NOT A READING IS NOT A CORRIDOR. Negative is
// ApproachDistance's "no reading" and is refused rather than read through an
// absolute value, the same way TravelEndpointWithinTolerance refuses a negative
// tolerance.
void ACorridorWithNoLengthIsRefused()
{
    ApproachRouteState state;
    ApproachRoute route = Route(Gap(600.38f, 29.24f), Gap(497.09f, 78.87f));
    route.waypointToStagingYards = -1.f;
    Check("a corridor of negative length", ApproachLegStep(state, route, LIMITS),
          ApproachLeg::Direct);
    CheckBool("and it does not stick", state.waypointPassed, false);

    // Zero is a reading and not a refusal: a corridor that starts at the door
    // is a corridor nought yards long, and a leader 497 yards out still walks
    // at it - which is walking at the door, because they are the same place.
    route.waypointToStagingYards = 0.f;
    Check("a corridor at the door is still walked to from 497y out",
          ApproachLegStep(state, route, LIMITS), ApproachLeg::ToWaypoint);

    ApproachRouteState arrived;
    ApproachRoute atIt = Route(Gap(0.f, 0.f), Gap(0.f, 0.f));
    atIt.waypointToStagingYards = 0.f;
    Check("and standing on it ends the leg",
          ApproachLegStep(arrived, atIt, LIMITS), ApproachLeg::Direct);
    CheckBool("passed", arrived.waypointPassed, true);
}

// WITHOUT THE STEP NUMBERS NOTHING IS OVERHEAD, which is ApproachShapeOf's own
// documented degradation. The corridor is then chosen on distance alone, and
// this test pins that it degrades rather than misbehaves: the rim, being nearer
// the door than the corridor, counts as past it. That is the old behaviour, and
// it is the reason the adapter always passes the full limits.
void WithoutStepLimitsTheGuardDegradesToDistance()
{
    constexpr ApproachLimits flat{10.f, 0.f, 0.f};
    ApproachRouteState state;
    Check("the rim, with no step bound to read",
          ApproachLegStep(state, Route(Gap(172.32f, 95.48f), Gap(0.f, 145.11f)), flat),
          ApproachLeg::Direct);
    CheckBool("passed, on distance alone", state.waypointPassed, true);
}

}  // namespace

int main()
{
    ADoorWithNoCorridorIsWalkedAtDirectly();
    TheWailingApproachGoesDownTheCorridor();
    TheRimIsNearerTheDoorAndStillNotPastTheCorridor();
    GroundInsideTheRavineIsPastTheCorridor();
    ArrivingAtTheCorridorEndsTheLeg();
    ThePassedCorridorIsNotWalkedTwice();
    AnUnmeasuredLegIsNotJudged();
    ACorridorWithNoLengthIsRefused();
    WithoutStepLimitsTheGuardDegradesToDistance();
    if (!failures)
        std::printf("the way in is a corridor, not a bearing: the approach leg holds\n");
    return failures ? 1 : 0;
}
