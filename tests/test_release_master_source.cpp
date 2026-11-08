/*
 * Source contract for #1437: the two places that let a character go of the
 * head clear its master only through MayClearMasterOnRelease, after the leave
 * has been tried, so a character still in the head's group is never cleared.
 *
 * Seen live: a released stand-in guest in the family's dungeon finder group
 * had its master cleared by SweepLeavingGuests every 5-second dungeon poll,
 * upstream handed it the head again on its next tick, and it whispered him
 * "Hello, I follow you!" about a dozen times a minute. The member sitting out
 * did the same through SendSittingOutMemberOff on every party poll.
 *
 * IT BITES. Against src/mod_overseer.cpp before the fix both functions call
 * SetMaster(nullptr) with no MayClearMasterOnRelease guard, before the group
 * is even read, and both checks fail.
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

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

int CheckRelease(std::string const& text, char const* signature, char const* leave)
{
    std::string const body = Body(text, signature);
    if (body.empty())
        return Fail(signature);
    std::size_t const guard = body.find("OverseerDecisions::MayClearMasterOnRelease(inHeadsGroup)");
    std::size_t const clear = body.find("SetMaster(nullptr)");
    std::size_t const tried = body.find(leave);
    if (guard == std::string::npos)
        return Fail("the master must be cleared only when MayClearMasterOnRelease allows it");
    if (clear == std::string::npos || clear < guard)
        return Fail("SetMaster(nullptr) must come after the MayClearMasterOnRelease guard");
    if (body.find("SetMaster(nullptr)", clear + 1) != std::string::npos)
        return Fail("there must be exactly one master clear, the guarded one");
    if (tried == std::string::npos || tried > guard)
        return Fail("the leave must be tried before the master is judged, so a character "
                    "that just left is let go on the same poll");
    std::size_t const inGroup = body.find("bool const inHeadsGroup");
    if (inGroup == std::string::npos || inGroup < tried || inGroup > guard)
        return Fail("whether it is still in the head's group must be read after the leave");
    return EXIT_SUCCESS;
}
}   // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (CheckRelease(text, "void SweepLeavingGuests()",
                     "guest->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);") != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckRelease(text, "void SendSittingOutMemberOff(",
                     "out->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);") != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "test_release_master_source: all checks passed\n";
    return EXIT_SUCCESS;
}
