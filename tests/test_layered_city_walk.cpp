/*
 * A walk through a layered city: the surveyed route is kept while the navmesh
 * cannot reach, a refused waypoint gives way to a nearer one, and a lift lands
 * on the nearest layer rather than the top one (#592).
 *
 * Measured on the dev realm on 2026-09-23: the Horde leader was aimed at the
 * Ragefire Chasm staging point in Orgrimmar's Cleft of Shadow. The survey
 * routed him 634 yards over 117 waypoints to within 14 yards of it. Inside 400
 * yards the route was dropped and the cone aimed straight through the city
 * floor; while it was held, the lookahead waypoint was refused and no nearer one
 * was tried; and when he was read as below the world he was lifted 58 yards onto
 * the top of the stack.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::LayerProbe;
using OverseerDecisions::LiftLandingSurface;
using OverseerDecisions::LiftProbeReaches;
using OverseerDecisions::RouteAim;
using OverseerDecisions::RouteCursor;
using OverseerDecisions::RouteLegLimits;
using OverseerDecisions::RouteLegRetreats;
using OverseerDecisions::RouteLegStep;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::SurveyedRouteStillLeads;
using OverseerDecisions::SurveyedRouteWorthPlanning;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const d = got - want;
    if (d < 0.01f && d > -0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

// The module's own numbers: TRAVEL_ROUTE_MIN_YARDS, the lookahead and the
// arrival radius.
constexpr float MinYards = 400.f;

void InsideTheReachTheNavmeshIsAsked()
{
    // The measured case: 283 yards out, the walk down longer than the navmesh
    // will return, and a surveyed route that finishes 14 yards from the aim.
    Check("planned inside 400 yards when the navmesh has no route",
          SurveyedRouteWorthPlanning(283.f, false, MinYards));
    Check("kept inside 400 yards while it still leads nearer",
          SurveyedRouteStillLeads(283.f, 14.f, false, MinYards));

    // And where the navmesh can walk it, nothing changes from before.
    Check("not planned inside 400 yards when the navmesh routes there",
          !SurveyedRouteWorthPlanning(283.f, true, MinYards));
    Check("dropped inside 400 yards when the navmesh routes there",
          !SurveyedRouteStillLeads(283.f, 14.f, true, MinYards));

    // A route that ends farther out than the character stands leads nowhere.
    Check("dropped when its end is farther from the aim than the character",
          !SurveyedRouteStillLeads(120.f, 300.f, false, MinYards));
    Check("dropped when its end is exactly as far as the character",
          !SurveyedRouteStillLeads(120.f, 120.f, false, MinYards));
}

void OutsideTheReachNothingChanges()
{
    Check("planned outside 400 yards whatever the navmesh says",
          SurveyedRouteWorthPlanning(599.f, true, MinYards));
    Check("kept outside 400 yards whatever the navmesh says",
          SurveyedRouteStillLeads(599.f, 14.f, true, MinYards));
    Check("kept outside 400 yards even where it ends farther out",
          SurveyedRouteStillLeads(599.f, 700.f, false, MinYards));
}

// A switchback: 30 points five yards apart heading north, then back south
// again twenty yards to the east. Every point of it is inside one lookahead of
// the start, so the ordinary rule aims at the far end of the return leg, which
// is twenty yards from the start across whatever the switchback goes round.
std::vector<RoutePoint> Switchback()
{
    std::vector<RoutePoint> route;
    for (int i = 0; i < 30; ++i)
        route.push_back(RoutePoint{0.f, 5.f * static_cast<float>(i), 0.f});
    for (int i = 29; i >= 0; --i)
        route.push_back(RoutePoint{20.f, 5.f * static_cast<float>(i), 0.f});
    return route;
}

void ARefusedWaypointGivesWayToNearerOnes()
{
    RouteLegLimits first;
    first.lookaheadYards = 250.f;
    first.arrivedYards = 12.f;
    std::vector<RouteLegLimits> const retreats = RouteLegRetreats(first);

    Check("a surveyed route has nearer waypoints to offer", !retreats.empty());
    if (retreats.empty())
        return;

    // Shorter every time, and never under the arrival radius.
    float previous = first.lookaheadYards;
    for (std::size_t i = 0; i + 1 < retreats.size(); ++i)
    {
        Check("each retreat reaches less far than the one before",
              retreats[i].lookaheadYards < previous);
        Check("no retreat reaches under the arrival radius",
              retreats[i].lookaheadYards >= first.arrivedYards);
        Check("a shortened retreat is still a surveyed-route aim",
              retreats[i].maxPointsAhead == 0);
        previous = retreats[i].lookaheadYards;
    }
    CheckNear("the halvings go 125, 62.5, 31.25, 15.625, then the next point",
              static_cast<float>(retreats.size()), 5.f);
    CheckNear("the first retreat is half the lookahead", retreats[0].lookaheadYards, 125.f);
    Check("the last retreat is the next waypoint",
          retreats.back().maxPointsAhead == 1);

    // Asked of the switchback from its start, on a copy of the cursor each time.
    std::vector<RoutePoint> const route = Switchback();
    RouteCursor cursor;
    RouteAim const aim = RouteLegStep(cursor, route, 0.f, 0.f, first);
    Check("the ordinary aim is on the far side of the switchback", aim.index >= 50);

    RouteCursor probe = cursor;
    RouteAim const next = RouteLegStep(probe, route, 0.f, 0.f, retreats.back());
    Check("the last retreat aims at the next waypoint", next.hasAim && next.index == 1);
    Check("a retreat does not move the cursor it was copied from", cursor.at == 0);

    for (RouteLegLimits const& limits : retreats)
    {
        RouteCursor copy = cursor;
        RouteAim const nearer = RouteLegStep(copy, route, 0.f, 0.f, limits);
        Check("every retreat aims nearer along the route", nearer.hasAim && nearer.index < aim.index);
    }
}

void AOnePointRouteHasNothingNearer()
{
    RouteLegLimits measured;
    measured.lookaheadYards = 250.f;
    measured.arrivedYards = 12.f;
    measured.maxPointsAhead = 1;
    Check("a measured corridor already aims at its next point",
          RouteLegRetreats(measured).empty());

    RouteLegLimits nonsense;
    nonsense.lookaheadYards = 0.f;
    Check("nonsense limits give nothing", RouteLegRetreats(nonsense).empty());
}

void ALiftLandsOnTheNearestLayer()
{
    // The measured reading: feet at 72.0, the highest surface in reach at 130.2.
    // A street layer 23 yards up is what a character in a layered city slipped
    // under, and the probes rising in two-yard steps meet it first.
    float const feet = 72.0f;
    std::vector<float> const reaches = LiftProbeReaches(2.f, 60.f);
    std::vector<LayerProbe> rising;
    for (float reach : reaches)
    {
        LayerProbe probe;
        if (feet + reach >= 130.2f)
        {
            probe.valid = true;
            probe.surfaceZ = 130.2f;
        }
        else if (feet + reach >= 95.0f)
        {
            probe.valid = true;
            probe.surfaceZ = 95.0f;
        }
        rising.push_back(probe);
    }
    CheckNear("the lift lands on the street, not the roof",
              LiftLandingSurface(feet, rising, 130.2f, 2.f), 95.0f);

    // Only one surface over the head: the lift is the one it always was.
    std::vector<LayerProbe> open;
    for (float reach : reaches)
    {
        LayerProbe probe;
        if (feet + reach >= 130.2f)
        {
            probe.valid = true;
            probe.surfaceZ = 130.2f;
        }
        open.push_back(probe);
    }
    CheckNear("under open terrain the only surface is the landing",
              LiftLandingSurface(feet, open, 130.2f, 2.f), 130.2f);

    // Nothing answered at all: the highest surface still stands.
    CheckNear("no probes, no change", LiftLandingSurface(feet, {}, 130.2f, 2.f), 130.2f);

    // A surface inside the footing reach is the floor the floor probe said is
    // not there, and is not a landing.
    std::vector<LayerProbe> hugging{LayerProbe{true, 73.0f}, LayerProbe{true, 95.0f}};
    CheckNear("a surface within the footing reach is passed over",
              LiftLandingSurface(feet, hugging, 130.2f, 2.f), 95.0f);

    // A probe that starts inside a slab meets its underside first, and the next
    // probe up meets its top. The landing is the top.
    std::vector<LayerProbe> slab{LayerProbe{false, 0.f}, LayerProbe{true, 95.0f},
                                 LayerProbe{true, 96.2f}, LayerProbe{true, 96.2f},
                                 LayerProbe{true, 130.2f}};
    CheckNear("a slab's underside gives way to its top",
              LiftLandingSurface(feet, slab, 130.2f, 2.f), 96.2f);

    // And two layers more than a stride apart are two layers: the lower wins.
    std::vector<LayerProbe> twoFloors{LayerProbe{true, 95.0f}, LayerProbe{true, 101.0f}};
    CheckNear("the next floor up is not the same layer",
              LiftLandingSurface(feet, twoFloors, 130.2f, 2.f), 95.0f);

    // And never above the highest reading, which is the one that authorized it.
    std::vector<LayerProbe> wild{LayerProbe{true, 400.0f}};
    CheckNear("a probe above the highest reading is not believed",
              LiftLandingSurface(feet, wild, 130.2f, 2.f), 130.2f);
}

void TheProbeReachesAreTheFootingStride()
{
    std::vector<float> const reaches = LiftProbeReaches(2.f, 60.f);
    CheckNear("thirty probes cover the sixty-yard window",
              static_cast<float>(reaches.size()), 30.f);
    if (!reaches.empty())
    {
        CheckNear("the first probe reaches one stride", reaches.front(), 2.f);
        CheckNear("the last probe reaches the whole window", reaches.back(), 60.f);
    }
    Check("a step that is not a distance gives nothing", LiftProbeReaches(0.f, 60.f).empty());
    Check("a reach that is not a distance gives nothing", LiftProbeReaches(2.f, -1.f).empty());
}

}  // namespace

int main()
{
    InsideTheReachTheNavmeshIsAsked();
    OutsideTheReachNothingChanges();
    ARefusedWaypointGivesWayToNearerOnes();
    AOnePointRouteHasNothingNearer();
    ALiftLandsOnTheNearestLayer();
    TheProbeReachesAreTheFootingStride();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a layered-city walk keeps its route, retreats along it, and lifts to the nearest layer\n");
    return EXIT_SUCCESS;
}
