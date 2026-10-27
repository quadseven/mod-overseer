/*
 * Source contract for where a guard walks its character (#163).
 *
 * ReadGuardWalk is proved in test_travel_guard_walk.cpp. What only the module
 * can get wrong, and what this file pins:
 *
 *  - MaySteerItself answers true for a character whose guard resolved to a
 *    place (WalksItsGuard), so a guarded follower is granted `new rpg` by the
 *    travel drive and is not stripped of it by the follower backstop. Measured
 *    2026-10-26: four guarded followers were each refused as "nothing walks it
 *    anywhere".
 *  - WalksItsGuard asks GuardWalkSteers of the verdict kept on the guard entry.
 *  - The travel drive walks a guard through GuardTarget, which asks
 *    ReadGuardWalk, rather than translating the column with TravelGuardPlace
 *    alone (which handed a map 1 place to a character on map 0).
 *  - GuardTarget says an other-map refusal with LOG_WARN.
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
    std::cerr << "FAIL " << what << "\n";
    return EXIT_FAILURE;
}

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const steer = Body(text, "    bool MaySteerItself(std::string const& name) const");
    if (steer.empty())
        return Fail("cannot find MaySteerItself");
    if (steer.find("WalksItsGuard(name)") == std::string::npos)
        return Fail("MaySteerItself must answer true for a character its guard walks");

    std::string const walks = Body(text, "    bool WalksItsGuard(std::string const& name) const");
    if (walks.empty())
        return Fail("cannot find WalksItsGuard");
    if (walks.find("_travelGuards.find(name)") == std::string::npos ||
        walks.find("OverseerDecisions::GuardWalkSteers(") == std::string::npos)
        return Fail("WalksItsGuard must ask GuardWalkSteers of the guard entry");

    std::string const target = Body(text, "    std::string GuardTarget(std::string const& name,");
    if (target.empty())
        return Fail("cannot find GuardTarget");
    if (target.find("OverseerDecisions::ReadGuardWalk(facts)") == std::string::npos)
        return Fail("GuardTarget must resolve the guard with ReadGuardWalk");
    if (target.find("facts.currentMap = bot->GetMapId()") == std::string::npos)
        return Fail("GuardTarget must read the map the character stands on");
    if (target.find("facts.leaderColumn") == std::string::npos)
        return Fail("GuardTarget must hand ReadGuardWalk the leader's column");
    if (target.find("seen->second.walk = verdict.walk") == std::string::npos)
        return Fail("GuardTarget must keep the verdict for WalksItsGuard");
    if (target.find("LOG_WARN") == std::string::npos)
        return Fail("GuardTarget must say an other-map refusal loudly");

    std::string const drive = Body(text, "    void DriveTravel()");
    if (drive.empty())
        return Fail("cannot find DriveTravel");
    if (drive.find("guarded ? GuardTarget(name, columnAim, aims)") == std::string::npos)
        return Fail("DriveTravel must walk a guard through GuardTarget");
    if (drive.find("guarded ? OverseerDecisions::TravelGuardPlace(columnAim)") !=
        std::string::npos)
        return Fail("DriveTravel must not hand a guard's raw place to the drive");

    std::cout << "travel guard walk source: all checks passed\n";
    return EXIT_SUCCESS;
}
