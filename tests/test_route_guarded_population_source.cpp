/*
 * Source contract for the route's guarded-ground population (mod-overseer#502).
 *
 * The rules are proved in test_route_guarded_population.cpp. What only the
 * adapter can get wrong is whether MeasureGuardedLegs ASKS them: before this
 * change the route sweep was HostileSpawnsNearEach with theOtherSideOnly and a
 * level + 10 - 1 floor, and the pricing gather filtered on
 * IsTheOtherSidesGround, so a neutral elite was never marked or priced, and
 * nothing counted grey density for a lone walker.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const measure = Between(text, "static bool MeasureGuardedLegs(",
                                        "A MEASURED STAGING CORRIDOR IS WALKED");
    if (measure.empty())
        return Fail("cannot find MeasureGuardedLegs");

    if (measure.find("HostileSpawnsNearEach(") != std::string::npos)
        return Fail("the route must be marked by GuardedGroundNearEach, not the other-side-only "
                    "HostileSpawnsNearEach sweep");
    if (measure.find("GuardedGroundNearEach(bot, mapId, points, GRAVEYARD_THREAT_RADIUS, "
                     "groundRule") == std::string::npos)
        return Fail("MeasureGuardedLegs must mark legs with GuardedGroundNearEach and its rule");
    if (measure.find("\"Overseer.Route.NeutralElites\", true") == std::string::npos)
        return Fail("Overseer.Route.NeutralElites must be read, on by default");
    if (measure.find("\"Overseer.Route.LoneGreyDensity\", 0") == std::string::npos)
        return Fail("Overseer.Route.LoneGreyDensity must be read, off (0) by default");
    if (measure.find("bot->GetGroup() != nullptr") == std::string::npos)
        return Fail("the lone walker rule must know whether the walker has a group");
    if (measure.find("densityRule.threshold > 0 && !walkerHasGroup") == std::string::npos)
        return Fail("the density sweep must cost nothing when A is off or the walker is grouped");
    if (measure.find("OverseerDecisions::ScoreLoneGreyDensity(") == std::string::npos)
        return Fail("MeasureGuardedLegs must ask ScoreLoneGreyDensity");
    if (measure.find("std::max(danger.detourYards, density[i].detourYards)") ==
        std::string::npos)
        return Fail("a leg's price must be the larger of the two rules' prices");
    if (measure.find("Overseer.Route.LoneGreyDensity asks for") == std::string::npos)
        return Fail("the lone walker log line must name the count and the rule");

    std::string const sweep = Between(text, "static void GuardedGroundNearEach(",
                                      "EVERY SPAWN THAT COULD PULL ON THIS ROUTE");
    if (sweep.empty())
        return Fail("cannot find GuardedGroundNearEach");
    if (sweep.find("OverseerDecisions::SpawnMarksGuardedGround(candidate, walkerLevel, rule)") ==
        std::string::npos)
        return Fail("GuardedGroundNearEach must ask SpawnMarksGuardedGround");
    if (sweep.find("candidate.rank = tmpl->rank;") == std::string::npos)
        return Fail("GuardedGroundNearEach must pass the creature's rank");
    if (sweep.find("CreatureSpawnStandsNow(") == std::string::npos ||
        sweep.find("CanBeFought(") == std::string::npos)
        return Fail("GuardedGroundNearEach must keep the standing and fightable checks");

    std::string const gather = Between(text, "static void DangerSpawnsNear(",
                                       "WHAT THIS ROUTE'S LEGS CROSS");
    if (gather.empty())
        return Fail("cannot find DangerSpawnsNear");
    if (gather.find("OverseerDecisions::SpawnPricedOnGuardedGround(candidate, rule)") ==
        std::string::npos)
        return Fail("DangerSpawnsNear must price the same population the mark used");
    if (gather.find("if (!IsTheOtherSidesGround(*theirs, *mine))") != std::string::npos)
        return Fail("DangerSpawnsNear must not filter to the other side alone any more");

    std::cout << "route guarded population source: all checks passed\n";
    return EXIT_SUCCESS;
}
