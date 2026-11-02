/*
 * Source contract for the CLEARING stall path's two endings (#1322): a run
 * whose dungeon module stopped on all bosses cleared walks out through EXIT as
 * complete, and a refused skip with any other stop reason still sets
 * `stalledReason`.
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
    std::cerr << "FAIL " << what << "\n";
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

    std::size_t const start = text.find("coord.clearSkipRefused = true;");
    std::size_t const end = text.find("coord.phase = DungeonRunPhase::Exiting;", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("cannot find the CLEARING stall path's ending");
    std::string const ending = text.substr(start, end - start);

    std::size_t const cleared = ending.find(
        "coord.clearSkipRefused &&\n"
        "            OverseerDecisions::DungeonModuleStopIsCleared(DungeonBrainStopReason(leader)))");
    if (cleared == std::string::npos)
        return Fail("the stall path must ask whether a refused skip followed a cleared stop");

    std::size_t const complete = ending.find("coord.provedComplete = true;", cleared);
    if (complete == std::string::npos)
        return Fail("a cleared stop must end the run as complete");

    std::size_t const otherwise = ending.find("else", complete);
    std::size_t const stalled = ending.find("coord.stalledReason =", complete);
    if (otherwise == std::string::npos || stalled == std::string::npos || stalled < otherwise)
        return Fail("only a stop that is not a clear may set stalledReason");
    if (ending.find("coord.stalledReason =") < cleared)
        return Fail("stalledReason must not be set before the cleared check");

    if (ending.find("OverseerDecisions::DungeonModuleStopWords(DungeonBrainStopReason(leader)) +\n"
                    "                          \"; it refused 'dc skip' \"",
                    stalled) == std::string::npos)
        return Fail("a refused skip with any other stop reason must still name it in "
                    "stalledReason");

    std::cout << "PASS: a cleared module stop ends complete; any other stop still stalls\n";
    return EXIT_SUCCESS;
}
