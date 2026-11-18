/* Source contract for the stale moving flag in the hearth verb (see
 * test_hearth_stale_moving.cpp): DoHearth asks HearthMovingIsStale before it
 * refuses a moving member, clears the flag and goes on to cast when it is
 * stale, says the flags the first time, and keeps the refusal when the member
 * is really moving.
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

    std::size_t const start = text.find("static char const* DoHearth(");
    std::size_t const end = text.find("character is already casting", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("cannot find DoHearth up to its casting refusal");
    std::string const body = text.substr(start, end - start);

    std::size_t const ask = body.find("OverseerDecisions::HearthMovingIsStale(");
    std::size_t const refusal = body.find("refuse(\"character is moving\")");
    if (ask == std::string::npos)
        return Fail("DoHearth must ask whether the moving read is stale");
    if (refusal == std::string::npos)
        return Fail("a member that is really moving must keep the refusal");
    if (body.find("movespline->Finalized()") == std::string::npos)
        return Fail("stale means no spline in flight");
    if (body.find("RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING)") == std::string::npos)
        return Fail("a stale moving flag must be cleared");
    if (body.find("GetUnitMovementFlags()") == std::string::npos)
        return Fail("the log must say what the movement flags held");
    if (body.find("TeleportTo") != std::string::npos)
        return Fail("the hearth verb must never teleport");
    std::size_t const hold = body.find("HoldStillAndReport(who, ev.character, \"hearth\", ev.hold);");
    if (hold == std::string::npos || hold < ask)
        return Fail("the hold is placed only after the stale check says the member is really moving");

    std::cout << "hearth stale moving source: ok\n";
    return EXIT_SUCCESS;
}
