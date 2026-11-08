/*
 * Source contract for the guild rogue's Lockpicking practice (mod-overseer#1440).
 *
 * The decisions are proved in test_guild_rogue_lockpick.cpp and
 * test_drive_lockpick.cpp. What only the adapter can get wrong is feeding them:
 *
 *  - the cap check must read the CURRENT Lockpicking value. A rogue's maximum
 *    rises with its level on its own, so a drive that compared the maximum
 *    cleared the errand of every rogue on the first poll;
 *  - the natural guild's drive must be polled, must ask the decisions, and must
 *    read the skill back after the cast rather than trust the cast.
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

    std::string const roster = Body(text, "void DriveLockpick()");
    if (roster.empty())
        return Fail("cannot find DriveLockpick");
    if (roster.find("ShouldClearLockpickErrand(bot->GetPureSkillValue(SKILL_LOCKPICKING)") ==
        std::string::npos)
        return Fail("DriveLockpick must judge the level cap by the current Lockpicking value");
    if (roster.find("GetPureMaxSkillValue") != std::string::npos)
        return Fail("DriveLockpick must not judge the level cap by the maximum, which is at the cap already");

    std::string const guild = Body(text, "void DriveGuildLockpick()");
    if (guild.empty())
        return Fail("cannot find DriveGuildLockpick");
    if (guild.find("NaturalGuilds()") == std::string::npos)
        return Fail("DriveGuildLockpick must serve the natural guild's members");
    if (guild.find("LockpickErrandApplies(false, true, true, bot->HasSpell(1804)") ==
        std::string::npos)
        return Fail("DriveGuildLockpick must ask LockpickErrandApplies for a natural member");
    if (guild.find("GetPureSkillValue(SKILL_LOCKPICKING)") == std::string::npos)
        return Fail("DriveGuildLockpick must read the current Lockpicking value");
    if (guild.find("DecideNaturalLockpick(") == std::string::npos)
        return Fail("DriveGuildLockpick must ask DecideNaturalLockpick each poll");
    std::size_t const castCase = guild.find("NaturalLockpickStep::Cast:");
    std::size_t const cast = guild.find("DriveChestOpen(");
    if (castCase == std::string::npos || cast == std::string::npos || cast < castCase)
        return Fail("the Pick Lock cast must come after the Cast step of the decision");
    if (guild.find("after > before", cast) == std::string::npos)
        return Fail("the cast must be judged by the Lockpicking skill read-back");

    // It is polled with the roster errands, on the same clock.
    std::size_t const poll = text.find("DriveLockpick();\n            DriveGuildLockpick();");
    if (poll == std::string::npos)
        return Fail("DriveGuildLockpick must be polled right after DriveLockpick");

    std::cout << "test_guild_lockpick_source: all cases passed\n";
    return EXIT_SUCCESS;
}
