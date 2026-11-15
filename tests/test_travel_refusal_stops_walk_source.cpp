/*
 * Source contract: a refused travel step STOPS the walk (raid door rehearsal).
 *
 * DriveTravel's refusal branch (every bearing in the forward cone refused)
 * used to log and `continue`, leaving the walk already in flight running and
 * `new rpg` granted, so upstream's own status update carried the character on
 * regardless. One refused character covered 2,335 yards across six refused
 * polls; in a raid rehearsal the same gap walked bots across a lava lake that
 * the gate had refused. The log line even said "Nothing is holding it".
 *
 * What this file pins, because only the adapter can get it wrong:
 *
 *  - the refusal branch calls StopRefusedWalk before it continues, on every
 *    refused poll (not only when it gives up);
 *  - StopRefusedWalk clears the MotionMaster, stops the unit, drops
 *    `new rpg` and idles the rpg status, each read back or guarded the way the
 *    rest of the file does it.
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

    std::string const drive = Body(text, "    void DriveTravel()\n");
    if (drive.empty())
        return Fail("cannot find DriveTravel");
    std::size_t const refused = drive.find("if (!stepped)");
    if (refused == std::string::npos)
        return Fail("cannot find the refusal branch (if (!stepped))");
    std::size_t const after = drive.find("// A poll that found a step ends any refusal episode", refused);
    if (after == std::string::npos)
        return Fail("cannot find the end of the refusal branch");
    std::string const branch = drive.substr(refused, after - refused);
    std::size_t const stop = branch.find("StopRefusedWalk(");
    std::size_t const cont = branch.rfind("continue;");
    if (stop == std::string::npos)
        return Fail("the refusal branch must call StopRefusedWalk");
    if (cont == std::string::npos || stop > cont)
        return Fail("StopRefusedWalk must run before the refusal branch continues");
    std::size_t const giveUpEnd = branch.find("_strandedGroundGiveUp[name]");
    if (giveUpEnd == std::string::npos || stop < giveUpEnd)
        return Fail("StopRefusedWalk must sit after the give-up block: it runs every refused poll");

    std::string const stopper = Body(text, "    static void StopRefusedWalk(");
    if (stopper.empty())
        return Fail("cannot find StopRefusedWalk");
    if (stopper.find("GetMotionMaster()->Clear()") == std::string::npos)
        return Fail("StopRefusedWalk must clear the MotionMaster");
    if (stopper.find("StopMoving()") == std::string::npos)
        return Fail("StopRefusedWalk must stop the unit");
    if (stopper.find("rpgInfo.ChangeToIdle()") == std::string::npos)
        return Fail("StopRefusedWalk must idle the rpg status");
    if (stopper.find("\"-new rpg\"") == std::string::npos)
        return Fail("StopRefusedWalk must drop `new rpg`");

    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
