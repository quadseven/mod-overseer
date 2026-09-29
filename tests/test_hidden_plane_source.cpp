/*
 * Source contract for the hidden-plane wiring in the terrain drive.
 *
 * The decisions are proved in test_hidden_plane.cpp. What only the adapter can
 * get wrong is whether it ASKS: measures the raw terrain, passes the plane into
 * the errand release and the stand-down set, and returns the character on the
 * same map. A rung nothing feeds is the failure this repository has shipped
 * before, so the wiring is pinned as text.
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

    std::size_t const measure = text.find("Map* const terrainMap = bot->GetMap()");
    if (measure == std::string::npos ||
        !Has(text, "terrainMap->GetGridHeight(reading.x, reading.y)", measure))
        return Fail("the drive must read the raw terrain height under the character");
    if (!Has(text, "!terrainMap->Instanceable()", measure))
        return Fail("the plane is measured on an open map only");
    if (!Has(text, "OverseerDecisions::PlaneRingIsFlat(", measure) ||
        !Has(text, "OverseerDecisions::OnTheHiddenPlane(", measure))
        return Fail("the reading must be folded through PlaneRingIsFlat and OnTheHiddenPlane");

    std::size_t const back = text.find(
        "verdict.remedy ==\n                OverseerDecisions::TerrainRemedy::ReturnToLastGround");
    if (back == std::string::npos)
        return Fail("the drive must act on ReturnToLastGround");
    if (!Has(text, "verdict.groundMapId != bot->GetMapId()", back))
        return Fail("a return must be refused unless it is on the character's own map");
    std::size_t const refuse = text.find("_travelAims.Refuse(name, travelTarget", back);
    std::size_t const tele = text.find("bot->TeleportTo(bot->GetMapId(), verdict.groundX", back);
    if (refuse == std::string::npos || tele == std::string::npos || !(refuse < tele))
        return Fail("the errand that walked it there is refused before the return");
    if (!Has(text, "WAS UNDER THE WORLD at map", back))
        return Fail("a return must be logged loudly, by name");
    if (!Has(text, "IS STILL UNDER THE WORLD at map"))
        return Fail("the plane's give-up must be its own loud line, not 'the detector is wrong'");
    if (!Has(text, "(nearThePlane || onThePlane)"))
        return Fail("a plane lift is not held to the 30-yard cap of an ordinary lift");
    std::cout << "hidden plane is measured, returned, refused and logged\n";
    return EXIT_SUCCESS;
}
