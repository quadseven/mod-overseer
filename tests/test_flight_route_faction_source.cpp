/*
 * Source contract for the team-aware taxi route (#501).
 *
 * The decisions are proved in test_flight_route_faction.cpp. What only the
 * adapter can get wrong is whether it ASKS them: the flight code must read its
 * routes from the team graph rather than upstream's faction-blind
 * sTravelNodeMap.FindTaxiPath, and must let ScanTaxiRoute decide which hop, if
 * any, is named. A decision nothing calls is the failure this repository has
 * shipped before, so the wiring is pinned as text.
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

std::size_t Count(std::string const& text, std::string const& needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (Count(text, "sTravelNodeMap.FindTaxiPath(") != 0)
        return Fail("no flight route may come from upstream's faction-blind FindTaxiPath");

    std::size_t const helper = text.find("static std::vector<uint32> TeamTaxiPath(");
    if (helper == std::string::npos ||
        text.find("OverseerDecisions::FindTeamTaxiPath(", helper) == std::string::npos)
        return Fail("TeamTaxiPath must search the decisions' team graph");
    if (text.find("OverseerDecisions::TaxiNodeServesTeam(") == std::string::npos ||
        text.find("sTaxiNodesMask[field]") == std::string::npos)
        return Fail("the team test must read the mount pair and the taxi network mask");

    std::size_t const seat = text.find("void ReadPartyFlightSeat(");
    std::size_t const consider = text.find("bool ConsiderFlight(");
    if (seat == std::string::npos || consider == std::string::npos)
        return Fail("cannot find the two flight readers");

    std::size_t const seatPath = text.find("TeamTaxiPath(fromNode, toNode, member->GetTeamId(true))", seat);
    std::size_t const seatScan = text.find("ScanTaxiRouteFor(member, seat.path)", seat);
    if (seatPath == std::string::npos || seatScan == std::string::npos || seatPath > consider ||
        seatScan > consider)
        return Fail("the party seat must read its route and its hops through the team scan");
    if (text.find("seat.read.undiscoveredNode = scan.learnNode;", seat) == std::string::npos)
        return Fail("the party seat may name only the node the scan names");

    std::size_t const path = text.find("TeamTaxiPath(fromNode, toNode, team)", consider);
    std::size_t const scan = text.find("ScanTaxiRouteFor(bot, path)", consider);
    std::size_t const said = text.find("OverseerDecisions::FlightMasterNodeAim(unknownHop)", consider);
    if (path == std::string::npos || scan == std::string::npos || said == std::string::npos ||
        !(path < scan && scan < said))
        return Fail("ConsiderFlight must route by team and scan before it names a node");
    if (text.find("uint32 const unknownHop = scan.learnNode;", consider) == std::string::npos)
        return Fail("the node ConsiderFlight names must be the scan's learnNode");

    std::cout << "test_flight_route_faction_source: wiring holds\n";
    return EXIT_SUCCESS;
}
