/*
 * The walk to Lothos Riftwaker has to arrive without dying (#1573).
 *
 * Lothos stands on a walkway above a lava lake. The navmesh holds the walkway
 * as ground except for two slivers about half a yard wide, tagged SLIME, where
 * its two stretches meet. A headless bot's filter excludes SLIME, so every
 * path stopped 40 yards short and the straight finish walked into the lake.
 *
 * tests/fixtures/lothos_walkway/pocket.txt is the REAL navmesh around the
 * walkway, read off the pinned core's tiles: 521 polygons, each with its
 * navmesh flags, its corners in world coordinates and its neighbours. This test
 * searches it twice, once as the stock filter does and once with the polygons
 * OverseerDecisions::PolyAdmitted lets through, and asserts that the second
 * search reaches Lothos on ground, over exactly the two slivers, and never on
 * a MAGMA polygon or on the lake's SLIME polygons 12 yards down.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and it reads
 * the fixture, so run it from the repository root.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>
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

void CheckCount(char const* what, std::size_t got, std::size_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %zu, wanted %zu\n", what, got, want);
    ++failures;
}

struct Poly
{
    std::uint16_t flags{0};
    std::vector<RoutePoint> corners;
    std::vector<std::size_t> neighbours;
    RoutePoint centre;
};

float Plane(RoutePoint const& a, float x, float y)
{
    return std::sqrt((a.x - x) * (a.x - x) + (a.y - y) * (a.y - y));
}

float Dist3(RoutePoint const& a, RoutePoint const& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

std::vector<Poly> LoadFixture()
{
    std::vector<Poly> polys;
    std::ifstream in("tests/fixtures/lothos_walkway/pocket.txt");
    std::string line;
    while (std::getline(in, line))
    {
        std::size_t const bar = line.find('|');
        if (line.compare(0, 5, "poly ") != 0 || bar == std::string::npos)
            continue;
        std::istringstream head(line.substr(5, bar - 5));
        Poly poly;
        unsigned flags = 0;
        unsigned count = 0;
        head >> flags >> count;
        poly.flags = static_cast<std::uint16_t>(flags);
        for (unsigned i = 0; i < count; ++i)
        {
            RoutePoint p;
            head >> p.x >> p.y >> p.z;
            poly.corners.push_back(p);
            poly.centre.x += p.x / count;
            poly.centre.y += p.y / count;
            poly.centre.z += p.z / count;
        }
        std::istringstream tail(line.substr(bar + 1));
        std::size_t n = 0;
        while (tail >> n)
            poly.neighbours.push_back(n);
        polys.push_back(poly);
    }
    return polys;
}

// Is (x, y) inside the polygon's outline, seen from above?
bool Inside(Poly const& poly, float x, float y)
{
    bool in = false;
    std::size_t const n = poly.corners.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
    {
        RoutePoint const& a = poly.corners[i];
        RoutePoint const& b = poly.corners[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

// The polygon standing under (x, y, z): inside its outline, nearest in height.
std::size_t PolyAt(std::vector<Poly> const& polys, float x, float y, float z)
{
    std::size_t best = polys.size();
    float bestGap = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < polys.size(); ++i)
    {
        if (!Inside(polys[i], x, y))
            continue;
        float const gap = std::fabs(polys[i].centre.z - z);
        if (gap < bestGap)
        {
            bestGap = gap;
            best = i;
        }
    }
    return best;
}

template <typename Admit>
struct Search
{
    std::vector<std::size_t> path;   // polygon indices, start first; empty when not reached
    std::vector<bool> reached;
};

// Dijkstra over the polygons `admit` lets through, by centre-to-centre yards.
template <typename Admit>
Search<Admit> Walk(std::vector<Poly> const& polys, std::size_t from, std::size_t to, Admit admit)
{
    Search<Admit> out;
    out.reached.assign(polys.size(), false);
    std::vector<float> cost(polys.size(), std::numeric_limits<float>::max());
    std::vector<std::size_t> before(polys.size(), polys.size());
    typedef std::pair<float, std::size_t> Item;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[from] = 0.f;
    open.push(Item(0.f, from));
    while (!open.empty())
    {
        Item const top = open.top();
        open.pop();
        if (top.first > cost[top.second])
            continue;
        out.reached[top.second] = true;
        for (std::size_t next : polys[top.second].neighbours)
        {
            if (!admit(polys[next]))
                continue;
            float const c = top.first + Dist3(polys[top.second].centre, polys[next].centre);
            if (c < cost[next])
            {
                cost[next] = c;
                before[next] = top.second;
                open.push(Item(c, next));
            }
        }
    }
    if (out.reached[to])
        for (std::size_t at = to; at != polys.size(); at = before[at])
            out.path.insert(out.path.begin(), at);
    return out;
}

// The start every bot path ended at, 40 yards short of Lothos.
constexpr float START_X = -7534.6f;
constexpr float START_Y = -1072.5f;
constexpr float START_Z = 182.0f;

void TheStockFilterStopsShort(std::vector<Poly> const& polys, std::size_t start, std::size_t door)
{
    auto stock = [](Poly const& p)
    { return (p.flags & (W::NAV_GROUND | W::NAV_WATER)) != 0 && (p.flags & W::NAV_SLIME) == 0; };
    Search<decltype(stock)> const found = Walk(polys, start, door, stock);
    Check("the stock filter does not reach Lothos", !found.path.empty(), false);
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < polys.size(); ++i)
        if (found.reached[i])
            nearest = std::min(nearest, Plane(polys[i].centre, W::LOTHOS_X, W::LOTHOS_Y));
    Check("the stock filter stops at least 25 yards short", nearest >= 25.f, true);
}

void TheAdmittedPolygonsReachLothos(std::vector<Poly> const& polys, std::size_t start,
                                    std::size_t door)
{
    auto admitted = [](Poly const& p) { return PolyAdmitted(p.flags, p.corners); };
    Search<decltype(admitted)> const found = Walk(polys, start, door, admitted);
    Check("the admitted polygons reach Lothos", !found.path.empty(), true);
    if (found.path.empty())
        return;

    Poly const& last = polys[found.path.back()];
    float nearestCorner = Plane(last.centre, W::LOTHOS_X, W::LOTHOS_Y);
    Check("the route ends within 5 yards of Lothos",
          WalkwayRouteArrives(last.centre.x, last.centre.y, last.centre.z, W::LOTHOS_X,
                              W::LOTHOS_Y, W::LOTHOS_Z) ||
              nearestCorner <= W::ARRIVE_YARDS,
          true);

    std::size_t slime = 0;
    bool anyMagma = false;
    bool anyLow = false;
    for (std::size_t index : found.path)
    {
        Poly const& p = polys[index];
        if ((p.flags & W::NAV_MAGMA) != 0)
            anyMagma = true;
        if ((p.flags & W::NAV_SLIME) != 0)
            ++slime;
        if (p.centre.z < 175.f)
            anyLow = true;
    }
    Check("no polygon on the route is MAGMA", anyMagma, false);
    Check("no polygon on the route is down in the lake", anyLow, false);
    CheckCount("the route crosses exactly the two slivers", slime, 2);
}

void TheLakeStaysClosed(std::vector<Poly> const& polys)
{
    // Every SLIME polygon in the fixture but the two slivers is the lake's, and
    // the lake must never be admitted.
    std::size_t slivers = 0;
    std::size_t lake = 0;
    std::size_t lakeAdmitted = 0;
    for (Poly const& p : polys)
    {
        if ((p.flags & W::NAV_SLIME) == 0)
            continue;
        if (PolyAdmitted(p.flags, p.corners))
            ++slivers;
        else
            ++lake;
        if (p.centre.z < 175.f && PolyAdmitted(p.flags, p.corners))
            ++lakeAdmitted;
    }
    CheckCount("two slivers are admitted", slivers, 2);
    Check("the rest of the slime, the lake, is not admitted", lake >= 50, true);
    CheckCount("no polygon down in the lake is admitted", lakeAdmitted, 0);
}

void PolygonRules()
{
    std::vector<RoutePoint> const onSeam = {{-7533.7f, -1071.4f, 181.3f},
                                            {-7532.5f, -1072.0f, 181.3f},
                                            {-7534.5f, -1070.0f, 181.3f}};
    std::vector<RoutePoint> const lake = {{-7533.7f, -1071.4f, 169.0f},
                                          {-7532.5f, -1072.0f, 169.0f}};
    std::vector<RoutePoint> const elsewhere = {{-7500.0f, -1000.0f, 181.3f}};
    std::vector<RoutePoint> const none;
    Check("ground is admitted", PolyAdmitted(W::NAV_GROUND, elsewhere), true);
    Check("water is admitted", PolyAdmitted(W::NAV_WATER, elsewhere), true);
    Check("a polygon with no passable flag is not", PolyAdmitted(0, elsewhere), false);
    Check("magma is never admitted", PolyAdmitted(W::NAV_MAGMA, onSeam), false);
    Check("magma with ground is never admitted",
          PolyAdmitted(W::NAV_MAGMA | W::NAV_GROUND, onSeam), false);
    Check("steep ground is never admitted", PolyAdmitted(W::NAV_GROUND_STEEP, onSeam), false);
    Check("slime on a seam is admitted", PolyAdmitted(W::NAV_SLIME, onSeam), true);
    Check("slime at the lake's height under a seam is not", PolyAdmitted(W::NAV_SLIME, lake),
          false);
    Check("slime anywhere else is not", PolyAdmitted(W::NAV_SLIME, elsewhere), false);
    Check("slime with no corners is not", PolyAdmitted(W::NAV_SLIME, none), false);
}

void TheAimIsThePocket()
{
    Check("Lothos's spawn is on the walkway",
          AimOnMoltenCoreWalkway(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z), true);
    Check("the door's staging point is on the walkway",
          AimOnMoltenCoreWalkway(0, -7508.32f, -1039.74f, 180.912f), true);
    Check("another map is not", AimOnMoltenCoreWalkway(1, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z),
          false);
    Check("the Spire ring is not", AimOnMoltenCoreWalkway(0, -7532.f, -1222.f, 286.f), false);
    Check("the Blackrock Depths door is not",
          AimOnMoltenCoreWalkway(0, -7176.63f, -937.667f, 170.206f), false);
    Check("the lake below him is not", AimOnMoltenCoreWalkway(0, -7499.f, -1019.f, 167.f), false);
    Check("a NaN is not", AimOnMoltenCoreWalkway(0, std::nanf(""), -1039.f, 181.f), false);

    Check("from the walkway's start the straight finish is lethal",
          StraightFinishIsLethal(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, START_X, START_Y,
                                 START_Z),
          true);
    Check("from the Spire ring it is not asked",
          StraightFinishIsLethal(0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z, -7532.f, -1222.f,
                                 286.f),
          false);
    Check("for another aim it is not asked",
          StraightFinishIsLethal(0, -7176.63f, -937.667f, 170.206f, START_X, START_Y, START_Z),
          false);
}

void TheArrivalRule()
{
    Check("4 yards off arrives",
          WalkwayRouteArrives(-7512.f, -1039.f, 181.f, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z),
          true);
    Check("40 yards short does not",
          WalkwayRouteArrives(START_X, START_Y, START_Z, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z),
          false);
    Check("the right place 20 yards up does not",
          WalkwayRouteArrives(W::LOTHOS_X, W::LOTHOS_Y, 201.f, W::LOTHOS_X, W::LOTHOS_Y,
                              W::LOTHOS_Z),
          false);
}

RouteLink Foot(std::uint32_t from, std::uint32_t to, float yards)
{
    RouteLink link;
    link.from = from;
    link.to = to;
    link.yards = yards;
    link.onFoot = true;
    return link;
}

RouteNode Node(std::uint32_t id, float x, float y, float z)
{
    RouteNode node;
    node.id = id;
    node.mapId = 0;
    node.x = x;
    node.y = y;
    node.z = z;
    return node;
}

void TheSurveyLinkThroughTheRoofIsDropped()
{
    // Node 7 is where the character is, 3380 is the Dracodar, 1485 and 1486 are
    // the Molten Core entrance nodes, and 9 is a ledge on the walkway's
    // approach. The link 3380 -> 1485 is the one that climbs the outside of the
    // mountain.
    std::vector<RouteNode> const nodes = {
        Node(7, -7000.f, -1500.f, 200.f),
        Node(3380, -7100.f, -1300.f, 200.f),
        Node(1485, -7508.f, -1040.f, 181.f),
        Node(1486, -7510.f, -1041.f, 181.f),
        Node(9, -7534.f, -1072.f, 182.f),
    };
    std::vector<RouteLink> links = {Foot(7, 3380, 300.f), Foot(3380, 1485, 500.f),
                                    Foot(3380, 1486, 500.f), Foot(3380, 9, 600.f),
                                    Foot(1485, 3380, 500.f)};

    RoutePlanLimits const limits;
    RoutePlan const before =
        PlanFootRoute(nodes, links, 0, -7000.f, -1500.f, W::LOTHOS_X, W::LOTHOS_Y, limits);
    Check("the survey as shipped plans a route", before.verdict == RoutePlanVerdict::Planned,
          true);
    Check("...that ends on the bogus link",
          !before.nodes.empty() && (before.nodes.back() == 1485 || before.nodes.back() == 1486),
          true);

    CheckCount("two links are dropped for the walkway",
               DropBogusDoorLinks(links, 0, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z), 2);
    CheckCount("the reverse link and the others stay", links.size(), 3);
    RoutePlan const after =
        PlanFootRoute(nodes, links, 0, -7000.f, -1500.f, W::LOTHOS_X, W::LOTHOS_Y, limits);
    Check("the plan no longer ends at the door nodes",
          after.verdict != RoutePlanVerdict::Planned ||
              (after.nodes.back() != 1485 && after.nodes.back() != 1486),
          true);
    Check("...and uses no dropped link", [&]
          {
              for (std::size_t i = 0; i + 1 < after.nodes.size(); ++i)
                  if (after.nodes[i] == 3380 &&
                      (after.nodes[i + 1] == 1485 || after.nodes[i + 1] == 1486))
                      return false;
              return true;
          }(),
          true);

    std::vector<RouteLink> other = {Foot(3380, 1485, 500.f)};
    CheckCount("another aim drops nothing",
               DropBogusDoorLinks(other, 0, -7176.63f, -937.667f, 170.206f), 0);
    CheckCount("another map drops nothing",
               DropBogusDoorLinks(other, 1, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z), 0);
    CheckCount("...and the link is still there", other.size(), 1);
}
}  // namespace

int main()
{
    std::vector<Poly> const polys = LoadFixture();
    Check("the fixture loaded", polys.size() > 500, true);
    std::size_t const start = PolyAt(polys, START_X, START_Y, START_Z);
    std::size_t const door = PolyAt(polys, W::LOTHOS_X, W::LOTHOS_Y, W::LOTHOS_Z);
    Check("the start polygon is in the fixture", start < polys.size(), true);
    Check("the door polygon is in the fixture", door < polys.size(), true);
    if (start < polys.size() && door < polys.size())
    {
        TheStockFilterStopsShort(polys, start, door);
        TheAdmittedPolygonsReachLothos(polys, start, door);
    }
    TheLakeStaysClosed(polys);
    PolygonRules();
    TheAimIsThePocket();
    TheArrivalRule();
    TheSurveyLinkThroughTheRoofIsDropped();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
