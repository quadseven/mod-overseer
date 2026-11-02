#include "overseer_decisions.h"

#include <fstream>
#include <iostream>
#include <iterator>

int main()
{
    using namespace OverseerDecisions;
    ZoneDangerLimits const limits;
    std::vector<ZoneDeath> deaths;
    for (int i = 0; i < 3; ++i)
        deaths.push_back({0, 46, 58, 1000 - i});
    auto zones = DangerZonesOn(deaths, 0, 17, 1000, limits);
    zones.at(0).name = "A \"quoted\" zone\n";
    std::string const json = ZoneDangerProbeJson(zones, 0, 17, 1000, limits);
    auto has = [&](char const* needle) { return json.find(needle) != std::string::npos; };
    if (!has("\"map_id\":0") || !has("\"party_level\":17") ||
        !has("\"sampled_at\":1000") || !has("\"window_seconds\":86400") ||
        !has("\"level_gap\":8") || !has("\"threshold\":3") ||
        !has("\"zone_id\":46") || !has("\"score\":3") ||
        !has("\"deaths_counted\":3") || !has("\"strongest_killer\":58") ||
        !has("A \\\"quoted\\\" zone\\u000a"))
        return 1;
    if (ZoneDangerProbeJson({}, 0, 17, 1000, limits).find("\"zones\":[]") == std::string::npos)
        return 2;
    std::ifstream file("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(file)), {});
    auto const begin = source.find("static std::string ZoneDangerFor(Player* bot, std::time_t now)");
    auto const end = source.find("static void ReadNodeZones(", begin);
    auto const probe = source.substr(begin, end - begin);
    if (probe.find("DangerZonesFor(bot, now)") == std::string::npos ||
        probe.find("ZoneDangerProbeJson(") == std::string::npos ||
        probe.find("LowestPartyLevel(PartyLevelsFor(bot))") == std::string::npos ||
        probe.find("ZONE_DANGER_LIMITS") == std::string::npos)
        return 3;
    auto const routeBegin = source.find("static std::vector<OverseerDecisions::RoutePoint> PlanRoute(");
    auto const routeEnd = source.find("static OverseerDecisions::RouteLegLimits RouteLimitsFor(", routeBegin);
    if (source.substr(routeBegin, routeEnd - routeBegin).find("DangerZonesFor(bot, plannedAt)") == std::string::npos)
        return 4;
    auto const snapshot = source.substr(source.find("void WriteSnapshot()"));
    if (snapshot.find("REPLACE INTO overseer_zone_danger (name, result)") == std::string::npos ||
        snapshot.find("EscLong(ZoneDangerFor(p, std::time(nullptr)))") == std::string::npos ||
        snapshot.find("DELETE FROM overseer_zone_danger WHERE updated_at < NOW() - INTERVAL 60 SECOND") == std::string::npos)
        return 5;
    std::cout << "zone danger projection: canonical route result, context, escaping and empty state passed\n";
}
