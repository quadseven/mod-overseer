/* Source contract for the errand leader's unissued-flight guard (#1542).
 *
 * The rule is tested in test_lone_leg.cpp; this pins the adapter wiring: the
 * town guard reads the flight leg's own marker (flightSince), not the errand,
 * and an errand holder is put through ErrandLeaderUnissuedFlight.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const guard = text.find("void KeepTownLeadersGrounded()");
    if (guard == std::string::npos)
    {
        std::cerr << "cannot find KeepTownLeadersGrounded\n";
        return EXIT_FAILURE;
    }
    std::size_t const end = text.find("_townLeaderAirborneSaid.swap(airborne);", guard);
    std::string const body = text.substr(guard, end - guard);

    if (body.find("HoldsIssuedFlight(name)") == std::string::npos)
    {
        std::cerr << "the guard must read the flight leg's marker (flightSince)\n";
        return EXIT_FAILURE;
    }
    if (body.find("mayCarry, statusIsFlight, issued,") == std::string::npos ||
        body.find("statusIsFlight, onErrand") != std::string::npos)
    {
        std::cerr << "holding an errand must not stand in for a module-issued flight\n";
        return EXIT_FAILURE;
    }
    if (body.find("ErrandLeaderUnissuedFlight(") == std::string::npos ||
        body.find("TRAVEL_FLIGHT_MIN_YARDS") == std::string::npos)
    {
        std::cerr << "an errand holder must be judged by ErrandLeaderUnissuedFlight\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
