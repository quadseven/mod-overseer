/*
 * A walker that died on a planned leg is not released down it again at once
 * (mod-overseer#502, the box folded in from #1040).
 *
 * The Bork sequence: a level 60 walker died 13 yards from survey node 3487 on a
 * leg PlanFootRoute had planned, revived at node 199 two legs back, and was
 * released down the same legs again, because nothing the planner reads had
 * changed. The graph below is a local frame for that shape: the route runs
 * 10 -> 199 -> 3487 -> 20, and there is a way round from 199 through 30 to 20
 * that is 400 yards longer. Compiled against src/overseer_decisions.cpp and
 * nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

using OverseerDecisions::DiedOnLeg;
using OverseerDecisions::DiedOnLegLimits;
using OverseerDecisions::DiedOnLegsOnPlan;
using OverseerDecisions::LegsOfDeath;
using OverseerDecisions::PlanErrandRouteAgain;
using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::PriceDiedOnLegs;
using OverseerDecisions::RememberDiedOnLeg;
using OverseerDecisions::RouteLegSpan;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePoint;

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

constexpr std::uint32_t MAP = 1;
constexpr std::time_t NOW = 1760000000;

RouteNode Node(std::uint32_t id, float x, float y)
{
    RouteNode node;
    node.id = id;
    node.mapId = MAP;
    node.x = x;
    node.y = y;
    return node;
}

void Walk(std::vector<RouteLink>& links, std::uint32_t a, std::uint32_t b, float yards)
{
    for (int dir = 0; dir < 2; ++dir)
    {
        RouteLink link;
        link.from = dir ? b : a;
        link.to = dir ? a : b;
        link.yards = yards;
        link.onFoot = true;
        links.push_back(link);
    }
}

// 10 at the start, 199 and 3487 along the road, 20 at the aim. The way round
// from 199 goes out through 30 and is 400 yards longer than through 3487.
std::vector<RouteNode> Nodes()
{
    return {Node(10, 0.f, 0.f), Node(199, 500.f, 0.f), Node(3487, 1000.f, 0.f),
            Node(20, 1500.f, 0.f), Node(30, 1000.f, 300.f)};
}

std::vector<RouteLink> Links(bool withWayRound)
{
    std::vector<RouteLink> links;
    Walk(links, 10, 199, 500.f);
    Walk(links, 199, 3487, 500.f);
    Walk(links, 3487, 20, 500.f);
    if (withWayRound)
    {
        Walk(links, 199, 30, 700.f);
        Walk(links, 30, 20, 700.f);
    }
    return links;
}

RoutePlanLimits Limits()
{
    RoutePlanLimits limits;
    limits.entryNodeYards = 100.f;
    limits.minGainYards = 200.f;
    return limits;
}

// Planned from where the walker revived: node 199.
RoutePlan FromNode199(std::vector<RouteLink> const& links)
{
    return PlanFootRoute(Nodes(), links, MAP, 500.f, 0.f, 1500.f, 0.f, Limits());
}

bool Walks(RoutePlan const& plan, std::uint32_t id)
{
    for (std::uint32_t node : plan.nodes)
        if (node == id)
            return true;
    return false;
}

// The route the walker was released down, as the adapter keeps it: one point
// every 50 yards per leg, legs back to back.
void Route(std::vector<RouteLegSpan>& legs, std::vector<RoutePoint>& points)
{
    std::uint32_t const ids[] = {10, 199, 3487, 20};
    for (int leg = 0; leg < 3; ++leg)
    {
        RouteLegSpan span;
        span.from = ids[leg];
        span.to = ids[leg + 1];
        span.firstPoint = points.size();
        for (int step = 1; step <= 10; ++step)
        {
            RoutePoint point;
            point.x = 500.f * leg + 50.f * step;
            points.push_back(point);
        }
        span.endPoint = points.size();
        legs.push_back(span);
    }
}

void TodayTheSameLegsAreWalkedAgain()
{
    // What main does: the revived walker gets the leg it died on.
    RoutePlan const plan = FromNode199(Links(true));
    Check("today: a plan from node 199", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("today: it walks node 3487 again", Walks(plan, 3487), true);
}

void TheDeathIsOnTheLegsThatMeetAtTheNode()
{
    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    // 13 yards short of node 3487, on the 199 -> 3487 leg: Bork's death.
    std::vector<RouteLegSpan> const on = LegsOfDeath(legs, points, 987.f, 5.f, DiedOnLegLimits{});
    CheckUInt("death 13 yards short of node 3487 is on one leg", on.size(), 1);
    Check("and it is 199 -> 3487", !on.empty() && on[0].from == 199 && on[0].to == 3487, true);

    // Between the last point of one leg and the first of the next: both.
    std::vector<RouteLegSpan> const both =
        LegsOfDeath(legs, points, 1025.f, 0.f, DiedOnLegLimits{});
    CheckUInt("death past the node is on both legs that meet there", both.size(), 2);
    Check("second is 3487 -> 20", both.size() > 1 && both[1].from == 3487 && both[1].to == 20,
          true);

    // Mid-leg, the one leg only.
    std::vector<RouteLegSpan> const mid =
        LegsOfDeath(legs, points, 760.f, 10.f, DiedOnLegLimits{});
    CheckUInt("death mid-leg is on one leg", mid.size(), 1);

    // Off the route.
    CheckUInt("death 100 yards off the route is on no leg",
              LegsOfDeath(legs, points, 760.f, 100.f, DiedOnLegLimits{}).size(), 0);
    CheckUInt("no route, no leg",
              LegsOfDeath({}, {}, 987.f, 5.f, DiedOnLegLimits{}).size(), 0);
    DiedOnLegLimits bad;
    bad.nearYards = -1.f;
    CheckUInt("nonsense limits find no leg", LegsOfDeath(legs, points, 987.f, 5.f, bad).size(),
              0);
}

void TheNextPlanGoesRound()
{
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 199, 3487, NOW, DiedOnLegLimits{});
    RememberDiedOnLeg(died, 3487, 20, NOW, DiedOnLegLimits{});
    CheckUInt("two legs remembered", died.size(), 2);

    std::vector<RouteLink> links = Links(true);
    CheckUInt("both still held a minute later",
              PriceDiedOnLegs(links, died, NOW + 60, DiedOnLegLimits{}), 2);
    RoutePlan const plan = FromNode199(links);
    Check("re-planned from node 199", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("the re-plan does not walk node 3487", Walks(plan, 3487), false);
    Check("the re-plan goes round through node 30", Walks(plan, 30), true);
    CheckUInt("and no leg of it is one it died on", DiedOnLegsOnPlan(plan.nodes, died, NOW + 60),
              0);
}

void BothDirectionsArePriced()
{
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 3487, 199, NOW, DiedOnLegLimits{});
    std::vector<RouteLink> links = Links(true);
    PriceDiedOnLegs(links, died, NOW + 1, DiedOnLegLimits{});
    unsigned priced = 0;
    for (RouteLink const& link : links)
        if (link.yards > 500.f + 1000.f)
            ++priced;
    CheckUInt("the leg is priced both ways", priced, 2);

    // Remembering the same leg the other way round refreshes, not duplicates.
    RememberDiedOnLeg(died, 199, 3487, NOW + 10, DiedOnLegLimits{});
    CheckUInt("one leg, refreshed", died.size(), 1);
    Check("refreshed to the later death",
          !died.empty() && died[0].until == NOW + 10 + DiedOnLegLimits{}.holdSeconds, true);
}

void ItIsForgottenAfterTheHold()
{
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 199, 3487, NOW, DiedOnLegLimits{});
    RememberDiedOnLeg(died, 3487, 20, NOW, DiedOnLegLimits{});
    std::vector<RouteLink> links = Links(true);
    std::time_t const later = NOW + DiedOnLegLimits{}.holdSeconds;
    CheckUInt("nothing held once the hold is over",
              PriceDiedOnLegs(links, died, later, DiedOnLegLimits{}), 0);
    CheckUInt("and it is forgotten", died.size(), 0);
    Check("the road is the walker's again", Walks(FromNode199(links), 3487), true);
}

void WithNoWayRoundItStillWalksAndSaysSo()
{
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 199, 3487, NOW, DiedOnLegLimits{});
    std::vector<RouteLink> links = Links(false);
    PriceDiedOnLegs(links, died, NOW + 1, DiedOnLegLimits{});
    RoutePlan const plan = FromNode199(links);
    Check("still a plan, not none", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("it walks node 3487", Walks(plan, 3487), true);
    CheckUInt("and the plan counts the leg it died on", DiedOnLegsOnPlan(plan.nodes, died, NOW + 1),
              1);
}

void NonsenseLimitsChangeNothing()
{
    DiedOnLegLimits bad;
    bad.holdSeconds = 0;
    std::vector<DiedOnLeg> died;
    RememberDiedOnLeg(died, 199, 3487, NOW, bad);
    CheckUInt("a zero hold remembers nothing", died.size(), 0);

    RememberDiedOnLeg(died, 199, 3487, NOW, DiedOnLegLimits{});
    DiedOnLegLimits negative;
    negative.priceYards = -5.f;
    std::vector<RouteLink> links = Links(true);
    CheckUInt("a negative price prices nothing",
              PriceDiedOnLegs(links, died, NOW + 1, negative), 0);
    Check("and the plan is today's", Walks(FromNode199(links), 3487), true);

    // A link the survey refused stays refused.
    std::vector<RouteLink> refused = Links(true);
    refused[2].yards = -1.f;
    PriceDiedOnLegs(refused, died, NOW + 1, DiedOnLegLimits{});
    Check("a refused link stays refused", refused[2].yards < 0.f, true);
}

// THE FIXED ERRAND (#502). An errand to a trainer, a vendor or a mailbox keeps
// its aim through a death, so nothing re-planned its route: the walker was
// released down the same route, and the price on the died-on leg was never
// asked. A plan records the walker's count of remembered deaths, and a newer
// death plans it again once the walker lives.
void AFixedErrandIsPlannedAgainAfterADeath()
{
    Check("a route never planned is planned", PlanErrandRouteAgain(false, 0, 0, true), true);
    Check("and is planned even for a ghost (today's rule)",
          PlanErrandRouteAgain(false, 0, 1, false), true);
    Check("a planned route with no newer death is kept", PlanErrandRouteAgain(true, 0, 0, true),
          false);
    Check("a planned route with a newer death is kept while the walker is dead",
          PlanErrandRouteAgain(true, 0, 1, false), false);
    Check("and planned again once it lives", PlanErrandRouteAgain(true, 0, 1, true), true);
    Check("a route planned after that death is kept", PlanErrandRouteAgain(true, 1, 1, true),
          false);

    // The whole sequence for an errand whose aim (node 20) never moves.
    std::vector<RouteLink> links = Links(true);
    std::uint32_t deaths = 0;
    std::uint32_t plannedAt = deaths;
    RoutePlan plan = FromNode199(links);
    Check("fixed errand: first plan walks node 3487", Walks(plan, 3487), true);

    std::vector<RouteLegSpan> legs;
    std::vector<RoutePoint> points;
    Route(legs, points);
    std::vector<DiedOnLeg> died;
    for (RouteLegSpan const& leg : LegsOfDeath(legs, points, 987.f, 5.f, DiedOnLegLimits{}))
        RememberDiedOnLeg(died, leg.from, leg.to, NOW, DiedOnLegLimits{});
    ++deaths;

    Check("fixed errand: dead, the route is not re-planned",
          PlanErrandRouteAgain(true, plannedAt, deaths, false), false);
    Check("fixed errand: revived, the route is re-planned",
          PlanErrandRouteAgain(true, plannedAt, deaths, true), true);
    std::vector<RouteLink> priced = links;
    PriceDiedOnLegs(priced, died, NOW + 60, DiedOnLegLimits{});
    plan = FromNode199(priced);
    plannedAt = deaths;
    Check("fixed errand: the re-plan goes round node 3487", Walks(plan, 3487), false);
    Check("fixed errand: and is then kept", PlanErrandRouteAgain(true, plannedAt, deaths, true),
          false);
}

}  // namespace

int main()
{
    TodayTheSameLegsAreWalkedAgain();
    TheDeathIsOnTheLegsThatMeetAtTheNode();
    TheNextPlanGoesRound();
    BothDirectionsArePriced();
    ItIsForgottenAfterTheHold();
    WithNoWayRoundItStillWalksAndSaysSo();
    NonsenseLimitsChangeNothing();
    AFixedErrandIsPlannedAgainAfterADeath();
    if (failures)
    {
        std::printf("%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    std::printf("route died on leg: all %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
