/*
 * A route's way in is a node the character can reach, not the nearest one.
 *
 * Measured 2026-09-23 on the dev realm. The Horde leader stood in Orgrimmar
 * at (1618.6, -4246.2, 47.2), walking to the Ragefire Chasm staging point in
 * the Cleft of Shadow. The planner took the nearest survey node, "Horde PVP
 * Barracks exit" (1713), 19 yards away by plane distance. Its recorded path
 * down into the Cleft starts at z 56.2, on a level above him, and he "made no
 * progress ... in 468 tries" at it. The caller now asks the navmesh about the
 * entry and plans again with the refused node in `refusedEntries`.
 *
 * The nodes and walk links below are the shipped survey's own rows for
 * Orgrimmar (playerbots_travelnode, playerbots_travelnode_link).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::EntryUnreachable;
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
    n.mapId = 1;
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
    Node(426, 1634.f, -4439.f, 16.f),  Node(488, 1934.f, -4162.f, 41.f),
    Node(537, 1562.f, -4407.f, 11.f),  Node(1457, 1818.f, -4427.f, -14.f),
    Node(1711, 1815.f, -4419.f, -19.f), Node(1713, 1637.f, -4243.f, 56.f),
    Node(2091, 1730.f, -4401.f, 34.f),
};

std::vector<RouteLink> const links{
    Walk(426, 537, 85.f),    Walk(426, 2091, 127.f),  Walk(537, 426, 81.f),
    Walk(537, 2091, 176.f),  Walk(488, 1711, 599.f),  Walk(1711, 537, 649.f),
    Walk(1711, 1713, 411.f), Walk(1711, 488, 530.f),  Walk(1711, 1457, 10.f),
    Walk(1457, 1711, 10.f),  Walk(1713, 2091, 258.f), Walk(1713, 426, 341.f),
    Walk(1713, 537, 376.f),  Walk(1713, 488, 455.f),  Walk(1713, 1711, 411.f),
    Walk(2091, 488, 578.f),  Walk(2091, 1713, 260.f), Walk(2091, 426, 125.f),
    Walk(2091, 537, 177.f),
};

constexpr float FROM_X = 1618.6f;
constexpr float FROM_Y = -4246.2f;
constexpr float AIM_X = 1807.39f;
constexpr float AIM_Y = -4407.8f;

void TheNearestNodeIsTheEntryByDefault()
{
    RoutePlanLimits limits;
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 1, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    Check("a route is planned", plan.verdict == RoutePlanVerdict::Planned);
    Check("the barracks exit is the entry when nothing is refused",
          !plan.nodes.empty() && plan.nodes.front() == 1713);
}

void ARefusedEntryIsSkipped()
{
    RoutePlanLimits limits;
    limits.refusedEntries.push_back(1713);
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 1, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    Check("a route is still planned without the barracks entry",
          plan.verdict == RoutePlanVerdict::Planned);
    Check("the refused node is not the entry",
          !plan.nodes.empty() && plan.nodes.front() != 1713);
    Check("the route still reaches the Cleft",
          !plan.nodes.empty() &&
              (plan.nodes.back() == 1711 || plan.nodes.back() == 1457));
}

void EveryNodeRefusedIsNoEntry()
{
    RoutePlanLimits limits;
    for (RouteNode const& n : nodes)
        limits.refusedEntries.push_back(n.id);
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 1, FROM_X, FROM_Y, AIM_X, AIM_Y, limits);
    Check("with every node refused there is no way in",
          plan.verdict == RoutePlanVerdict::NoEntryNode);
}

void OnlyARealRefusalRefuses()
{
    Check("no path at all refuses the node", EntryUnreachable(true, false, true, 0.f, 10.f));
    Check("a path that stops short of a node above refuses it",
          EntryUnreachable(true, false, false, 19.5f, 10.f));
    Check("a path that arrives accepts the node",
          !EntryUnreachable(true, false, false, 2.f, 10.f));
    Check("ground with no navmesh never refuses (the survey is the road there)",
          !EntryUnreachable(true, true, true, 500.f, 10.f));
    Check("a path that could not be computed is no answer",
          !EntryUnreachable(false, false, true, 500.f, 10.f));
}

}  // namespace

int main()
{
    OnlyARealRefusalRefuses();
    TheNearestNodeIsTheEntryByDefault();
    ARefusedEntryIsSkipped();
    EveryNodeRefusedIsNoEntry();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
