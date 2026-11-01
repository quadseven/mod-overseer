/*
 * A zone that keeps killing a party is routed round (mod-overseer#1243).
 *
 * The graph is a local frame for the shape in the ticket. The short road runs
 * 10 -> 20 -> 30 and node 20 stands in zone 47. The way round runs
 * 10 -> 40 -> 30 through zone 45 and is longer. Compiled against
 * src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

using OverseerDecisions::DangerZone;
using OverseerDecisions::DangerZonePrice;
using OverseerDecisions::DangerZonesOn;
using OverseerDecisions::ForgetOldZoneDeaths;
using OverseerDecisions::LowestPartyLevel;
using OverseerDecisions::NodeZones;
using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::PriceDangerZones;
using OverseerDecisions::ReadZoneRoute;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::ZoneAvoidedLine;
using OverseerDecisions::ZoneDangerLimits;
using OverseerDecisions::ZoneDangerScore;
using OverseerDecisions::ZoneDeath;
using OverseerDecisions::ZoneNoWayRoundLine;
using OverseerDecisions::ZoneRouteReading;

namespace
{

int failures = 0;
int checks = 0;

void Check(char const* what, bool got)
{
    ++checks;
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr std::uint32_t MAP = 0;
constexpr std::uint32_t DANGER = 47;
constexpr std::uint32_t SAFE = 45;
constexpr std::time_t NOW = 1000000;

RouteNode Node(std::uint32_t id, float x, float y)
{
    RouteNode node;
    node.id = id;
    node.mapId = MAP;
    node.x = x;
    node.y = y;
    return node;
}

void Link(std::vector<RouteLink>& links, std::uint32_t from, std::uint32_t to, float yards)
{
    for (int i = 0; i < 2; ++i)
    {
        RouteLink link;
        link.from = i ? to : from;
        link.to = i ? from : to;
        link.yards = yards;
        link.onFoot = true;
        links.push_back(link);
    }
}

std::vector<RouteNode> Nodes()
{
    return {Node(10, 0.f, 0.f), Node(20, 1000.f, 0.f), Node(30, 2000.f, 0.f),
            Node(40, 1000.f, 1500.f)};
}

std::vector<RouteLink> LinksWithWayRound()
{
    std::vector<RouteLink> links;
    Link(links, 10, 20, 1000.f);
    Link(links, 20, 30, 1000.f);
    Link(links, 10, 40, 1800.f);
    Link(links, 40, 30, 1800.f);
    return links;
}

std::vector<RouteLink> LinksWithNoWayRound()
{
    std::vector<RouteLink> links;
    Link(links, 10, 20, 1000.f);
    Link(links, 20, 30, 1000.f);
    return links;
}

NodeZones Zones()
{
    return {{10, SAFE}, {20, DANGER}, {30, SAFE}, {40, SAFE}};
}

ZoneDeath Death(std::uint32_t killerLevel, std::time_t at)
{
    ZoneDeath death;
    death.mapId = MAP;
    death.zoneId = DANGER;
    death.killerLevel = killerLevel;
    death.at = at;
    return death;
}

std::vector<ZoneDeath> ThreeDeaths(std::uint32_t killerLevel = 45)
{
    return {Death(killerLevel, NOW - 600), Death(killerLevel, NOW - 400),
            Death(killerLevel, NOW - 200)};
}

RoutePlan Plan(std::vector<RouteLink> const& links)
{
    return PlanFootRoute(Nodes(), links, MAP, 0.f, 0.f, 2000.f, 0.f, RoutePlanLimits{});
}

bool Walks(RoutePlan const& plan, std::uint32_t node)
{
    for (std::uint32_t const at : plan.nodes)
        if (at == node)
            return true;
    return false;
}

void ThreeDeathsRouteAroundTheZone()
{
    ZoneDangerLimits const limits;
    std::vector<RouteLink> links = LinksWithWayRound();
    Check("today's plan crosses the zone", Walks(Plan(links), 20));

    std::vector<DangerZone> const zones = DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits);
    Check("three deaths to creatures 8 or more levels above make the zone dangerous",
          zones.size() == 1 && zones[0].zoneId == DANGER && zones[0].deaths == 3 &&
              zones[0].strongestKiller == 45);
    Check("two legs touch the zone in each direction",
          PriceDangerZones(links, Zones(), zones, limits) == 4);
    RoutePlan const round = Plan(links);
    Check("the plan is made", round.verdict == RoutePlanVerdict::Planned);
    Check("the plan goes round the zone", !Walks(round, 20) && Walks(round, 40));
}

void FewerOrCloserOrOlderDeathsChangeNothing()
{
    ZoneDangerLimits const limits;
    std::vector<ZoneDeath> two = ThreeDeaths();
    two.pop_back();
    Check("two deaths do not make a zone dangerous",
          DangerZonesOn(two, MAP, 34, NOW, limits).empty());
    Check("killers 7 levels above do not count",
          DangerZonesOn(ThreeDeaths(41), MAP, 34, NOW, limits).empty());
    Check("killers exactly 8 levels above do count",
          DangerZonesOn(ThreeDeaths(42), MAP, 34, NOW, limits).size() == 1);

    std::time_t const later = NOW + limits.windowSeconds;
    Check("deaths past the window do not count",
          DangerZonesOn(ThreeDeaths(), MAP, 34, later, limits).empty());
    std::vector<ZoneDeath> kept = ThreeDeaths();
    kept[2].at = later;
    ForgetOldZoneDeaths(kept, later, limits);
    Check("a death past the window is forgotten", kept.size() == 1);
    Check("deaths on another map do not count",
          DangerZonesOn(ThreeDeaths(), 1, 34, NOW, limits).empty());
    Check("a party of no known level scores nothing",
          ZoneDangerScore(ThreeDeaths(), MAP, DANGER, 0, NOW, limits) == 0);

    std::vector<RouteLink> links = LinksWithWayRound();
    Check("no dangerous zone prices no leg", PriceDangerZones(links, Zones(), {}, limits) == 0);
    Check("and today's plan stands", Walks(Plan(links), 20));
}

void ARefusedLinkStaysRefused()
{
    ZoneDangerLimits const limits;
    std::vector<RouteLink> links;
    Link(links, 10, 20, -1.f);
    std::vector<DangerZone> const zones = DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits);
    Check("a refused link is not priced", PriceDangerZones(links, Zones(), zones, limits) == 0);
    Check("and stays refused", links[0].yards < 0.f);
}

void TheLogNamesTheAvoidedZone()
{
    ZoneDangerLimits const limits;
    std::vector<DangerZone> zones = DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits);
    zones[0].name = "The Hinterlands";
    std::vector<RouteLink> through = LinksWithWayRound();
    RoutePlan const wayThrough = Plan(through);
    PriceDangerZones(through, Zones(), zones, limits);
    RoutePlan const round = Plan(through);
    ZoneRouteReading const reading = ReadZoneRoute(round.nodes, wayThrough.nodes, Zones(), zones);
    Check("the zone reads as avoided", reading.avoided.size() == 1 && reading.stillWalked.empty());
    std::string const line = ZoneAvoidedLine("Ugga", "vendor", reading.avoided, limits);
    Check("the line is the exact text",
          line == "overseer: 'Ugga' is sent to 'vendor' and its route avoids zone 47 (The "
                  "Hinterlands), 3 death(s) in 24 hours to creatures up to level 45, 8 or more "
                  "levels above this party (#1243)");

    ZoneRouteReading const away = ReadZoneRoute({10, 40, 30}, {10, 40, 30}, Zones(), zones);
    Check("a zone this walk would not have crossed is not named",
          away.avoided.empty() && away.stillWalked.empty());
}

void WithNoAlternativeItWalksAndSaysSo()
{
    ZoneDangerLimits const limits;
    std::vector<DangerZone> zones = DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits);
    zones[0].name = "The Hinterlands";
    std::vector<RouteLink> links = LinksWithNoWayRound();
    PriceDangerZones(links, Zones(), zones, limits);
    RoutePlan const plan = Plan(links);
    Check("the walker still gets a route",
          plan.verdict == RoutePlanVerdict::Planned && Walks(plan, 20));
    ZoneRouteReading const reading = ReadZoneRoute(plan.nodes, plan.nodes, Zones(), zones);
    Check("the zone reads as still walked",
          reading.stillWalked.size() == 1 && reading.avoided.empty());
    std::string const line = ZoneNoWayRoundLine("Ugga", "vendor", reading.stillWalked, limits);
    Check("the line says no alternative exists and names the zone",
          line == "overseer: 'Ugga' is sent to 'vendor' and no alternative route exists round "
                  "zone 47 (The Hinterlands), 3 death(s) in 24 hours to creatures up to level "
                  "45, 8 or more levels above this party. It walks the fewest legs there it can "
                  "(#1243)");
}

void TheScoreDecaysAsThePartyLevels()
{
    ZoneDangerLimits const limits;
    std::vector<ZoneDeath> const deaths = {Death(45, NOW - 600), Death(47, NOW - 400),
                                           Death(49, NOW - 200)};
    Check("level 34 counts all three", ZoneDangerScore(deaths, MAP, DANGER, 34, NOW, limits) == 3);
    Check("level 38 counts two", ZoneDangerScore(deaths, MAP, DANGER, 38, NOW, limits) == 2);
    Check("level 40 counts one", ZoneDangerScore(deaths, MAP, DANGER, 40, NOW, limits) == 1);
    Check("level 42 counts none", ZoneDangerScore(deaths, MAP, DANGER, 42, NOW, limits) == 0);

    Check("the same deaths avoid the zone at level 34",
          DangerZonesOn(deaths, MAP, 34, NOW, limits).size() == 1);
    std::vector<DangerZone> const grown = DangerZonesOn(deaths, MAP, 38, NOW, limits);
    Check("and no longer avoid it at level 38", grown.empty());
    std::vector<RouteLink> links = LinksWithWayRound();
    PriceDangerZones(links, Zones(), grown, limits);
    Check("so the party has its short road back", Walks(Plan(links), 20));
}

void TheYoungestMemberIsWhoTheZoneIsJudgedAgainst()
{
    ZoneDangerLimits const limits;
    Check("the lowest level is used", LowestPartyLevel({60, 34, 45}) == 34);
    Check("an unknown level is passed over", LowestPartyLevel({0, 34}) == 34);
    Check("an empty party has no level", LowestPartyLevel({}) == 0);
    Check("a leg with one end in the zone is priced",
          DangerZonePrice(10, 20, Zones(), DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits),
                          limits) == limits.priceYards);
    Check("a leg with neither end in the zone is not",
          DangerZonePrice(10, 40, Zones(), DangerZonesOn(ThreeDeaths(), MAP, 34, NOW, limits),
                          limits) == 0.f);
}

void TheFormerSeedZonesComeFromDeathRows()
{
    ZoneDangerLimits const limits;
    std::vector<ZoneDeath> deaths;
    // The two original seed zones use the same row-only rule, with no seed input.
    for (std::uint32_t const zone : {46u, 51u})
        for (std::time_t const ago : {60, 120, 180})
        {
            ZoneDeath death = Death(zone == 46 ? 58 : 51, NOW - ago);
            death.zoneId = zone;
            deaths.push_back(death);
        }
    std::vector<DangerZone> const zones = DangerZonesOn(deaths, MAP, 17, NOW, limits);
    Check("death rows flag both former seed zones without configuration", zones.size() == 2);
    for (std::uint32_t const zone : {46u, 51u})
    {
        Check("each former seed zone has three qualifying deaths",
              ZoneDangerScore(deaths, MAP, zone, 17, NOW, limits) == 3);
        NodeZones nodes = Zones();
        nodes[20] = zone;
        std::vector<RouteLink> links = LinksWithWayRound();
        Check("each former seed zone prices its route without a seed",
              PriceDangerZones(links, nodes, zones, limits) == 4);
        Check("each former seed zone is avoided using only the death rows",
              !Walks(Plan(links), 20) && Walks(Plan(links), 40));
    }
    Check("a zone absent from the rows has no score",
          ZoneDangerScore(deaths, MAP, 999, 17, NOW, limits) == 0);
    Check("empty rows do not silently restore the former seed zones",
          DangerZonesOn({}, MAP, 17, NOW, limits).empty());
}

}  // namespace

int main()
{
    ThreeDeathsRouteAroundTheZone();
    FewerOrCloserOrOlderDeathsChangeNothing();
    ARefusedLinkStaysRefused();
    TheLogNamesTheAvoidedZone();
    WithNoAlternativeItWalksAndSaysSo();
    TheScoreDecaysAsThePartyLevels();
    TheYoungestMemberIsWhoTheZoneIsJudgedAgainst();
    TheFormerSeedZonesComeFromDeathRows();
    std::printf("zone danger routing: %d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
