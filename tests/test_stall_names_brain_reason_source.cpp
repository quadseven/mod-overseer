/* Source contract: the "cannot be cleared" stall line says what the dungeon brain
 * said, read from every steerable member inside and not only the family leader.
 *
 * Measured on the dev realm: a headless family's run ended 'stalled' with the
 * line naming the family leader, while the dungeon brain's elected leader was
 * another member (the paladin), so the reason read from the family leader was
 * empty and the log said nothing about the objective it could not finish.
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

    std::size_t const helper = text.find("static std::string PartyBrainStallReason(");
    if (helper == std::string::npos)
        return Fail("a helper must read the stall reason across the party");
    std::string const helperBody = text.substr(helper, 1200);
    if (helperBody.find("DungeonBrainStallReason(") == std::string::npos ||
        helperBody.find("ObjectAccessor::FindPlayerByName") == std::string::npos)
        return Fail("the helper must read each member's own brain reason");

    std::size_t const line = text.find("cannot be cleared - {} 'dc");
    if (line == std::string::npos)
        return Fail("cannot find the cannot-be-cleared line");
    std::string const around = text.substr(line, 1800);
    if (around.find("The dungeon brain says: {}") == std::string::npos ||
        around.find("PartyBrainStallReason(") == std::string::npos)
        return Fail("the line must say what the brain said, read across the party");
    if (text.find("TeleportTo", line) != std::string::npos &&
        text.find("TeleportTo", line) < line + 1800)
        return Fail("the stall line must never teleport");

    std::cout << "stall names brain reason source: ok\n";
    return EXIT_SUCCESS;
}
