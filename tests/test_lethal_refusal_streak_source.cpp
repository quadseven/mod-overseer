/* Source contract (mod-overseer#1706): both lethal-refusal releases in the
 * travel drive feed the per-character stall state that the no-progress release
 * reads, through LethalRefusalStep. */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return std::cerr << "cannot read src/mod_overseer.cpp\n", EXIT_FAILURE;
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    auto const step = text.find("OverseerDecisions::LethalRefusalStep(_stallRecovery[");
    if (step == std::string::npos)
        return std::cerr << "the refusal does not feed _stallRecovery\n", EXIT_FAILURE;
    for (char const* marker : {"zone flagged for the party's deaths)\");",
                               "above the family's level)\");"})
    {
        auto const at = text.find(marker);
        if (at == std::string::npos)
            return std::cerr << "release site missing\n", EXIT_FAILURE;
        auto const feed = text.find("ReleaseLethalRefusalStreak(bot, name);", at);
        if (feed == std::string::npos || feed - at > 80)
            return std::cerr << "a lethal release does not feed the streak\n", EXIT_FAILURE;
    }
    if (text.find("aims refused as lethal in a row") == std::string::npos)
        return std::cerr << "the line does not name the refusal count\n", EXIT_FAILURE;
    return EXIT_SUCCESS;
}
