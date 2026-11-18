/* Source contract for the flight leg's stray check (see test_flight_leg_strayed.cpp).
 *
 * The rule is tested there; this pins that the adapter records the distance
 * at issue, asks the rule before it holds the leg open, and gives the status
 * back to idle when the leg has strayed.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (text.find("float flightMasterYards{-1.f};") == std::string::npos)
        return Fail("the travel state must carry the distance to the flight master at issue");

    std::size_t const flies = text.find("and flies instead - ");
    if (flies == std::string::npos)
        return Fail("cannot find where the leader's flight leg is issued");
    std::size_t const stamped = text.rfind("state.flightSince = std::time(nullptr);", flies);
    std::size_t const recorded = text.rfind("state.flightMasterYards = toMaster;", flies);
    if (stamped == std::string::npos || recorded == std::string::npos || recorded < stamped ||
        flies - stamped > 400)
        return Fail("the leader's flight leg must record toMaster right after flightSince");

    std::size_t const leg = text.find("if (state.flightSince)\n            {\n                if (botAI->rpgInfo.GetStatus() == RPG_TRAVEL_FLIGHT)");
    if (leg == std::string::npos)
        return Fail("cannot find the flight leg's hold");
    std::size_t const asks = text.find("OverseerDecisions::FlightLegStrayed(", leg);
    std::size_t const holds = text.find("TRAVEL_FLIGHT_BACKSTOP_SECONDS)\n                        continue;", leg);
    if (asks == std::string::npos || holds == std::string::npos || asks > holds)
        return Fail("the stray check must come before the leg is held open");
    std::string const between = text.substr(asks, holds - asks);
    if (between.find("rpgInfo.ChangeToIdle()") == std::string::npos ||
        between.find("flightSince = 0") == std::string::npos)
        return Fail("a strayed leg must go back to idle and clear the leg clock");

    std::cout << "flight leg strayed source: ok\n";
    return EXIT_SUCCESS;
}
