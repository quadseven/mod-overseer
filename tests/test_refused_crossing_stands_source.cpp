/*
 * Source contract for the refused crossing's bound (#1143).
 *
 * The rule is proved in test_refused_crossing_stands.cpp. What only the
 * adapter can get wrong is whether it asks it: the party poll must read a
 * standing refusal through RefusedCrossingStands with the leader's map, and a
 * refusal must be recorded together with the map it was made for. A rule
 * nothing feeds is the failure this repository has shipped before.
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

    for (char const* needed :
         {"OverseerDecisions::RefusedCrossingStands(", "_loneCrossingRefusedFor[name] = destination",
          "_loneCrossingRefusedFor[name],\n                facts.leaderMap)"})
        if (text.find(needed) == std::string::npos)
        {
            std::cerr << "the refused crossing's bound must be wired: " << needed << "\n";
            return EXIT_FAILURE;
        }
    if (text.find("std::time(nullptr) - refused->second.first < "
                  "time_t(LONE_CROSSING_STANDDOWN_SECONDS)") != std::string::npos)
    {
        std::cerr << "the 15 minute stand-down must not come back beside the rule\n";
        return EXIT_FAILURE;
    }
    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
