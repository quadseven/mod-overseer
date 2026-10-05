/*
 * Source contract for the set-down wiring in the terrain drive.
 *
 * The decision is proved in test_off_a_deck.cpp. What only the adapter can get
 * wrong is whether it ASKS: measures a transport within reach, passes the
 * lethal-drop bound into the limits, and acts on SetDown on the character's own
 * map at its own x and y. A rung nothing feeds is the failure this repository
 * has shipped before, so the wiring is pinned as text.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
bool Has(std::string const& text, char const* needle, std::size_t from = 0)
{
    return text.find(needle, from) != std::string::npos;
}

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

    std::size_t const limits = text.find("TERRAIN_RECOVERY_LIMITS{");
    if (limits == std::string::npos ||
        !Has(text, "TERRAIN_SET_DOWN_DROP_YARDS};", limits))
        return Fail("the lethal-drop bound must be the last field of the live limits");

    std::size_t const floor = text.find("reading.floorBelowValid =");
    if (floor == std::string::npos)
        return Fail("the drive must measure the floor under the feet");
    std::size_t const beside = text.find("reading.besideTransport = true;", floor);
    if (beside == std::string::npos ||
        !Has(text, "GetAllTransports()", floor) ||
        !Has(text, "TERRAIN_TRANSPORT_REACH_YARDS", floor))
        return Fail("the drive must look for a transport within the core's reach");
    std::size_t const step = text.find("OverseerDecisions::TerrainRecoveryStep(", floor);
    if (step == std::string::npos || !(beside < step))
        return Fail("the transport is measured before the step reads the reading");

    std::size_t const act =
        text.find("verdict.remedy == OverseerDecisions::TerrainRemedy::SetDown");
    if (act == std::string::npos)
        return Fail("the drive must act on SetDown");
    std::size_t const tele = text.find("bot->TeleportTo(bot->GetMapId(), fromX, fromY, downZ", act);
    std::size_t const lift =
        text.find("verdict.remedy == OverseerDecisions::TerrainRemedy::LiftToSurface", act);
    if (tele == std::string::npos || lift == std::string::npos || !(tele < lift))
        return Fail("a set-down stays on the character's own map at its own x and y");
    if (!Has(text, "WAS IN THE AIR beside", act))
        return Fail("a set-down must be logged loudly, by name");
    std::cout << "the set-down is measured, bounded, applied and logged\n";
    return EXIT_SUCCESS;
}
