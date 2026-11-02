/*
 * Source contract for faction-aware dungeon completion masks.
 *
 * The pure decision test proves which encounter bit is faction-specific. This
 * check protects the adapter call site that applies that rule to each run.
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

std::string FunctionBody(std::string const& source, std::string const& signature)
{
    std::size_t const start = source.find(signature);
    if (start == std::string::npos)
        return {};

    std::size_t const open = source.find('{', start);
    if (open == std::string::npos)
        return {};

    std::size_t depth = 0;
    for (std::size_t i = open; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}' && --depth == 0)
            return source.substr(open + 1, i - open - 1);
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
    std::string const body = FunctionBody(
        text, "static uint32 ExpectedEncounterMask(uint32 mapId, Player* member)");
    if (body.empty())
        return Fail("cannot find ExpectedEncounterMask body");

    std::size_t const expected = body.find("DungeonRunExpectedMask(mapId, mapMask, wingMask)");
    std::size_t const faction = body.find("DungeonRunFactionExpectedMask(");
    if (expected == std::string::npos || faction == std::string::npos || faction < expected)
        return Fail("expected map mask must pass through the faction-aware mask helper");

    std::string const factionCall = body.substr(faction);
    if (factionCall.find("mapId, expectedMask") == std::string::npos ||
        factionCall.find("member->GetTeamId() == TEAM_ALLIANCE") == std::string::npos)
        return Fail("faction helper must receive the map, expected mask, and member faction");

    std::cout << "dungeon faction expected mask source: all checks passed\n";
    return EXIT_SUCCESS;
}
