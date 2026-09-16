/*
 * Source contract for the below-terrain recovery ordering.
 *
 * Recovery must record the last aim before releasing it. Otherwise the
 * cleanup fixes the travel loop but erases the evidence needed to identify
 * the destination that caused it.
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
    std::size_t const snapshot = text.find("travelTarget = it->second.travelTarget;");
    std::size_t const release = text.find("_travelAims.Release(name);", snapshot);
    std::size_t const teleport = text.find("bot->TeleportTo(bot->GetMapId()", release);
    if (snapshot == std::string::npos || release == std::string::npos ||
        teleport == std::string::npos || !(snapshot < release && release < teleport))
    {
        std::cerr << "recovery must snapshot the aim, release it, then teleport\n";
        return EXIT_FAILURE;
    }
    std::cout << "terrain recovery preserves the last aim before release\n";
    return EXIT_SUCCESS;
}
