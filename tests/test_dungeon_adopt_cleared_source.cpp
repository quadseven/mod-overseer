/*
 * Source contract for adopting a run in a cleared instance (#165).
 *
 * The rule is proved in test_dungeon_adopt_cleared.cpp. What only the adapter
 * can get wrong is whether it asks it: an adoption that still picks its phase
 * from DungeonRunAllThrough alone walks the party outside back into an instance
 * with nothing left in it, which is the 2026-10-20 Blackfathom loop.
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

    std::size_t const start = text.find("uint32 adoptedInside = 0;");
    if (start == std::string::npos)
        return Fail("cannot find the adoption census");
    std::size_t const end = text.find("adopting it at {}", start);
    if (end == std::string::npos)
        return Fail("cannot find the adoption line");
    std::string const adopt = text.substr(start, end - start);

    std::size_t const asked = adopt.find("OverseerDecisions::DungeonRunAdoptAt(");
    if (asked == std::string::npos)
        return Fail("adoption must pick its phase with DungeonRunAdoptAt");
    std::size_t const completion = adopt.find("CompletedEncounters(activeInside)");
    if (completion == std::string::npos || completion > asked)
        return Fail("adoption must read the inside member's credited encounters before "
                    "it picks a phase");
    if (adopt.find("? DungeonRunPhase::Clearing\n") != std::string::npos ||
        adopt.find("DungeonRunAllThrough(adoptedStates)\n                                  ?") !=
            std::string::npos)
        return Fail("adoption must not pick CLEARING or STAGED_INSIDE from the census alone");

    std::cout << "dungeon adopt cleared source: all checks passed\n";
    return EXIT_SUCCESS;
}
