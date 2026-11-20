/*
 * Where a survey plan for the Molten Core door ends, and who finishes the walk
 * (#1557, #1573).
 *
 * The shipped survey joins a node near the mountain's outside to the two door
 * nodes by a link that climbs the mountain. With that link dropped for the
 * door's aim, the plan ends at the nearest node, hundreds of yards short. These
 * tests pin the shape: without the drop the plan reaches the aim (the old,
 * lethal answer), with it the plan ends short, and the tail reading says the
 * walkway leg finishes it, or that the walk stops when that is out of reach.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OverseerDecisions;
namespace W = OverseerDecisions::MoltenCoreWalkway;

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

RouteLink Link(std::uint32_t from, std::uint32_t to, float yards)
{
    RouteLink l;
    l.from = from;
    l.to = to;
    l.yards = yards;
    l.onFoot = true;
    return l;
}

// The issue's data shape: a start node, an outside node (3380), and the two
// door nodes (1485, 1486) 5 yards from the aim, linked only from 3380. The
// outside node is placed 294 yards from the aim.
struct Survey
{
    std::vector<RouteNode> nodes;
    std::vector<RouteLink> links;
};

Survey MakeSurvey()
{
    Survey s;
    s.nodes = {Node(245, -6000.f, -2500.f, 120.f),
               Node(3380, W::LOTHOS_X, W::LOTHOS_Y - 294.f, 150.f),
               Node(1485, W::LOTHOS_X + 3.f, W::LOTHOS_Y + 3.f, W::LOTHOS_Z),
               Node(1486, W::LOTHOS_X - 3.f, W::LOTHOS_Y - 2.f, W::LOTHOS_Z)};
    s.links = {Link(245, 3380, 600.f), Link(3380, 1485, 700.f), Link(3380, 1486, 700.f)};
    return s;
}

RoutePlan Plan(Survey const& s)
{
    RoutePlanLimits limits;
    return PlanFootRoute(s.nodes, s.links, 0, -6000.f, -2500.f, W::LOTHOS_X, W::LOTHOS_Y, limits);
}

void TheSurveyAloneEndsAtTheDoorNode()
{
    Survey const s = MakeSurvey();
    RoutePlan const plan = Plan(s);
    Check("planned", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("ends within 10 yards of the aim", plan.endsFromAimYards <= DOOR_PLAN_REACH_YARDS, true);
    Check("tail reads as reached",
          ReadDoorPlanTail(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, plan) ==
              DoorPlanTail::SurveyReachesAim,
          true);
}

void WithTheDoorLinksDroppedThePlanEndsShortAndTheWalkwayLegFinishes()
{
    Survey s = MakeSurvey();
    Check("two links dropped",
          DropBogusDoorLinks(s.links, 0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z) == 2, true);
    RoutePlan const plan = Plan(s);
    Check("planned", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("ends short of the aim", plan.endsFromAimYards > 280.f && plan.endsFromAimYards < 300.f,
          true);
    DoorPlanTail const tail = ReadDoorPlanTail(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, plan);
    Check("the walkway leg finishes it", tail == DoorPlanTail::WalkwayLegFinishes, true);
    std::string const line = DoorPlanTailLine(tail, plan.endsFromAimYards);
    Check("the line names the walkway leg", line.find("walkway leg") != std::string::npos, true);
    Check("the line names the yards", line.find("294") != std::string::npos, true);
}

void AnUnprovenLastLegStopsAndSaysWhy()
{
    Survey s = MakeSurvey();
    // The nearest node the survey can reach is 700 yards out: past the bound.
    s.nodes[1] = Node(3380, W::LOTHOS_X, W::LOTHOS_Y - 700.f, 150.f);
    DropBogusDoorLinks(s.links, 0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z);
    RoutePlanLimits limits;
    limits.entryNodeYards = 5000.f;
    RoutePlan const plan =
        PlanFootRoute(s.nodes, s.links, 0, -6000.f, -2500.f, W::LOTHOS_X, W::LOTHOS_Y, limits);
    DoorPlanTail const tail = ReadDoorPlanTail(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, plan);
    Check("planned", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("stops short", tail == DoorPlanTail::StopsShort, true);
    std::string const line = DoorPlanTailLine(tail, plan.endsFromAimYards);
    Check("the line says it does not improvise", line.find("does not improvise") != std::string::npos,
          true);
}

void AnyOtherAimIsNotRead()
{
    Survey const s = MakeSurvey();
    RoutePlan const plan = Plan(s);
    Check("another aim", ReadDoorPlanTail(0, -6000.f, -2500.f, 120.f, plan) == DoorPlanTail::NotADoorAim,
          true);
    Check("another map",
          ReadDoorPlanTail(1, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, plan) == DoorPlanTail::NotADoorAim,
          true);
    Check("no plan",
          ReadDoorPlanTail(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, RoutePlan{}) ==
              DoorPlanTail::NotADoorAim,
          true);
    Check("no line for it", DoorPlanTailLine(DoorPlanTail::NotADoorAim, 1.f).empty(), true);
}
}  // namespace

int main()
{
    TheSurveyAloneEndsAtTheDoorNode();
    WithTheDoorLinksDroppedThePlanEndsShortAndTheWalkwayLegFinishes();
    AnUnprovenLastLegStopsAndSaysWhy();
    AnyOtherAimIsNotRead();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
