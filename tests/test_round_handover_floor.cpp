/*
 * A way round may not hand the walk to the stepper on another floor than the
 * aim (mod-overseer#1557).
 *
 * The ordered Molten Core raid's head walked about 8600 yards and ended on the
 * Blackrock Spire ledge, 189 yards from the door and 105 above it, where every
 * bearing was refused. The survey has a node on the door ("Molten Core
 * entrance", 5 yards from the aim) and a walk link to it from the same node
 * that has the link to the ledge ("Blackrock Spire exit"). The door's leg runs
 * through ground the other side guards, so the #326 rule went ROUND: the
 * guard-free search reaches the ledge, and the ledge is within the
 * handover ceiling and not farther than the way through once the guards are
 * priced. It is nearer the aim in the plane and unreachable from it on foot.
 *
 * The ids, names and coordinates below are rows of the shipped
 * playerbots_travelnode and playerbots_travelnode_link tables.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
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

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

constexpr std::uint32_t EASTERN_KINGDOMS = 0;
constexpr std::uint32_t DRACODAR = 3380;
constexpr std::uint32_t DOOR = 1485;
constexpr std::uint32_t LEDGE = 1701;

// The aim: the Molten Core door's staging point.
constexpr float AIM_X = -7508.3f;
constexpr float AIM_Y = -1039.7f;
constexpr float AIM_Z = 180.9f;

std::vector<RouteNode> Nodes()
{
    return {
        {DRACODAR, EASTERN_KINGDOMS, -8203.09f, -922.076f, 133.28f},   // Burning Steppes Dracodar
        {DOOR,     EASTERN_KINGDOMS, -7510.87f, -1035.87f, 178.952f},  // Molten Core entrance
        {LEDGE,    EASTERN_KINGDOMS, -7524.7f,  -1228.41f, 285.733f},  // Blackrock Spire exit
    };
}

std::vector<RouteLink> Links()
{
    RouteLink toDoor;
    toDoor.from = DRACODAR;
    toDoor.to = DOOR;
    toDoor.yards = 1194.61f;
    toDoor.onFoot = true;
    // The door's leg crosses guarded ground, priced as a party that is not
    // yet past it would price it.
    toDoor.guardedGround = true;
    toDoor.detourWorthYards = 1000.f;
    RouteLink toLedge;
    toLedge.from = DRACODAR;
    toLedge.to = LEDGE;
    toLedge.yards = 1232.11f;
    toLedge.onFoot = true;
    return {toDoor, toLedge};
}

RoutePlanLimits Limits(bool aimHeightKnown)
{
    RoutePlanLimits limits;
    limits.fromZKnown = true;
    limits.fromZ = 133.28f;
    limits.aimZKnown = aimHeightKnown;
    limits.aimZ = AIM_Z;
    return limits;
}

RoutePlan Plan(std::vector<RouteNode> const& nodes, bool aimHeightKnown)
{
    return PlanFootRoute(nodes, Links(), EASTERN_KINGDOMS, -8203.f, -922.f, AIM_X, AIM_Y,
                         Limits(aimHeightKnown));
}
}  // namespace

int main()
{
    // THE REPORT: the plan ends on the node at the aim, not the ledge above it.
    RoutePlan const plan = Plan(Nodes(), true);
    Check("planned", plan.verdict == RoutePlanVerdict::Planned, true);
    CheckUInt("the plan's final node is the door node", plan.nodes.empty() ? 0 : plan.nodes.back(),
              DOOR);
    Check("it ends within a few yards of the aim", plan.endsFromAimYards < 10.f, true);
    Check("it did not go round", plan.wentRound, false);
    CheckUInt("it says the door's leg is guarded", plan.guardedLegs, 1);

    // The aim's height not given: the plan is the one this planner always made.
    RoutePlan const old = Plan(Nodes(), false);
    CheckUInt("no aim height, the ledge as before", old.nodes.empty() ? 0 : old.nodes.back(),
              LEDGE);
    Check("no aim height, it went round", old.wentRound, true);

    // A handover node on the aim's own floor is still taken: lower the ledge to
    // the door's height and the way round is the way it was.
    std::vector<RouteNode> level = Nodes();
    level[2].z = 184.f;
    RoutePlan const flat = Plan(level, true);
    CheckUInt("a handover on the aim's floor is still taken",
              flat.nodes.empty() ? 0 : flat.nodes.back(), LEDGE);
    Check("a handover on the aim's floor went round", flat.wentRound, true);

    // A hill is not a cliff: 105 yards of rise over a long leftover is a
    // gradient the stepper walks.
    std::vector<RouteNode> hill = Nodes();
    hill[2].x = -7510.87f;
    hill[2].y = -1035.87f - 480.f;
    RoutePlan const slope = Plan(hill, true);
    CheckUInt("a long gentle rise is still a handover",
              slope.nodes.empty() ? 0 : slope.nodes.back(), LEDGE);

    if (failures)
        return 1;    // A head that ended an attempt on the ledge does not take the same way round
    // again: the next plan is made with the round rule off and ends at the door.
    RoutePlanLimits again = Limits(false);
    again.allowRound = false;
    RoutePlan const retried = PlanFootRoute(Nodes(), Links(), EASTERN_KINGDOMS, -8203.f, -922.f,
                                            AIM_X, AIM_Y, again);
    CheckUInt("a retry with the round rule off ends at the door",
              retried.nodes.empty() ? 0 : retried.nodes.back(), DOOR);

    // More than 50 yards above the aim is a wrong-level ending.
    Check("the ledge is above the door", OverseerDecisions::EndedAboveTheAim(285.7f, AIM_Z), true);
    Check("a few yards above is not", OverseerDecisions::EndedAboveTheAim(200.f, AIM_Z), false);
    Check("below the aim is not this case", OverseerDecisions::EndedAboveTheAim(100.f, AIM_Z),
          false);
    Check("exactly the limit is not above it",
          OverseerDecisions::EndedAboveTheAim(AIM_Z + OverseerDecisions::WRONG_LEVEL_YARDS, AIM_Z),
          false);

    // The planned nodes, named once in the log.
    Check("the node list names every node",
          OverseerDecisions::RouteNodeList({DRACODAR, DOOR}, 64) == "3380 1485", true);
    Check("a long list is cut and says so",
          OverseerDecisions::RouteNodeList({1, 2, 3, 4}, 2) == "1 2 ... and 2 more", true);
    Check("an empty plan names nothing", OverseerDecisions::RouteNodeList({}, 64).empty(), true);

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
