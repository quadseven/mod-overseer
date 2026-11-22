/* Source contract for the town-trip loop fix (adapter wiring).
 *
 * The rules are tested in test_town_trip_cycle.cpp; this pins that the travel
 * drive tells the intent book when the maintenance errand is the bridge's (so
 * it outranks a fetch for a member left behind) and remembers who held the
 * errand off so the no-progress backoff does not restart under the same one.
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
        return std::cerr << "cannot read src/mod_overseer.cpp\n", EXIT_FAILURE;
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    for (char const* needle : {"bool const bridgeErrand =", "!MovementOwner().HoldsClaim(name, columnAim)",
                               "\"walks to '\" + target + \"'\",\n                                  bridgeErrand)",
                               "_travelAims.NoteUrgency(", "TRAVEL_STUCK_BACKOFF_SECONDS, backoff.urgency)"})
        if (text.find(needle) == std::string::npos)
            return std::cerr << "missing wiring: " << needle << "\n", EXIT_FAILURE;
    return EXIT_SUCCESS;
}
