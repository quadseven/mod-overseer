/* Source contract for the off-mesh leader hold (mod-overseer#1698): the travel
 * drive asks StallRecoveryHoldsErrand before it asks for the leader, and the
 * recovery reading asks for the quick rule. */

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
    auto const hold = text.find("OverseerDecisions::StallRecoveryHoldsErrand(");
    auto const ask = text.find("if (!AskForLeader(name, kind, owner, about,");
    if (hold == std::string::npos || ask == std::string::npos || hold > ask)
        return std::cerr << "the hold is not asked before the leader claim\n", EXIT_FAILURE;
    if (text.find("facts.stillRule = true;") == std::string::npos)
        return std::cerr << "the recovery reading does not ask for the quick rule\n", EXIT_FAILURE;
    return EXIT_SUCCESS;
}
