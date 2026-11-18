/* Source contract for the stuck "moving" read in the exit hearth pass (see
 * test_exit_hearth_stuck_moving.cpp): the pass clocks how long a member has
 * been read as moving, and past the limit says what the motion master and
 * the movement flags held and clears a moving flag that has no spline behind it.
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

    std::size_t const start = text.find("case ExitHearthStep::StopFirst:");
    std::size_t const end = text.find("case ExitHearthStep::Waiting:", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("cannot find the StopFirst case of the exit hearth pass");
    std::string const stop = text.substr(start, end - start);

    if (text.find("std::map<std::string, std::time_t> stopFirstSince;") == std::string::npos)
        return Fail("the episode must clock how long each member has been read as moving");
    if (stop.find("OverseerDecisions::ExitHearthStopIsStuck(") == std::string::npos)
        return Fail("the StopFirst case must ask whether the hold is stuck");
    if (stop.find("movespline->Finalized()") == std::string::npos ||
        stop.find("RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING)") == std::string::npos)
        return Fail("a stuck moving flag is cleared only when no spline is in flight");
    if (stop.find("GetUnitMovementFlags()") == std::string::npos)
        return Fail("the line must say what the movement flags held");
    if (stop.find("TeleportTo") != std::string::npos)
        return Fail("the exit hearth pass must never teleport");

    std::cout << "exit hearth stuck moving source: ok\n";
    return EXIT_SUCCESS;
}
