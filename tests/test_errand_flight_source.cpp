/* Source contract for unissued flights on travel-column errands (#1542).
 * test_lone_leg.cpp covers the resulting decisions.
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

    std::size_t const reader = text.find("bool HoldsIssuedFlight(");
    std::size_t const marker = text.find("it->second.flightSince != 0", reader);
    if (reader == std::string::npos || marker == std::string::npos ||
        marker >= text.find('}', reader))
    {
        std::cerr << "the issued-flight reader must check TravelState::flightSince\n";
        return EXIT_FAILURE;
    }

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
    if (body.find("leader, ai, issued, &goal)") == std::string::npos ||
        body.find("TRAVEL_FLIGHT_MIN_YARDS") != std::string::npos)
    {
        std::cerr << "the town errand guard must share the destination check\n";
        return EXIT_FAILURE;
    }

    std::size_t const drive = text.find("void DriveTravel()");
    std::size_t const air = text.find("if (bot->IsInFlight())", drive);
    if (drive == std::string::npos || air == std::string::npos)
    {
        std::cerr << "cannot find DriveTravel's airborne early return\n";
        return EXIT_FAILURE;
    }
    std::string const beforeAir = text.substr(drive, air - drive);
    std::size_t const issued = beforeAir.find("issued = _travelAims.HoldsIssuedFlight(name)");
    std::size_t const check = beforeAir.find("CheckErrandFlight(", issued);
    if (issued == std::string::npos || check == std::string::npos ||
        beforeAir.find("issued,", check) == std::string::npos)
    {
        std::cerr << "DriveTravel must check flights with the issued-leg marker before "
                     "the airborne early return\n";
        return EXIT_FAILURE;
    }

    std::size_t const helper = text.find("void CheckErrandFlight(");
    if (helper == std::string::npos || helper >= guard)
    {
        std::cerr << "cannot find CheckErrandFlight\n";
        return EXIT_FAILURE;
    }
    std::string const adapter = text.substr(helper, guard - helper);
    if (adapter.find("flight->path.back()") == std::string::npos ||
        adapter.find("sTaxiNodesStore.LookupEntry(destinationNode)") == std::string::npos ||
        adapter.find("ErrandLeaderUnissuedFlight(") == std::string::npos ||
        adapter.find("issued, inFlight, goalYards, landingYards") == std::string::npos ||
        adapter.find("step == OverseerDecisions::UnissuedFlightStep::Cancel") == std::string::npos ||
        adapter.find("ChangeToIdle()") == std::string::npos ||
        adapter.find("_errandFlightAirborneSaid.insert(name).second") == std::string::npos ||
        adapter.find("starter {},") == std::string::npos ||
        adapter.find("destination node {}, {} yards from goal") == std::string::npos)
    {
        std::cerr << "the flight guard must judge the destination, idle the ground "
                     "cancel, and log the airborne facts once\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
