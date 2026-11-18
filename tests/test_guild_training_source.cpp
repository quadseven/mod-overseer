/*
 * Source contract for the natural guild member's trainer pick.
 *
 * The rule is proved in test_guild_training.cpp. What only the adapter can get
 * wrong is whether it asks it: the pick filtered a trainer by faction, and a
 * seasonal trainer of faction 35 is friendly to everyone, so a level 37 Horde
 * warlock was sent to an Arena Tournament Warlock Trainer standing inside
 * Ironforge's guards, out of season, and died there 14 times.
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

    std::size_t const filter =
        text.find("The trainers of its class on its map that would teach it");
    if (filter == std::string::npos)
        return Fail("cannot find the guild trainer filter");
    std::size_t const pick = text.find("OverseerDecisions::NearestTrainerSpot(", filter);
    if (pick == std::string::npos || pick - filter > 6000)
        return Fail("cannot find the guild trainer pick after its filter");
    std::string const block = text.substr(filter, pick - filter);

    if (block.find("OverseerDecisions::GuildTrainerSpotUsable(") == std::string::npos)
        return Fail("the guild trainer filter must ask GuildTrainerSpotUsable");
    if (block.find("CreatureSpawnStandsNow(") == std::string::npos)
        return Fail("the guild trainer filter must read the spawn's game event");
    if (block.find("GuildTrainerGuardsNear(") == std::string::npos)
        return Fail("the guild trainer filter must read the guards beside the trainer");

    if (text.find("_classTrainerSpawnIds[cls].push_back(spawn.first);") == std::string::npos)
        return Fail("the trainer index must keep each spawn id, to read its game event");

    std::cout << "guild training source: all checks passed\n";
    return EXIT_SUCCESS;
}
