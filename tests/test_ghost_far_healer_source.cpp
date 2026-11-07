/*
 * Source contract for wow-overseer#1093: the guild reroute asks for a graveyard
 * beyond the corpse's zone when the zone's own candidates are all refused, and
 * that search filters by OverseerDecisions::FarHealerConsidered before it runs
 * the safety test.
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

std::string Body(std::string const& text, std::string const& signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return {};
    std::size_t const open = text.find('{', start);
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i)
    {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}' && --depth == 0)
            return text.substr(start, i - start + 1);
    }
    return {};
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const search = Body(text, "static GraveyardStruct const* SafeGraveyardOnMap(");
    if (search.empty())
        return Fail("cannot find SafeGraveyardOnMap");
    std::size_t const considered = search.find("OverseerDecisions::FarHealerConsidered(");
    std::size_t const judged = search.find("GraveyardRefusal(");
    if (considered == std::string::npos || judged == std::string::npos || considered > judged)
        return Fail("SafeGraveyardOnMap must filter by FarHealerConsidered before GraveyardRefusal");
    if (search.find("GRAVEYARD_FAR_CANDIDATES") == std::string::npos)
        return Fail("SafeGraveyardOnMap must bound how many graveyards it judges");

    std::string const reroute = Body(text, "void RerouteHealerGraveyard(");
    if (reroute.empty())
        return Fail("cannot find RerouteHealerGraveyard");
    std::size_t const near = reroute.find("SafeGraveyardNear(");
    std::size_t const far = reroute.find("SafeGraveyardOnMap(");
    std::size_t const pick = reroute.find("PickGuildHealerGraveyard(");
    if (near == std::string::npos || far == std::string::npos || pick == std::string::npos ||
        !(near < far && far < pick))
        return Fail("RerouteHealerGraveyard must try SafeGraveyardOnMap after SafeGraveyardNear "
                    "finds none, before it picks");
    if (reroute.find("if (!safe)") == std::string::npos)
        return Fail("the far search runs only when the corpse zone offered nothing");

    std::cout << "the guild reroute walks a ghost beyond its corpse zone when that zone has no "
                 "graveyard it can stand at\n";
    return EXIT_SUCCESS;
}
