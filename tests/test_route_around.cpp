/*
 * A way round an obstacle (mod-overseer#316).
 *
 * The step chooser is greedy - five bearings in a cone toward the aim - and a
 * greedy cone cannot decide to walk AWAY from a destination in order to reach
 * it, which is what going round a mountain is. Three Wailing Caverns runs ended
 * at 2577, 2788 and 3022 yards out without converging.
 *
 * These two decisions are the fix: plan a sequence of surveyed nodes, then say
 * which of that route's points to aim at this poll. The fixtures below are the
 * REAL shape measured on the dev realm's own travel node tables - the same node
 * ids, the same coordinates, the same link distances - reduced to the nodes the
 * measured routes actually touch plus the island that made the naive planner
 * answer "no route".
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RouteAim;
using OverseerDecisions::RouteCursor;
using OverseerDecisions::RouteLegLimits;
using OverseerDecisions::RouteLegStep;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePlanVerdictName;
using OverseerDecisions::RoutePoint;

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

void CheckVerdict(char const* what, RoutePlanVerdict got, RoutePlanVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RoutePlanVerdictName(got),
                RoutePlanVerdictName(want));
    ++failures;
}

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

// ------------------------------------------------------------ the fixture --
//
// Map 1, Kalimdor. Every id, name and coordinate below was read out of the
// realm's `playerbots_travelnode` table; every link distance out of
// `playerbots_travelnode_link`. The Wailing Caverns door this family is aimed
// at is (-705, -2045).
constexpr std::uint32_t KALIMDOR = 1;
constexpr std::uint32_t OUTLAND = 530;

constexpr float DOOR_X = -705.f;
constexpr float DOOR_Y = -2045.f;

std::vector<RouteNode> Kalimdor()
{
    return {
        // The Stonetalon half, where the party actually stood.
        {211,  KALIMDOR,  2683.0f,  1466.0f,  233.6f},   // Stonetalon Peak flightMaster
        {571,  KALIMDOR,  2560.9f,  1298.4f,  233.9f},   // Stonetalon Peak spirithealer
        {2959, KALIMDOR,   509.9f,   606.9f,   73.2f},   // Sishir Canyon
        {3056, KALIMDOR,  -466.5f,   -25.6f,   43.4f},   // Camp Aparaje
        // The Barrens half.
        {2510, KALIMDOR,  -799.9f, -1113.6f,  -12.2f},   // Honors Stand
        {3517, KALIMDOR,  -390.9f, -2182.5f,  158.7f},   // Shrine of the Fallen Warrior
        {138,  KALIMDOR, -1073.0f, -3479.0f,   63.0f},   // Ratchet spirithealer
        {405,  KALIMDOR,  -437.1f, -2596.0f,   95.9f},   // The Crossroads flightMaster
        // THE ISLAND. These two are the nearest nodes in the whole table to the
        // door - 173 and 175 yards - and they are the instance portal's own
        // pair, with no walk link to anything overland. They are what makes
        // "nearest node to the aim" the wrong goal.
        {1663, KALIMDOR,  -740.1f, -2214.2f,   16.1f},   // Wailing Caverns exit
        {1409, KALIMDOR,  -753.6f, -2212.8f,   17.5f},   // Wailing Caverns entrance
        // Another map entirely, which must never be read.
        {9001, OUTLAND,   -390.0f, -2182.0f,  158.0f},
    };
}

RouteLink Walk(std::uint32_t from, std::uint32_t to, float yards)
{
    RouteLink link;
    link.from = from;
    link.to = to;
    link.yards = yards;
    link.onFoot = true;
    return link;
}

RouteLink Flight(std::uint32_t from, std::uint32_t to)
{
    RouteLink link;
    link.from = from;
    link.to = to;
    // Upstream prices a flight leg at nothing, which is exactly why it wins
    // every search that is willing to take one.
    link.yards = 0.f;
    link.onFoot = false;
    return link;
}

std::vector<RouteLink> KalimdorLinks()
{
    return {
        Walk(211, 571, 209.f),    Walk(571, 211, 209.f),
        Walk(571, 2959, 2000.f),  Walk(2959, 571, 2000.f),
        Walk(2959, 3056, 1615.f), Walk(3056, 2959, 1615.f),
        Walk(3056, 2510, 972.f),  Walk(2510, 3056, 972.f),
        Walk(2510, 3517, 1500.f), Walk(3517, 2510, 1500.f),
        Walk(138, 405, 1139.f),   Walk(405, 138, 1139.f),
        Walk(405, 3517, 956.f),   Walk(3517, 405, 956.f),
        // The island is joined to itself and to nothing else.
        Walk(1663, 1409, 15.f),   Walk(1409, 1663, 15.f),
        // And the flight that would carry ONE character over the mountain while
        // the other four stayed where they were.
        Flight(211, 405),         Flight(405, 211),
    };
}

RoutePlanLimits Limits()
{
    RoutePlanLimits limits;
    limits.entryNodeYards = 600.f;
    limits.minGainYards = 200.f;
    return limits;
}

bool Uses(RoutePlan const& plan, std::uint32_t id)
{
    for (std::uint32_t node : plan.nodes)
        if (node == id)
            return true;
    return false;
}

// ------------------------------------------------------------- the planner --

void TheJourneyThatNeverConvergedIsPlanned()
{
    // Where 'Og', 'Grog' and 'Bork' actually stood, 4880 yards from the door.
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                         2680.9f, 1469.3f, DOOR_X, DOOR_Y,
                                         Limits());
    CheckVerdict("the party's own position is routed", plan.verdict,
                 RoutePlanVerdict::Planned);
    Check("it starts at the node it stands on", !plan.nodes.empty() && plan.nodes.front() == 211u, true);
    Check("it ends at the nearest node it can WALK to",
          !plan.nodes.empty() && plan.nodes.back() == 3517u, true);
    Check("it crosses the Stonetalon ridge by Sishir Canyon", Uses(plan, 2959u), true);
    Check("and by Camp Aparaje", Uses(plan, 3056u), true);
    // The whole point: the route ends 343 yards from the aim, on open Barrens
    // ground, and that last stretch is the greedy stepper's to walk.
    Check("it finishes near the door without pretending to reach it",
          plan.endsFromAimYards > 300.f && plan.endsFromAimYards < 400.f, true);
}

void TheNearestNodeToTheAimIsNotTheGoal()
{
    // 1663 and 1409 are 173 and 175 yards from the door; 3517 is 343. A planner
    // that picks the goal by distance picks the island, finds no path, and
    // answers "no route" for a journey that has one. This is the measured bug.
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                         509.9f, 606.9f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("Sishir Canyon is routed", plan.verdict, RoutePlanVerdict::Planned);
    Check("the island is not on the route", Uses(plan, 1663u) || Uses(plan, 1409u), false);
    Check("the reachable node is", Uses(plan, 3517u), true);
    // Four nodes, so three legs - the shape the realm's own tables give for
    // this journey: Sishir Canyon, Camp Aparaje, Honors Stand, the Shrine.
    CheckUInt("four nodes, three legs", static_cast<unsigned>(plan.nodes.size()), 4u);
}

void NoLegOfARouteIsAFlight()
{
    // 211 -> 405 by air is one hop and free. On foot it is five hops and 6296
    // yards. The search must take the walk, because the flight leaves four of
    // the five where they stand.
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                         2680.9f, 1469.3f, DOOR_X, DOOR_Y, Limits());
    Check("the route is long, which is what walking round costs",
          plan.yards > 5000.f, true);
    Check("it does not touch the Crossroads flight master", Uses(plan, 405u), false);
}

void AGraphWithOnlyFlightsRefuses()
{
    std::vector<RouteLink> onlyAir;
    onlyAir.push_back(Flight(211, 405));
    onlyAir.push_back(Flight(405, 3517));
    RoutePlan const plan = PlanFootRoute(Kalimdor(), onlyAir, KALIMDOR, 2680.9f,
                                         1469.3f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("a party cannot fly together, so there is no route",
                 plan.verdict, RoutePlanVerdict::NoNearerNode);
}

void AnotherMapIsNotRead()
{
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), 99u, 0.f,
                                         0.f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("a map with no nodes says so", plan.verdict, RoutePlanVerdict::NoGraph);
}

void AWayInHasToBeInReach()
{
    // Middle of the Barrens' western sea, thousands of yards from any node.
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                         -6000.f, -6000.f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("no node within reach", plan.verdict, RoutePlanVerdict::NoEntryNode);
}

void ARouteHasToBeWorthWalking()
{
    // Standing at the Shrine already. Nothing reachable is 200 yards nearer.
    RoutePlan const plan = PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                         -390.9f, -2182.5f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("already as near as the graph gets", plan.verdict,
                 RoutePlanVerdict::NoNearerNode);
}

void NonsenseLimitsAreRefusedAndNotClamped()
{
    RoutePlanLimits bad = Limits();
    bad.entryNodeYards = -600.f;
    CheckVerdict("a negative reach", PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                                   2680.9f, 1469.3f, DOOR_X, DOOR_Y, bad).verdict,
                 RoutePlanVerdict::BadLimits);
    bad = Limits();
    bad.minGainYards = -1.f;
    CheckVerdict("a negative gain", PlanFootRoute(Kalimdor(), KalimdorLinks(), KALIMDOR,
                                                  2680.9f, 1469.3f, DOOR_X, DOOR_Y, bad).verdict,
                 RoutePlanVerdict::BadLimits);
}

void ALinkToANodeThisMapDoesNotHaveIsIgnored()
{
    std::vector<RouteLink> links = KalimdorLinks();
    links.push_back(Walk(211, 9001, 1.f));    // the Outland node
    links.push_back(Walk(211, 4242, 1.f));    // a node that does not exist
    RoutePlan const plan = PlanFootRoute(Kalimdor(), links, KALIMDOR, 2680.9f,
                                         1469.3f, DOOR_X, DOOR_Y, Limits());
    CheckVerdict("a dangling link changes nothing", plan.verdict,
                 RoutePlanVerdict::Planned);
    Check("and the route is the same one", plan.nodes.back() == 3517u, true);
}

// -------------------------------------------------------------- the cursor --

// Points five yards apart, which is what the shipped survey holds: the three
// measured routes had a median gap of 4.2 to 5.2 yards and a maximum of 8.6.
std::vector<RoutePoint> StraightRoute(std::size_t count)
{
    std::vector<RoutePoint> route;
    for (std::size_t i = 0; i < count; ++i)
    {
        RoutePoint point;
        point.x = static_cast<float>(i) * 5.f;
        point.y = 0.f;
        point.z = 40.f;
        route.push_back(point);
    }
    return route;
}

RouteLegLimits LegLimits()
{
    RouteLegLimits limits;
    limits.lookaheadYards = 250.f;
    limits.arrivedYards = 12.f;
    return limits;
}

void TheAimIsTheFurthestPointInsideTheLookahead()
{
    std::vector<RoutePoint> const route = StraightRoute(200);   // 995 yards of it
    RouteCursor cursor;
    RouteAim const aim = RouteLegStep(cursor, route, 0.f, 0.f, LegLimits());
    Check("there is an aim", aim.hasAim, true);
    Check("not the point underfoot", aim.index > 0u, true);
    CheckUInt("the furthest point inside 250 yards", aim.index, 50u);
    Check("and its height comes with it", aim.z == 40.f, true);
}

void AStepThatOvershootsAPointStillAdvancesTheCursor()
{
    // THE BUG THIS RULE EXISTS FOR. A sixty-yard step steps clean over a point
    // five yards wide, so a cursor that only advances on "came within N yards"
    // sticks at nought forever. In the replay that walked 180,000 yards in
    // circles and finished seventy yards from where it started.
    std::vector<RoutePoint> const route = StraightRoute(200);
    RouteCursor cursor;
    RouteLegLimits const limits = LegLimits();

    RouteAim first = RouteLegStep(cursor, route, 0.f, 0.f, limits);
    CheckUInt("the cursor starts at the beginning", cursor.at, 0u);

    // Sixty yards along, and DELIBERATELY three yards off the line, because the
    // stepper is allowed to go round things and never lands on a point exactly.
    RouteAim second = RouteLegStep(cursor, route, 60.f, 3.f, limits);
    CheckUInt("the cursor moved to the point nearest the character", cursor.at, 12u);
    Check("and the aim moved with it", second.index > first.index, true);

    RouteAim third = RouteLegStep(cursor, route, 300.f, -2.f, limits);
    CheckUInt("and again, from a point it never touched", cursor.at, 60u);
    Check("the aim keeps running ahead", third.index > second.index, true);
}

void TheCursorNeverGoesBACKWARDS()
{
    std::vector<RoutePoint> const route = StraightRoute(200);
    RouteCursor cursor;
    RouteLegLimits const limits = LegLimits();
    RouteLegStep(cursor, route, 500.f, 0.f, limits);
    CheckUInt("a hundred points in", cursor.at, 100u);
    // Now the character is knocked back down the route - a fall, a corpse run,
    // a follower released behind the leader. The nearest point is behind it,
    // and it must not be sent back to walk the route again.
    RouteLegStep(cursor, route, 100.f, 0.f, limits);
    CheckUInt("the cursor holds", cursor.at, 100u);
}

void AWALKEDROUTEISSPENT()
{
    std::vector<RoutePoint> const route = StraightRoute(20);   // 95 yards
    RouteCursor cursor;
    RouteAim const aim = RouteLegStep(cursor, route, 95.f, 4.f, LegLimits());
    Check("arrived", aim.arrived, true);
    Check("and there is nothing left to aim at", aim.hasAim, false);
}

void AnEmptyRouteAndNonsenseLimitsBothOfferNothing()
{
    std::vector<RoutePoint> const none;
    RouteCursor cursor;
    Check("an empty route", RouteLegStep(cursor, none, 0.f, 0.f, LegLimits()).hasAim, false);

    std::vector<RoutePoint> const route = StraightRoute(50);
    RouteLegLimits bad = LegLimits();
    bad.lookaheadYards = 0.f;
    Check("a lookahead of nothing", RouteLegStep(cursor, route, 0.f, 0.f, bad).hasAim, false);
    bad = LegLimits();
    bad.arrivedYards = -1.f;
    Check("a negative arrival", RouteLegStep(cursor, route, 0.f, 0.f, bad).hasAim, false);
}

void ACursorPastTheEndIsPulledBackRatherThanReadOff()
{
    // A caller that shortened a route without resetting its cursor - a replan
    // mid-errand - must not read off the end of the vector.
    std::vector<RoutePoint> const route = StraightRoute(10);
    RouteCursor cursor;
    cursor.at = 500u;
    RouteAim const aim = RouteLegStep(cursor, route, 0.f, 0.f, LegLimits());
    Check("the cursor is inside the route again", cursor.at < route.size(), true);
    Check("and the far end is still an aim or an arrival",
          aim.hasAim || aim.arrived, true);
}

void TheWholeRouteIsWalkedPointByPointToTheEnd()
{
    // The end-to-end property, run as the drive would run it: aim, walk sixty
    // yards toward the aim, ask again. It must terminate at the far end rather
    // than stall or loop.
    std::vector<RoutePoint> const route = StraightRoute(400);   // 1995 yards
    RouteCursor cursor;
    RouteLegLimits const limits = LegLimits();
    float x = 0.f;
    bool arrived = false;
    for (int poll = 0; poll < 200 && !arrived; ++poll)
    {
        RouteAim const aim = RouteLegStep(cursor, route, x, 0.f, limits);
        if (aim.arrived)
        {
            arrived = true;
            break;
        }
        if (!aim.hasAim)
            break;
        float const gap = aim.x - x;
        x += gap > 60.f ? 60.f : gap;
    }
    Check("the route is walked to its end", arrived, true);
}

}  // namespace

int main()
{
    TheJourneyThatNeverConvergedIsPlanned();
    TheNearestNodeToTheAimIsNotTheGoal();
    NoLegOfARouteIsAFlight();
    AGraphWithOnlyFlightsRefuses();
    AnotherMapIsNotRead();
    AWayInHasToBeInReach();
    ARouteHasToBeWorthWalking();
    NonsenseLimitsAreRefusedAndNotClamped();
    ALinkToANodeThisMapDoesNotHaveIsIgnored();

    TheAimIsTheFurthestPointInsideTheLookahead();
    AStepThatOvershootsAPointStillAdvancesTheCursor();
    TheCursorNeverGoesBACKWARDS();
    AWALKEDROUTEISSPENT();
    AnEmptyRouteAndNonsenseLimitsBothOfferNothing();
    ACursorPastTheEndIsPulledBackRatherThanReadOff();
    TheWholeRouteIsWalkedPointByPointToTheEnd();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a party can be routed round an obstacle\n");
    return EXIT_SUCCESS;
}
