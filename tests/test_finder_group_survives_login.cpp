/*
 * A bot logging back in after a restart keeps its dungeon finder group.
 *
 * The live failure this pins (dev realm, 2026-10-10 14:02 UTC, the first
 * restart after #896): four guild runs were inside, and the worldserver loaded
 * all four finder groups from the database (6 groups and 29 members at
 * startup: the two family parties plus 4 x 5). Seconds later every one of them
 * was gone, `group_member` held only the two family parties, and the members
 * were each in a dungeon instance of their own (Bigmon in instance 10,
 * Chillmon in 5, Deadpan in 7, Grumbles in 8, all map 36). Three runs ended
 * "lost at the restart: its group was disbanded"; the fourth's tank was not
 * back yet.
 *
 * The cause is mod-playerbots' bot login (PlayerbotHolder::OnBotLogin,
 * src/Bot/PlayerbotMgr.cpp): a bot whose group holds neither its master nor a
 * character from a non-random account leaves it. A guild member is a random
 * bot with no master after a restart, so the first member to log back in left
 * its finder group, and so did the next, until the core disbanded it and its
 * instance bind with it. RandomPlayerbotMgr already exempts a finder group from
 * its own "leave a group a random bot leads" rule; patch 0016 gives the login
 * check the same exemption.
 *
 * Compiled with nothing but the standard library; reads the patch as text
 * (run from the repo root).
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

std::string Read(char const* path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void TheLoginCheckSparesAFinderGroup()
{
    std::string const patch =
        Read("patches/mod-playerbots/0016-a-bot-logging-in-keeps-its-finder-group.patch");
    Check("the patch exists", !patch.empty());
    Check("it patches the bot login", patch.find("+++ b/src/Bot/PlayerbotMgr.cpp") != std::string::npos);
    Check("it touches the leave-a-group rule and nothing else",
          patch.find("-        if (!groupValid)\n") != std::string::npos &&
              patch.find("+        if (!groupValid && !group->isLFGGroup())\n") != std::string::npos);
    Check("the bot still leaves an ordinary party its master is not in",
          patch.find("botAI->LeaveOrDisbandGroup();") != std::string::npos &&
              patch.find("-            botAI->LeaveOrDisbandGroup();") == std::string::npos);
    Check("it says why", patch.find("WHY THIS PATCH EXISTS") != std::string::npos);
    Check("it says what would retire it",
          patch.find("WHAT WOULD LET THIS PATCH BE DELETED") != std::string::npos);
    Check("it names the tree it applies to", patch.find("APPLIES TO") != std::string::npos);
}

}  // namespace

int main()
{
    TheLoginCheckSparesAFinderGroup();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
