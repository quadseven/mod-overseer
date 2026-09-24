/*
 * A character on a surveyed road is on the survey, and a leader walking his
 * route is closing the gap however the straight line reads.
 *
 * Measured 2026-09-24 on the dev realm. The Alliance leader was walked to the
 * Zul'Farrak staging point (-6793.47, -2890.64) from the ramp that climbs out
 * of Un'Goro Crater into Tanaris. The survey records that ramp as one walk link
 * from Thistleshrub Valley (2595) to the Marshlands (3032), 1906 yards and 457
 * points, and the reverse link 3032 -> 2595 as 1998 yards and 407 points, with
 * no node between. At 1780 yards from the door the planner answered "no travel
 * node within reach": the nearest node stood more than the 600-yard entry
 * radius away. The leader was handed the door itself across the crater wall,
 * read 16 and then 26 yards inside the wall, was lifted onto a ledge, was
 * refused every bearing from it, and fell 29 yards.
 *
 * FROM_X, FROM_Y is the point 1780 yards from the door (the logged distance)
 * with no node within 600 yards that lies nearest the ramp; the log gives the
 * distance and the verdict, not the coordinates.
 *
 * The nodes are every survey node on map 1 in the box x -9400..-6400,
 * y -3400..-1400, and the walk links among the ones a route here uses, from
 * the shipped survey's own rows (playerbots_travelnode, _link). The ramp points
 * are every fourth row of playerbots_travelnode_path for each direction, and
 * its last row.
 *
 * The same run lost two attempts to the twelve-minute GATHERING clock with the
 * leader walking the route the survey gave him (6587 yards, 9 nodes, out of
 * the crater by this ramp): 976 yards out at the start, 1807 at Thistleshrub
 * Valley twelve minutes later, so no straight-line reading ever set a new best.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::ChooseRoadJoin;
using OverseerDecisions::JoinRoad;
using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RoadJoin;
using OverseerDecisions::RoadJoinOption;
using OverseerDecisions::RoadMayPassNear;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteMark;
using OverseerDecisions::RouteMarkAdvanced;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::StagingClock;
using OverseerDecisions::StagingClockAfterReading;

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

RoutePoint P(float x, float y, float z)
{
    RoutePoint p;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

std::vector<RouteNode> const nodes{
    Node(48, -7207.f, -2439.f, -218.f),        Node(199, -7746.9f, -3012.54f, 40.7f),
    Node(800, -9048.19f, -2724.85f, 37.4f),    Node(1439, -6773.49f, -2889.77f, 11.1f),
    Node(1693, -6795.56f, -2890.72f, 8.9f),    Node(2595, -8923.98f, -2244.71f, 9.0f),
    Node(2948, -6995.66f, -1573.08f, -274.2f), Node(3032, -7419.17f, -1882.49f, -273.8f),
    Node(3041, -8448.89f, -2984.24f, 8.7f),    Node(3066, -7145.69f, -2948.89f, 10.3f),
    Node(3181, -9117.17f, -3016.26f, 48.0f),   Node(3236, -8314.71f, -3365.48f, 10.3f),
    Node(3285, -6616.95f, -1416.21f, -271.7f), Node(3426, -9381.94f, -2616.78f, 13.0f),
    Node(3437, -7830.6f, -3341.54f, 60.1f),    Node(3487, -7931.32f, -2546.3f, 8.9f),
    Node(3653, -6874.17f, -2895.55f, 9.8f),    Node(3663, -6942.2f, -2378.71f, -206.9f),
};

std::vector<RouteLink> const links{
    Walk(48, 3032, 850.5f),   Walk(48, 3663, 441.f),     Walk(199, 3487, 755.7f),
    Walk(199, 3653, 941.7f),  Walk(800, 2595, 709.3f),   Walk(800, 3041, 701.7f),
    Walk(1693, 3653, 82.8f),  Walk(2595, 800, 704.5f),   Walk(2595, 3032, 1905.8f),
    Walk(3032, 48, 822.7f),   Walk(3032, 800, 2327.7f),  Walk(3032, 2595, 1997.5f),
    Walk(3032, 3663, 905.5f), Walk(3041, 199, 856.5f),   Walk(3041, 3487, 793.3f),
    Walk(3066, 199, 842.1f),  Walk(3066, 3487, 1105.6f), Walk(3066, 3653, 281.3f),
    Walk(3487, 199, 641.f),   Walk(3487, 2595, 1155.7f), Walk(3487, 3041, 937.6f),
    Walk(3487, 3066, 1207.9f), Walk(3487, 3653, 1265.9f), Walk(3653, 1693, 82.9f),
    Walk(3653, 3066, 284.3f), Walk(3663, 48, 441.f),     Walk(3663, 3032, 905.5f),
};

// 2595 (Thistleshrub Valley, on the Tanaris plateau) down to 3032.
std::vector<RoutePoint> const rampDown{
    P(-8924.0f, -2244.7f, 9.0f), P(-8898.4f, -2244.0f, 11.3f), P(-8880.4f, -2228.2f, 9.6f),
    P(-8870.0f, -2219.4f, 10.0f), P(-8842.0f, -2218.2f, 9.6f), P(-8826.0f, -2217.6f, 9.6f),
    P(-8810.0f, -2216.9f, 9.6f), P(-8794.0f, -2216.3f, 9.6f), P(-8778.1f, -2215.7f, 9.8f),
    P(-8762.1f, -2215.0f, 11.0f), P(-8746.1f, -2214.4f, 11.7f), P(-8730.2f, -2216.0f, 11.2f),
    P(-8714.2f, -2217.6f, 11.8f), P(-8696.0f, -2218.7f, 10.6f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f),
    P(-8684.0f, -2218.7f, 9.7f), P(-8684.0f, -2218.7f, 9.7f), P(-8678.2f, -2213.2f, 9.8f),
    P(-8663.6f, -2199.5f, 10.8f), P(-8641.3f, -2192.0f, 9.7f), P(-8610.2f, -2184.4f, 9.6f),
    P(-8583.0f, -2177.7f, 10.1f), P(-8561.3f, -2161.5f, 10.2f), P(-8548.5f, -2146.2f, 9.8f),
    P(-8539.7f, -2132.8f, 9.5f), P(-8530.9f, -2119.4f, 9.3f), P(-8522.1f, -2106.1f, 4.1f),
    P(-8513.3f, -2092.7f, 0.2f), P(-8499.1f, -2089.0f, -2.4f), P(-8483.2f, -2088.6f, -6.5f),
    P(-8467.2f, -2088.1f, -12.4f), P(-8463.5f, -2080.1f, -13.3f), P(-8460.2f, -2068.1f, -9.6f),
    P(-8445.1f, -2064.1f, -13.3f), P(-8431.7f, -2055.5f, -2.7f), P(-8418.3f, -2054.2f, -13.0f),
    P(-8413.1f, -2067.6f, -26.1f), P(-8397.6f, -2070.6f, -32.2f), P(-8377.6f, -2071.6f, -38.8f),
    P(-8361.6f, -2072.0f, -45.6f), P(-8345.6f, -2072.3f, -51.7f), P(-8329.6f, -2072.6f, -57.5f),
    P(-8313.6f, -2073.0f, -63.6f), P(-8297.6f, -2073.3f, -69.8f), P(-8281.6f, -2073.7f, -77.1f),
    P(-8265.6f, -2074.0f, -84.8f), P(-8249.6f, -2074.3f, -91.7f), P(-8233.6f, -2074.7f, -97.0f),
    P(-8217.6f, -2075.0f, -101.9f), P(-8201.7f, -2075.3f, -105.9f), P(-8185.7f, -2075.5f, -113.2f),
    P(-8169.8f, -2073.3f, -118.4f), P(-8154.1f, -2070.2f, -123.4f), P(-8138.6f, -2074.7f, -127.1f),
    P(-8124.3f, -2081.8f, -134.0f), P(-8110.0f, -2089.0f, -142.6f), P(-8097.5f, -2098.8f, -151.0f),
    P(-8082.6f, -2102.6f, -160.1f), P(-8066.6f, -2104.1f, -168.4f), P(-8050.7f, -2105.5f, -176.5f),
    P(-8034.8f, -2106.9f, -184.6f), P(-8018.9f, -2108.4f, -193.6f), P(-8002.9f, -2109.8f, -202.4f),
    P(-7987.0f, -2111.2f, -209.1f), P(-7971.4f, -2113.9f, -214.8f), P(-7956.7f, -2120.4f, -219.4f),
    P(-7941.9f, -2126.3f, -225.0f), P(-7926.5f, -2130.7f, -232.9f), P(-7911.4f, -2132.1f, -242.6f),
    P(-7897.1f, -2124.8f, -254.9f), P(-7882.9f, -2117.6f, -266.5f), P(-7869.4f, -2109.3f, -268.3f),
    P(-7858.0f, -2098.0f, -266.2f), P(-7846.7f, -2086.7f, -271.4f), P(-7832.6f, -2072.6f, -271.3f),
    P(-7823.4f, -2051.3f, -272.2f), P(-7812.7f, -2034.8f, -272.7f), P(-7798.7f, -2036.4f, -272.6f),
    P(-7782.6f, -2048.2f, -270.3f), P(-7765.8f, -2059.0f, -272.1f), P(-7742.1f, -2074.0f, -272.5f),
    P(-7719.6f, -2081.1f, -272.5f), P(-7692.1f, -2076.1f, -272.6f), P(-7670.5f, -2055.3f, -271.7f),
    P(-7654.6f, -2053.2f, -271.2f), P(-7634.8f, -2050.5f, -270.5f), P(-7615.0f, -2047.9f, -271.1f),
    P(-7594.7f, -2026.7f, -273.6f), P(-7578.2f, -2044.1f, -272.9f), P(-7552.8f, -2048.1f, -272.0f),
    P(-7529.1f, -2050.2f, -272.7f), P(-7509.9f, -2040.2f, -271.2f), P(-7504.9f, -2016.8f, -271.4f),
    P(-7498.9f, -1993.6f, -271.1f), P(-7490.9f, -1970.9f, -271.4f), P(-7479.5f, -1954.2f, -270.7f),
    P(-7456.9f, -1931.5f, -269.1f), P(-7438.3f, -1910.8f, -270.8f), P(-7427.1f, -1894.2f, -272.3f),
    P(-7419.2f, -1882.5f, -273.8f),
};

// 3032 (the Marshlands, on the crater floor) up to 2595.
std::vector<RoutePoint> const rampUp{
    P(-7419.2f, -1882.5f, -273.8f), P(-7428.6f, -1897.7f, -272.1f), P(-7456.6f, -1898.8f, -271.6f),
    P(-7484.4f, -1895.8f, -271.3f), P(-7511.9f, -1890.9f, -271.7f), P(-7531.7f, -1891.2f, -273.0f),
    P(-7554.0f, -1899.2f, -272.6f), P(-7573.6f, -1903.1f, -270.1f), P(-7597.2f, -1907.8f, -271.2f),
    P(-7616.8f, -1911.7f, -270.7f), P(-7640.4f, -1916.4f, -268.5f), P(-7656.0f, -1919.5f, -271.1f),
    P(-7672.8f, -1934.1f, -271.5f), P(-7689.2f, -1956.6f, -271.8f), P(-7701.0f, -1977.5f, -271.5f),
    P(-7712.2f, -1988.8f, -270.1f), P(-7723.8f, -2004.8f, -269.3f), P(-7721.6f, -2026.8f, -270.7f),
    P(-7726.5f, -2052.3f, -272.4f), P(-7755.8f, -2065.2f, -271.5f), P(-7782.3f, -2073.9f, -271.1f),
    P(-7805.7f, -2078.8f, -271.5f), P(-7825.5f, -2076.6f, -272.7f), P(-7842.6f, -2085.0f, -271.9f),
    P(-7858.0f, -2097.7f, -266.3f), P(-7872.0f, -2112.0f, -269.1f), P(-7889.9f, -2120.9f, -261.0f),
    P(-7904.2f, -2128.1f, -249.8f), P(-7918.9f, -2133.2f, -236.8f), P(-7934.7f, -2130.7f, -229.3f),
    P(-7950.5f, -2128.1f, -222.6f), P(-7966.3f, -2125.5f, -216.4f), P(-7982.1f, -2122.9f, -208.8f),
    P(-7997.9f, -2120.6f, -204.4f), P(-8013.8f, -2119.1f, -197.8f), P(-8029.7f, -2117.5f, -189.9f),
    P(-8045.7f, -2116.0f, -181.7f), P(-8061.6f, -2114.5f, -173.3f), P(-8077.5f, -2112.9f, -165.1f),
    P(-8092.9f, -2108.9f, -155.3f), P(-8107.6f, -2102.8f, -145.8f), P(-8122.4f, -2096.7f, -135.8f),
    P(-8138.1f, -2095.0f, -124.7f), P(-8154.1f, -2094.7f, -118.3f), P(-8166.7f, -2090.9f, -115.8f),
    P(-8182.5f, -2092.9f, -112.1f), P(-8192.8f, -2092.0f, -112.0f), P(-8212.8f, -2090.7f, -109.2f),
    P(-8228.5f, -2088.0f, -102.4f), P(-8244.2f, -2084.8f, -94.5f), P(-8259.8f, -2081.5f, -85.5f),
    P(-8275.6f, -2078.8f, -79.5f), P(-8291.5f, -2077.8f, -72.5f), P(-8307.5f, -2076.7f, -65.8f),
    P(-8323.5f, -2075.7f, -59.8f), P(-8339.4f, -2074.6f, -54.3f), P(-8355.4f, -2073.6f, -48.3f),
    P(-8371.4f, -2072.5f, -41.6f), P(-8387.3f, -2071.5f, -33.6f), P(-8403.2f, -2070.0f, -29.8f),
    P(-8412.1f, -2061.4f, -20.9f), P(-8424.3f, -2051.1f, -5.7f), P(-8438.3f, -2058.9f, -6.6f),
    P(-8451.6f, -2067.8f, -13.7f), P(-8466.8f, -2071.2f, -9.9f), P(-8481.6f, -2077.1f, -6.3f),
    P(-8496.2f, -2083.6f, -3.1f), P(-8510.9f, -2090.2f, -0.5f), P(-8522.7f, -2085.3f, 1.5f),
    P(-8537.0f, -2078.1f, 7.8f), P(-8558.9f, -2068.8f, 9.7f), P(-8574.9f, -2067.4f, 10.3f),
    P(-8590.8f, -2065.9f, 9.6f), P(-8606.7f, -2064.5f, 10.3f), P(-8622.6f, -2063.1f, 13.8f),
    P(-8638.6f, -2061.6f, 17.3f), P(-8654.5f, -2060.2f, 17.5f), P(-8674.0f, -2064.7f, 22.7f),
    P(-8684.0f, -2070.1f, 34.8f), P(-8671.4f, -2067.7f, 22.1f), P(-8659.2f, -2073.6f, 19.9f),
    P(-8658.8f, -2093.5f, 15.9f), P(-8661.0f, -2109.3f, 17.9f), P(-8677.2f, -2116.4f, 17.4f),
    P(-8692.9f, -2119.5f, 24.3f), P(-8692.4f, -2124.6f, 23.5f), P(-8681.9f, -2136.2f, 17.4f),
    P(-8675.2f, -2149.9f, 13.8f), P(-8686.8f, -2166.2f, 19.4f), P(-8698.5f, -2182.4f, 14.7f),
    P(-8712.9f, -2187.4f, 18.4f), P(-8724.7f, -2176.6f, 15.1f), P(-8738.5f, -2179.1f, 11.2f),
    P(-8756.8f, -2182.4f, 10.4f), P(-8783.8f, -2189.9f, 10.0f), P(-8810.7f, -2197.3f, 9.1f),
    P(-8836.9f, -2207.0f, 9.7f), P(-8867.0f, -2218.1f, 9.6f), P(-8878.1f, -2228.3f, 10.2f),
    P(-8890.5f, -2237.2f, 11.1f), P(-8902.9f, -2247.3f, 10.2f), P(-8917.5f, -2246.6f, 9.6f),
    P(-8924.0f, -2244.7f, 9.0f),
};

constexpr float FROM_X = -8356.f;
constexpr float FROM_Y = -2039.f;
constexpr float AIM_X = -6793.47f;
constexpr float AIM_Y = -2890.64f;
constexpr float JOIN = 200.f;

RoutePlanLimits Limits()
{
    RoutePlanLimits limits;
    limits.entryNodeYards = 600.f;
    limits.minGainYards = 200.f;
    return limits;
}

RouteNode const& Find(std::uint32_t id)
{
    for (RouteNode const& n : nodes)
        if (n.id == id)
            return n;
    return nodes.front();
}

void TheRampIsOutOfReachOfEveryNode()
{
    // The failure as it was: this is what the planner said at 03:47:04.
    RoutePlan const plan =
        PlanFootRoute(nodes, links, 1, FROM_X, FROM_Y, AIM_X, AIM_Y, Limits());
    Check("no node is within 600 yards of the leader on the ramp",
          plan.verdict == RoutePlanVerdict::NoEntryNode);
}

void OnlyTheRampPassesNear()
{
    Check("the ramp down passes near",
          RoadMayPassNear(-8923.98f, -2244.71f, -7419.17f, -1882.49f, 1905.8f, FROM_X,
                          FROM_Y, JOIN));
    Check("the ramp up passes near",
          RoadMayPassNear(-7419.17f, -1882.49f, -8923.98f, -2244.71f, 1997.5f, FROM_X,
                          FROM_Y, JOIN));
    Check("Dunemaul to the Abyssal Sands does not",
          !RoadMayPassNear(-8448.89f, -2984.24f, -7746.9f, -3012.54f, 856.5f, FROM_X,
                           FROM_Y, JOIN));
    Check("the Noxious Lair to Zul'Farrak does not",
          !RoadMayPassNear(-7931.32f, -2546.3f, -6874.17f, -2895.55f, 1265.9f, FROM_X,
                           FROM_Y, JOIN));
    Check("a link with no length is never near",
          !RoadMayPassNear(0.f, 0.f, 10.f, 0.f, -1.f, 5.f, 0.f, JOIN));

    // The bound is exact enough to be honest: every recorded point of the ramp
    // is inside it for a character standing on that point.
    bool all = true;
    for (RoutePoint const& p : rampDown)
        all = all && RoadMayPassNear(-8923.98f, -2244.71f, -7419.17f, -1882.49f,
                                     1905.8f, p.x, p.y, 0.f);
    Check("every recorded point lies inside its own link's ellipse", all);
}

void TheLeaderJoinsTheRampNearHim()
{
    RoadJoin const down = JoinRoad(rampDown, FROM_X, FROM_Y, JOIN);
    Check("the leader is beside the ramp down", down.found && down.yards < 60.f);
    Check("the join is mid-ramp, not at either node",
          down.found && down.index > 0 && down.index + 1 < rampDown.size());
    RoadJoin const up = JoinRoad(rampUp, FROM_X, FROM_Y, JOIN);
    Check("the leader is beside the ramp up", up.found && up.yards < 60.f);
    Check("what is left of the ramp up is part of it",
          up.found && up.remainingYards > 300.f && up.remainingYards < 1998.f);

    Check("nothing is joined out of reach", !JoinRoad(rampDown, -9000.f, -1000.f, JOIN).found);
    Check("an empty road has no join", !JoinRoad({}, FROM_X, FROM_Y, JOIN).found);
    // A character beside a link's far end is beside that node.
    std::vector<RoutePoint> const two{P(0.f, 0.f, 0.f), P(100.f, 0.f, 0.f)};
    Check("the far end is never the join", !JoinRoad(two, 100.f, 1.f, 5.f).found);
}

void UpTheRampAndOnToTheDoorIsChosen()
{
    RoutePlanLimits const limits = Limits();
    RoadJoin const down = JoinRoad(rampDown, FROM_X, FROM_Y, JOIN);
    RoadJoin const up = JoinRoad(rampUp, FROM_X, FROM_Y, JOIN);

    std::vector<RoadJoinOption> options;
    // Down to the Marshlands, onward from 3032.
    {
        RouteNode const& end = Find(3032);
        RoutePlan const onward =
            PlanFootRoute(nodes, links, 1, end.x, end.y, AIM_X, AIM_Y, limits);
        RoadJoinOption o;
        o.remainingYards = down.remainingYards;
        if (onward.verdict == RoutePlanVerdict::Planned)
        {
            o.onwardYards = onward.yards;
            o.endsFromAimYards = onward.endsFromAimYards;
        }
        else
            o.endsFromAimYards = 1000.f;
        options.push_back(o);
    }
    // Up to Thistleshrub Valley, onward from 2595.
    RoutePlan upOnward;
    {
        RouteNode const& end = Find(2595);
        upOnward = PlanFootRoute(nodes, links, 1, end.x, end.y, AIM_X, AIM_Y, limits);
        RoadJoinOption o;
        o.remainingYards = up.remainingYards;
        o.onwardYards = upOnward.yards;
        o.endsFromAimYards = upOnward.endsFromAimYards;
        options.push_back(o);
    }
    Check("a route is planned on from the top of the ramp",
          upOnward.verdict == RoutePlanVerdict::Planned && !upOnward.nodes.empty() &&
              upOnward.nodes.front() == 2595 && upOnward.nodes.back() == 1693);
    std::size_t const chosen = ChooseRoadJoin(options, 1780.f, limits.minGainYards);
    Check("up the ramp is the way to the door", chosen == 1);
}

void AJoinThatGetsNowhereIsRefused()
{
    std::vector<RoadJoinOption> options(1);
    options[0].remainingYards = 300.f;
    options[0].endsFromAimYards = 1700.f;
    Check("finishing 80 yards nearer is not worth a route",
          ChooseRoadJoin(options, 1780.f, 200.f) == options.size());
    options[0].endsFromAimYards = -1.f;
    Check("an unknown finish is refused", ChooseRoadJoin(options, 1780.f, 200.f) == 1);
    Check("no options, no choice", ChooseRoadJoin({}, 1780.f, 200.f) == 0);
    std::vector<RoadJoinOption> two(2);
    two[0].remainingYards = 900.f;
    two[0].onwardYards = 2000.f;
    two[1].remainingYards = 1500.f;
    two[1].onwardYards = 900.f;
    Check("the least ground in total wins, not the shortest rest of the road",
          ChooseRoadJoin(two, 1780.f, 200.f) == 1);
}

void AStepAlongTheSameRouteIsProgress()
{
    Check("point 12 to 13 on one route is progress",
          RouteMarkAdvanced(RouteMark{7, 12}, RouteMark{7, 13}));
    Check("standing still is not", !RouteMarkAdvanced(RouteMark{7, 13}, RouteMark{7, 13}));
    Check("a new route is not, whatever its cursor reads",
          !RouteMarkAdvanced(RouteMark{7, 0}, RouteMark{8, 40}));
    Check("no route is not", !RouteMarkAdvanced(RouteMark{7, 5}, RouteMark{}));
    Check("the first reading is not", !RouteMarkAdvanced(RouteMark{}, RouteMark{7, 3}));

    // A leader whose errand is ended and re-armed every few seconds gets a new
    // plan each time, and each plan's cursor lands wherever that plan's points
    // put him - measured, 14 re-arms in a minute at 02:51. A bare cursor reads
    // every climb between two of those plans as a step along a route.
    long const landed[] = {31, 40, 12, 55, 18, 61, 7, 44, 29, 70, 3, 38, 22, 49};
    RouteMark seen;
    int progress = 0;
    for (std::uint64_t plan = 1; plan <= 14; ++plan)
    {
        RouteMark const now{plan, landed[plan - 1]};
        progress += RouteMarkAdvanced(seen, now);
        seen = now;
        progress += RouteMarkAdvanced(seen, now);
    }
    Check("a re-arm loop that walks nowhere never reads as progress", progress == 0);
}

void TheClockWaitsForALeaderOnHisRoute()
{
    constexpr float PROGRESS = 50.f;
    constexpr time_t BACKSTOP = 12 * 60;
    time_t const start = 5000;

    // The measured walk: out of the crater first, so the straight line only
    // grows. Every poll the leader is further along the same route.
    StagingClock walked{start, -1.f};
    StagingClock stood{start, -1.f};
    RouteMark seen;
    for (time_t t = 0; t <= 14 * 60; t += 5)
    {
        float const yards = 976.f + static_cast<float>(t) * 1.15f;
        RouteMark const now{3, static_cast<long>(t / 5)};
        bool const on = RouteMarkAdvanced(seen, now);
        seen = now;
        walked = StagingClockAfterReading(walked, true, yards, start + t, PROGRESS, on);
        stood = StagingClockAfterReading(stood, true, yards, start + t, PROGRESS, false);
    }
    time_t const end = start + 14 * 60;
    Check("a leader walking his route away from the door keeps the run open",
          end - walked.since <= BACKSTOP);
    Check("the same straight line with no route is written off, as it was",
          end - stood.since > BACKSTOP);
    Check("the route does not move the best distance", walked.bestYards == 976.f);

    StagingClock c{start, 900.f};
    c = StagingClockAfterReading(c, false, 0.f, start + 60, PROGRESS, true);
    Check("a step along the route counts on a poll with no reading",
          c.since == start + 60 && c.bestYards == 900.f);
}

}  // namespace

int main()
{
    TheRampIsOutOfReachOfEveryNode();
    OnlyTheRampPassesNear();
    TheLeaderJoinsTheRampNearHim();
    UpTheRampAndOnToTheDoorIsChosen();
    AJoinThatGetsNowhereIsRefused();
    AStepAlongTheSameRouteIsProgress();
    TheClockWaitsForALeaderOnHisRoute();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
