/*
 * Source contract for the party-wait re-arm and the guild split clock (#1308).
 *
 * What each decision answers is proved in test_dungeon_stall.cpp and
 * test_guild_run_stall.cpp. What only the adapter can get wrong, and what this
 * file pins:
 *
 *  - The watchdog asks DungeonClearWaitStopRearms only of a party nobody holds,
 *    before the skip ladder, and forgets every member's `dc on` record, which
 *    is what makes the arming drive issue it again.
 *  - The stall step keeps its split clock through GuildRunSplitClock and ends a
 *    split as its own step, naming the split.
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

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const watchdog = Body(text, "    bool RunClearingWatchdog(");
    std::size_t const rearm = watchdog.find("DungeonClearWaitStopRearms");
    std::size_t const skip = watchdog.find("DungeonClearStallAction::Skip)");
    if (rearm == std::string::npos || skip == std::string::npos || rearm > skip)
        return Fail("the re-arm is asked before the skip ladder");
    if (watchdog.substr(rearm - 60, 60).find("!anyBusy") == std::string::npos)
        return Fail("a busy party is not re-armed");
    if (watchdog.find("_dcOnIssued.erase(state.name)", rearm) > skip)
        return Fail("the re-arm forgets every member's dc on record");

    std::string const stall = Body(text, "    bool StepGuildRunStall(");
    if (stall.find("GuildRunSplitClock(") == std::string::npos)
        return Fail("the split clock goes through GuildRunSplitClock");
    if (stall.find("GuildRunStallStep::EndSplit") == std::string::npos)
        return Fail("a split ends as its own step");

    return EXIT_SUCCESS;
}
