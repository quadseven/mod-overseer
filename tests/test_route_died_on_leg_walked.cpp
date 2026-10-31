/*
 * A death is charged to the leg actually walked, and every walk kind avoids a
 * leg its walker just died on (mod-overseer#1228).
 *
 * After #502 shipped, one member logged three deaths on the same leg (2175 to
 * 2323) inside its 120-minute price window. The second and third came after a
 * trainer walk that printed no "its route avoids" line. Two gaps:
 *
 *  - a death was charged to whatever route the walker was last planned, even
 *    once it had left that route (the memory outlives the walk);
 *  - a walk that did not take the planned-route path was never priced: a
 *    walker revived at its corpse stands on the leg that killed it, and the
 *    road join it then takes was chosen on bare survey yards and returned
 *    before the log; and a walk to a creature (a trainer) is handed whole to
 *    upstream's mover, which never asks the survey at all.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

using OverseerDecisions::ChooseRoadJoin;
using OverseerDecisions::DiedOnLeg;
using OverseerDecisions::DiedOnLegLimits;
using OverseerDecisions::DiedOnLegPrice;
using OverseerDecisions::DiedOnLegsHeld;
using OverseerDecisions::DiedOnLegsOnPlan;
using OverseerDecisions::DiedOnLegsOnRoute;
using OverseerDecisions::LegsOfDeath;
using OverseerDecisions::LegsWalkedAtDeath;
using OverseerDecisions::RememberDiedOnLeg;
using OverseerDecisions::RoadJoinOption;
using OverseerDecisions::RouteLegSpan;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::SteerCreatureWalkAlongRoute;

namespace
{

int failures = 0;
int checks = 0;

void Check(char const* what, bool got, bool want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckUInt(char const* what, unsigned long got, unsigned long want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got %lu, wanted %lu\n", what, got, want);
    ++failures;
}

constexpr std::time_t NOW = 1760000000;

// A route of three legs along the x axis, 500 yards each, one point every 50
// yards: 100 -> 2175 -> 2323 -> 400. Leg k owns points [10k, 10k + 10).
void Route(std::vector<RouteLegSpan>& legs, std::vector<RoutePoint>& points)
{
    std::uint32_t const ids[] = {100, 2175, 2323, 400};
    for (int leg = 0; leg < 3; ++leg)
    {
        RouteLegSpan span;
        span.from = ids[leg];
        span.to = ids[leg + 1];
        span.firstPoint = points.size();
        for (int i = 0; i < 10; ++i)
        {
            RoutePoint p;
            p.x = 500.f * leg + 50.f * i;
            points.push_back(p);
        }
        span.endPoint = points.size();
        legs.push_back(span);
    }
}

bool Holds(std::vector<RouteLegSpan> const& on, std::uint32_t from, std::uint32_t to)
{
    for (RouteLegSpan const& leg : on)
        if (leg.from == from && leg.to == to)
            return true;
    return false;
}

// THE LEG BEING WALKED IS CHARGED. Cursor on the middle leg, stepped a poll ago,
// dead in the middle of it.
void ADeathOnTheWalkedLegIsCharged()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    std::vector<RouteLegSpan> const on =
        LegsWalkedAtDeath(legs, points, 14, NOW - 15, NOW, 700.f, 5.f, DiedOnLegLimits{});
    CheckUInt("walked leg: one leg charged", on.size(), 1);
    Check("walked leg: it is 2175 to 2323", Holds(on, 2175, 2323), true);
}

// A death at a node is still on both legs that meet there, as #502 said.
void ADeathAtANodeIsOnBothLegsThatMeetThere()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    std::vector<RouteLegSpan> const on =
        LegsWalkedAtDeath(legs, points, 19, NOW - 15, NOW, 975.f, 0.f, DiedOnLegLimits{});
    Check("node: 2175 to 2323", Holds(on, 2175, 2323), true);
    Check("node: 2323 to 400", Holds(on, 2323, 400), true);
    Check("node: not the first leg", Holds(on, 100, 2175), false);
}

// THE #1228 SHAPE, PART ONE. The walker last planned this route, then left it
// (the errand ended, or a creature walk took it elsewhere with no new plan),
// and died beside it two minutes later. Before #1228 the death was charged to
// the remembered route anyway; now it is not.
void ADeathBesideARouteNoLongerWalkedIsNotCharged()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    DiedOnLegLimits const limits;
    Check("left route: the bare reading would charge it",
          !LegsOfDeath(legs, points, 700.f, 5.f, limits).empty(), true);
    CheckUInt("left route: last stepped two minutes ago, nothing charged",
              LegsWalkedAtDeath(legs, points, 14, NOW - 120, NOW, 700.f, 5.f, limits).size(),
              0);
    CheckUInt("never stepped: nothing charged",
              LegsWalkedAtDeath(legs, points, 0, 0, NOW, 700.f, 5.f, limits).size(), 0);
    CheckUInt("a step in the future is nonsense: nothing charged",
              LegsWalkedAtDeath(legs, points, 14, NOW + 30, NOW, 700.f, 5.f, limits).size(),
              0);
}

// ON THE ROUTE BUT NOT WHERE IT WAS WALKING. The cursor is on the first leg and
// the body lies beside the last one: the walker was not there on this route,
// so the last leg is not charged.
void ADeathFarAlongTheRouteIsNotChargedToALegNotYetReached()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    CheckUInt("far leg: nothing charged",
              LegsWalkedAtDeath(legs, points, 3, NOW - 15, NOW, 1250.f, 0.f, DiedOnLegLimits{})
                  .size(),
              0);
    // And a cursor past the last point is on the last leg.
    std::vector<RouteLegSpan> const end =
        LegsWalkedAtDeath(legs, points, 99, NOW - 15, NOW, 1250.f, 0.f, DiedOnLegLimits{});
    Check("cursor past the end: the last leg", Holds(end, 2323, 400), true);
}

// THE #1228 SHAPE, PART TWO. Revived at its corpse on leg 2175-2323, the walker
// stands between nodes, so its next walk joins the road it stands on. Two
// roads pass near: the one it died on (a shorter walk) and another one. On bare
// yards the died-on road wins; priced for this walker, the other one does.
void TheRoadJoinIsPricedForTheWalker()
{
    std::vector<DiedOnLeg> died;
    DiedOnLegLimits const limits;
    RememberDiedOnLeg(died, 2175, 2323, NOW - 600, limits);

    RoadJoinOption diedOn;
    diedOn.remainingYards = 200.f;
    diedOn.onwardYards = 600.f;
    diedOn.endsFromAimYards = 50.f;
    RoadJoinOption other;
    other.remainingYards = 450.f;
    other.onwardYards = 700.f;
    other.endsFromAimYards = 50.f;

    std::vector<RoadJoinOption> bare{diedOn, other};
    CheckUInt("road join: on bare yards the died-on road is joined",
              ChooseRoadJoin(bare, 2000.f, 200.f), 0);

    // Either direction of the road is the same ground.
    Check("price: 2323 to 2175 is priced too",
          DiedOnLegPrice(2323, 2175, died, NOW, limits) == limits.priceYards, true);
    Check("price: another road is not", DiedOnLegPrice(2323, 400, died, NOW, limits) == 0.f,
          true);

    std::vector<RoadJoinOption> priced = bare;
    priced[0].remainingYards += DiedOnLegPrice(2175, 2323, died, NOW, limits);
    priced[1].remainingYards += DiedOnLegPrice(2323, 400, died, NOW, limits);
    CheckUInt("road join: priced, the other road is joined",
              ChooseRoadJoin(priced, 2000.f, 200.f), 1);

    // After the hold the road is forgotten and the short way is back.
    Check("price: forgotten after the hold",
          DiedOnLegPrice(2175, 2323, died, NOW - 600 + limits.holdSeconds, limits) == 0.f,
          true);
}

// WITH NO OTHER ROAD, THE DIED-ON ROAD IS STILL JOINED, AND IT IS SAID. The
// count the log reads covers every leg of the route returned, the joined road
// included. Counting only the planned nodes (as #502 did) called a route that
// starts down the died-on road one that "avoids" it.
void AWalkThatStillTakesTheRoadSaysSo()
{
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 2175, 2323, NOW - 600, DiedOnLegLimits{});

    std::vector<RoadJoinOption> only(1);
    only[0].remainingYards = 200.f + DiedOnLegPrice(2175, 2323, died, NOW, DiedOnLegLimits{});
    only[0].onwardYards = 600.f;
    only[0].endsFromAimYards = 50.f;
    CheckUInt("only road: still joined", ChooseRoadJoin(only, 2000.f, 200.f), 0);

    // The joined road, then the plan on from its far node.
    std::vector<RouteLegSpan> spans(2);
    spans[0].from = 2175;
    spans[0].to = 2323;
    spans[1].from = 2323;
    spans[1].to = 400;
    std::vector<std::uint32_t> const planNodes{2323, 400};
    CheckUInt("only road: the plan's nodes alone miss it",
              DiedOnLegsOnPlan(planNodes, died, NOW), 0);
    CheckUInt("only road: the route's legs count it", DiedOnLegsOnRoute(spans, died, NOW), 1);
    CheckUInt("only road: the road alone counts it",
              DiedOnLegsOnRoute(std::vector<RouteLegSpan>(spans.begin(), spans.begin() + 1),
                                died, NOW),
              1);
    CheckUInt("expired: nothing counted",
              DiedOnLegsOnRoute(spans, died, NOW - 600 + DiedOnLegLimits{}.holdSeconds), 0);
}

// THE TRAINER WALK. A walk to a creature is handed to upstream's mover, which
// never asks the survey, unless its walker holds a died-on leg: then it is
// stepped along the walker's priced route like a place walk. Nothing changes
// for a walker that holds nothing, or for a place walk, which is always routed.
void ATrainerWalkIsSteeredWhileADiedOnLegIsHeld()
{
    std::vector<DiedOnLeg> died;
    CheckUInt("held: none", DiedOnLegsHeld(died, NOW), 0);
    Check("trainer walk, nothing held: upstream walks it",
          SteerCreatureWalkAlongRoute(true, DiedOnLegsHeld(died, NOW)), false);

    RememberDiedOnLeg(died, 2175, 2323, NOW - 600, DiedOnLegLimits{});
    CheckUInt("held: one", DiedOnLegsHeld(died, NOW), 1);
    Check("trainer walk, a died-on leg held: steered along the priced route",
          SteerCreatureWalkAlongRoute(true, DiedOnLegsHeld(died, NOW)), true);
    Check("place walk: routed anyway, not this rule",
          SteerCreatureWalkAlongRoute(false, DiedOnLegsHeld(died, NOW)), false);

    std::time_t const after = NOW - 600 + DiedOnLegLimits{}.holdSeconds;
    CheckUInt("held: none after the hold", DiedOnLegsHeld(died, after), 0);
    Check("trainer walk after the hold: upstream walks it again",
          SteerCreatureWalkAlongRoute(true, DiedOnLegsHeld(died, after)), false);
}

void NonsenseLimitsChargeNothing()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    DiedOnLegLimits bad;
    bad.nearYards = 0.f;
    CheckUInt("bad limits: nothing charged",
              LegsWalkedAtDeath(legs, points, 14, NOW - 15, NOW, 700.f, 5.f, bad).size(), 0);
    DiedOnLegLimits negative;
    negative.walkedWithinSeconds = -1;
    CheckUInt("negative window: nothing charged",
              LegsWalkedAtDeath(legs, points, 14, NOW, NOW, 700.f, 5.f, negative).size(), 0);
    CheckUInt("no legs: nothing charged",
              LegsWalkedAtDeath({}, points, 0, NOW - 15, NOW, 700.f, 5.f, DiedOnLegLimits{})
                  .size(),
              0);
}

}  // namespace

int main()
{
    ADeathOnTheWalkedLegIsCharged();
    ADeathAtANodeIsOnBothLegsThatMeetThere();
    ADeathBesideARouteNoLongerWalkedIsNotCharged();
    ADeathFarAlongTheRouteIsNotChargedToALegNotYetReached();
    TheRoadJoinIsPricedForTheWalker();
    AWalkThatStillTakesTheRoadSaysSo();
    ATrainerWalkIsSteeredWhileADiedOnLegIsHeld();
    NonsenseLimitsChargeNothing();
    if (failures)
    {
        std::printf("%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    std::printf("route died on leg walked: all %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
