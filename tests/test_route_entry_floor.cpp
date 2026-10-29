/*
 * A route's way in is on the character's own floor, not on a roof above it.
 *
 * Measured 2026-10-12 (#925). A leader stood in the Stormwind Wizard's
 * Sanctum at map 0 (-8989.4, 848.0, 29.6). The planner took the nearest node
 * by plane distance, 1689 "Stormwind City portal", the Mage Tower roof: 37
 * yards across and 119 yards above him, reached only through areatrigger
 * 702. Node 1690 stands on his own floor 47 yards off with a surveyed walk
 * link to the street (1690 to 2890). Every bearing was refused and he never
 * moved. The planner now knows the character's height and passes over a node
 * outside RoutePlanLimits::maxEntryRiseYards while one inside it is near.
 *
 * The nodes and walk links below are the shipped survey's own rows
 * (playerbots_travelnode, playerbots_travelnode_link). The areatrigger links
 * between the floor and the roof (1434 to 1689, 1435 to 1690) are not walks
 * and are left out, as the caller leaves them out.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;

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

RouteNode Node(std::uint32_t id, float x, float y, float z)
{
    RouteNode n;
    n.id = id;
    n.mapId = 0;
    n.x = x;
    n.y = y;
    n.z = z;
    return n;
}

RouteLink Walk(std::uint32_t from, std::uint32_t to, float yards)
{
    RouteLink l;
    l.from = from;
    l.to = to;
    l.yards = yards;
    l.onFoot = true;
    return l;
}

std::vector<RouteNode> const nodes{
    Node(1689, -9015.8f, 874.6f, 148.617f),   Node(1435, -9020.09f, 880.502f, 147.576f),
    Node(1690, -9017.4f, 886.3f, 29.6206f),   Node(1434, -9023.26f, 892.398f, 29.5096f),
    Node(2890, -8744.62f, 657.759f, 105.175f), Node(1677, -8766.11f, 845.499f, 87.9952f),
};

std::vector<RouteLink> const links{
    Walk(1689, 1435, 8.40486f),  Walk(1435, 1689, 7.47036f),
    Walk(1690, 2890, 488.512f),  Walk(1690, 1434, 8.72648f),
    Walk(1690, 1677, 453.485f),  Walk(1434, 1690, 8.63848f),
    Walk(2890, 1690, 488.512f),  Walk(2890, 1677, 314.36f),
    Walk(1677, 2890, 314.36f),
};

constexpr float FROM_X = -8989.4f;
constexpr float FROM_Y = 848.0f;
constexpr float FROM_Z = 29.6f;
// Out past the street node, so a route from the floor has somewhere to go.
constexpr float AIM_X = -8700.f;
constexpr float AIM_Y = 600.f;

RoutePlanLimits KnownHeight(float z)
{
    RoutePlanLimits limits;
    limits.fromZKnown = true;
    limits.fromZ = z;
    return limits;
}

void TheSanctumEntersOnItsOwnFloor()
{
    RoutePlan const plan = PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X,
                                         AIM_Y, KnownHeight(FROM_Z));
    Check("a route is planned from the Sanctum floor",
          plan.verdict == RoutePlanVerdict::Planned);
    Check("the entry is 1690 on the character's floor",
          !plan.nodes.empty() && plan.nodes.front() == 1690);
    Check("the roof node 1689 is not the entry",
          plan.nodes.empty() || plan.nodes.front() != 1689);
    Check("the route reaches the street",
          !plan.nodes.empty() && plan.nodes.back() == 2890);
    Check("the entry's height gap is reported and under the limit",
          plan.entryHeightGapYards >= 0.f && plan.entryHeightGapYards < 1.f);
}

void UnknownHeightIsTodaysAnswer()
{
    RoutePlanLimits limits;
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    // The roof island goes nowhere near the aim, which is what the leader got.
    Check("with no height the nearest node is still the roof and nothing is planned",
          plan.verdict == RoutePlanVerdict::NoNearerNode);
    Check("no height gap is reported when the height is unknown",
          plan.entryHeightGapYards < 0.f);
}

void OnTheRoofTheRoofIsTheEntry()
{
    // Standing on the roof, the roof nodes are the floor and 1689 is nearest.
    RoutePlan const plan = PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X,
                                         AIM_Y, KnownHeight(148.6f));
    Check("on the roof the plan is the roof's, which reaches nothing nearer",
          plan.verdict == RoutePlanVerdict::NoNearerNode);
}

void NoLevelNodeNearFallsBackToTheNearest()
{
    // Nothing within the limit of this height: the nearest node is taken as
    // before, so a character with no level node near still gets one.
    RoutePlanLimits limits = KnownHeight(-200.f);
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    RoutePlanLimits unknown;
    RoutePlan const before =
        PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, unknown);
    Check("with no level node near, the verdict is today's",
          plan.verdict == before.verdict && plan.nodes == before.nodes);
}

void ALevelNodeOutOfReachDoesNotCount()
{
    // 1690 is 47 yards off. With entry reach under that, only the roof nodes
    // are within reach, so the floor rule has nothing to prefer.
    RoutePlanLimits limits = KnownHeight(FROM_Z);
    limits.entryNodeYards = 45.f;
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    Check("a level node beyond reach does not refuse the near ones",
          plan.verdict == RoutePlanVerdict::NoNearerNode);
}

void ARefusedLevelNodeDoesNotCount()
{
    RoutePlanLimits limits = KnownHeight(FROM_Z);
    limits.refusedEntries.push_back(1690);
    limits.refusedEntries.push_back(1434);
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    Check("with both floor nodes refused, the nearest of the rest is taken",
          plan.verdict == RoutePlanVerdict::NoNearerNode);
}

void TheRouteLineNamesTheSameFloorNodesInReach()
{
    // Sanctum start: 1690 and 1434 are on the floor and in reach, the roof
    // nodes 1689 and 1435 are in reach but 119 yards up.
    RoutePlan const plan = PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y,
                                         KnownHeight(FROM_Z));
    Check("two floor nodes are in reach of the Sanctum", plan.sameFloorNodesInReach == 2);
    Check("the line names the entry, the gap and the count",
          OverseerDecisions::RouteEntryNote(plan.nodes.front(), plan.entryHeightGapYards,
                                            plan.sameFloorNodesInReach) ==
              "It enters at node 1690, 0 yards above or below where the plan starts (#925), "
              "with 2 same-floor nodes in reach");
    Check("a count of one keeps the same wording",
          OverseerDecisions::RouteEntryNote(7, 12.f, 1) ==
              "It enters at node 7, 12 yards above or below where the plan starts (#925), "
              "with 1 same-floor nodes in reach");
    Check("a count of none is said as none",
          OverseerDecisions::RouteEntryNote(1689, 119.f, 0) ==
              "It enters at node 1689, 119 yards above or below where the plan starts (#925), "
              "with 0 same-floor nodes in reach");
    // A refused floor node is not in reach; unknown height counts nothing.
    RoutePlanLimits refused = KnownHeight(FROM_Z);
    refused.refusedEntries.push_back(1690);
    Check("a refused floor node is not counted",
          PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, refused)
                  .sameFloorNodesInReach == 1);
    RoutePlanLimits unknown;
    Check("an unknown height counts nothing",
          PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, unknown)
                  .sameFloorNodesInReach == 0);
}

void BadHeightLimitsAreRefused()
{
    RoutePlanLimits limits = KnownHeight(FROM_Z);
    limits.maxEntryRiseYards = -1.f;
    Check("a negative height limit is refused",
          PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits).verdict ==
              RoutePlanVerdict::BadLimits);
    // Ignored when the height is not known, as before #925.
    limits.fromZKnown = false;
    Check("a height limit is not read when the height is unknown",
          PlanFootRoute(nodes, links, 0, FROM_X, FROM_Y, AIM_X, AIM_Y, limits).verdict !=
              RoutePlanVerdict::BadLimits);
}

} // namespace

int main()
{
    TheSanctumEntersOnItsOwnFloor();
    UnknownHeightIsTodaysAnswer();
    OnTheRoofTheRoofIsTheEntry();
    NoLevelNodeNearFallsBackToTheNearest();
    ALevelNodeOutOfReachDoesNotCount();
    ARefusedLevelNodeDoesNotCount();
    TheRouteLineNamesTheSameFloorNodesInReach();
    BadHeightLimitsAreRefused();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
