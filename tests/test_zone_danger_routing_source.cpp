/*
 * Source contract for zone danger routing (mod-overseer#1243).
 *
 * The rule is proved in test_zone_danger_routing.cpp. What only the adapter can
 * get wrong is whether it is asked: that the score is read from overseer_death,
 * that PlanRoute prices the walker's links, that the road join pays the same
 * price, that both returns say what the route did, and that a creature walk is
 * steered round a zone.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find(to, start + 1);
    if (end == std::string::npos)
        return std::string();
    return text.substr(start, end - start);
}

std::size_t Count(std::string const& text, std::string const& needle)
{
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size()))
        ++count;
    return count;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const read = Between(text, "static void ReadZoneDeaths(std::time_t now)",
                                     "static void ReadNodeZones(");
    if (read.empty())
        return Fail("cannot find ReadZoneDeaths");
    if (read.find("FROM overseer_death WHERE id > {}") == std::string::npos)
        return Fail("the deaths must be read from overseer_death past the last id");
    if (read.find("killer_type = 'creature'") == std::string::npos)
        return Fail("only a creature's kills count against a zone");
    if (read.find("killer->maxlevel") == std::string::npos)
        return Fail("the killer's level must come from its template");
    if (read.find("mine->IsFriendlyTo(*theirs)") == std::string::npos)
        return Fail("a kill by something friendly to the walker must not count");
    if (read.find("OverseerDecisions::LowestPartyLevel(PartyLevelsFor(bot))") ==
        std::string::npos)
        return Fail("a zone must be judged against the party's lowest level");

    std::string const plan = Between(text, "static std::vector<OverseerDecisions::RoutePoint> PlanRoute(",
                                     "static OverseerDecisions::RouteLegLimits RouteLimitsFor(");
    if (plan.empty())
        return Fail("cannot find PlanRoute");
    if (plan.find("DangerZonesFor(bot, plannedAt);") == std::string::npos)
        return Fail("PlanRoute must ask which zones are dangerous to this walker");
    if (plan.find("OverseerDecisions::PriceDangerZones(pricedLinks, survey.nodeZones, dangerZones,") ==
        std::string::npos)
        return Fail("PlanRoute must price the walker's copy of the links");
    if (plan.find("plannedAt, dangerZones, byId, aimX, aimY, joinLimits);") == std::string::npos)
        return Fail("PlanRoute must hand the road join the dangerous zones");
    if (Count(plan, "SayDangerZones(bot, survey, name, target, diedOn, dangerZones,") != 2)
        return Fail("every PlanRoute return with a surveyed leg must say what it did");
    if (plan.find("OverseerDecisions::ZoneNoWayRoundLine(") == std::string::npos ||
        plan.find("OverseerDecisions::ZoneAvoidedLine(") == std::string::npos)
        return Fail("the route must log the avoided zone and a missing way round");

    std::string const join = Between(text, "static SurveyedRoadJoin JoinSurveyedRoad(",
                                     "THE WHOLE ROUTE FOR ONE ERRAND");
    if (join.find("OverseerDecisions::DangerZonePrice(link.from, link.to, survey.nodeZones,") ==
        std::string::npos)
        return Fail("the road join must pay the zone's price");

    std::string const drive = Between(text, "void DriveTravel()", "EndTravelPoll(stillAimed);");
    if (drive.find("ZonesCreatureWalkGoesRound(bot, pos, state, name, target)") ==
            std::string::npos ||
        drive.find("DiedOnLegsHeldBy(bot->GetName(), std::time(nullptr)) + zonesRound") ==
            std::string::npos)
        return Fail("a creature walk must be steered round a dangerous zone");

    std::cout << "zone danger routing source: all checks passed\n";
    return EXIT_SUCCESS;
}
