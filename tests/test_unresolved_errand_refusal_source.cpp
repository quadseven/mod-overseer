/*
 * Source contract for the unresolved errand's bound (#1711). The rule is
 * proved in test_unresolved_errand_refusal.cpp; what only the adapter can get
 * wrong is whether it asks it: the release for a spawn that is not on the
 * leader's map must record a refusal, with its reason and bound in the log.
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
         {"OverseerDecisions::ReadUnresolvedErrand(target, guarded,",
          "_travelAims.Refuse(name, target, call.reason)",
          "refusing it for {}s"})
        if (text.find(needed) == std::string::npos)
        {
            std::cerr << "the unresolved errand's bound must be wired: " << needed << "\n";
            return EXIT_FAILURE;
        }
    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
