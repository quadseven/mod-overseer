/* Source contract for patches/azerothcore-wotlk/0003-iron-hall-flames-stop-when-magmus-dies.patch.
 *
 * In the pinned core the Ironhand Guardians' Gout of Flames loop is switched on
 * when Magmus engages (TYPE_IRON_HALL IN_PROGRESS) and, without this patch, never
 * switched off when he dies (TYPE_IRON_HALL DONE), so a party waiting in the hall
 * burns while out of combat. The patch must add, inside the DONE case, a loop
 * over the six guardians calling SetData(0, 0). Run from the repository root. */

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
    std::ifstream in("patches/azerothcore-wotlk/0003-iron-hall-flames-stop-when-magmus-dies.patch");
    if (!in)
        return Fail("cannot read the iron hall flames patch");
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string const file = "+++ b/src/server/scripts/EasternKingdoms/BlackrockMountain/BlackrockDepths/instance_blackrock_depths.cpp";
    std::size_t const at = text.find(file);
    if (at == std::string::npos)
        return Fail("patch no longer touches instance_blackrock_depths.cpp");
    std::string const diff = text.substr(at);

    // The DONE case context line must precede the added lines: the additions belong to DONE.
    std::size_t const done = diff.find("                case DONE:\n");
    std::size_t const door = diff.find("HandleGameObject(GetGuidData(DATA_THRONE_DOOR), true);");
    if (done == std::string::npos || door == std::string::npos || door < done)
        return Fail("patch no longer anchors on the TYPE_IRON_HALL DONE case");

    std::size_t const loop = diff.find("+                    for (int i = 0; i < 6; i++)\n", door);
    if (loop == std::string::npos)
        return Fail("patch does not add the six-guardian loop after the throne door opens");
    std::size_t const off = diff.find("+                            ironhand->AI()->SetData(0, 0);", loop);
    if (off == std::string::npos)
        return Fail("patch does not switch the guardians' flames off (SetData(0, 0))");

    // It must never switch them on: SetData(0, 1) added anywhere is the opposite of the fix.
    if (diff.find("+                            ironhand->AI()->SetData(0, 1);") != std::string::npos)
        return Fail("patch switches the flames on");

    // The IN_PROGRESS path (flames on during the fight) must be left to the pinned code.
    if (diff.find("-                            ironhand->AI()->SetData(0, data == IN_PROGRESS);") != std::string::npos)
        return Fail("patch removes the fight-time flames");

    return EXIT_SUCCESS;
}
