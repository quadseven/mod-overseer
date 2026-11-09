/*
 * Source contract for #1450: SweepLeavingGuests finds the family's group for a
 * released guest through OverseerDecisions::GuestGroupIsFamilys, so a head who
 * is not in the world for a poll does not make it forget a guest still in the
 * party.
 *
 * IT BITES. Against src/mod_overseer.cpp before the fix the group is read only
 * as `headPlayer ? headPlayer->GetGroup() : nullptr`, there is no decision call
 * and no read of the guest's own group leader, so the checks fail.
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
}   // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const body = Body(text, "void SweepLeavingGuests()");
    if (body.empty())
        return Fail("void SweepLeavingGuests() not found");
    std::size_t const decide = body.find("OverseerDecisions::GuestGroupIsFamilys(");
    std::size_t const leave = body.find("guest->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);");
    if (decide == std::string::npos)
        return Fail("the family's group must be chosen through GuestGroupIsFamilys");
    if (body.find("GetLeaderName() == head") == std::string::npos)
        return Fail("the guest's own group must be compared with the head by its leader's name");
    if (body.find("headPlayer ? headPlayer->GetGroup() : nullptr") == std::string::npos ||
        body.find("headPlayer != nullptr") == std::string::npos)
        return Fail("whether the head is in the world must be passed to the decision");
    if (leave == std::string::npos || leave < decide)
        return Fail("the leave must be tried on the group the decision chose");

    std::cout << "test_release_head_away_source: all checks passed\n";
    return EXIT_SUCCESS;
}
