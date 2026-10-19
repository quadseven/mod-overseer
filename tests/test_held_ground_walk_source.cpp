/*
 * Source contract for the far hold's walk to safe ground (mod-overseer#744).
 *
 * The rule is proved in test_held_ground_walk.cpp. What only the adapter can
 * get wrong is whether the far hold ever TELLS DecideHeldGround a safe spot is
 * known, and whether it acts on WalkToSafety. Before this change nothing set
 * safeSpotKnown, so the WalkToSafety branch had no caller and a member with its
 * stone not ready was held still on lethal ground.
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

    std::size_t const start = text.find("THE SPOT IT IS HELD ON IS JUDGED TOO (#744)");
    if (start == std::string::npos)
        return Fail("cannot find the far hold's ground check (#744)");
    std::size_t const end = text.find("EndFarHold(name, lethal != _lethalLegHeld.end()", start);
    if (end == std::string::npos)
        return Fail("cannot find the end of the far hold block");
    std::string const block = text.substr(start, end - start);

    if (block.find("OverseerDecisions::HeldGroundNeedsSafeSpot(ground)") == std::string::npos)
        return Fail("the far hold must ask HeldGroundNeedsSafeSpot before reading a graveyard");
    if (block.find("ground.safeSpotKnown = true;") == std::string::npos)
        return Fail("the far hold must tell DecideHeldGround when a safe spot is known");
    if (block.find("ground.safeSpotTopLevel =") == std::string::npos ||
        block.find("ground.safeSpotYards =") == std::string::npos)
        return Fail("the far hold must pass the safe spot's level and distance");
    std::size_t const walk = block.find("WalkHeldToSafeGround(p, name, *safeGrave)");
    if (walk == std::string::npos)
        return Fail("the far hold must walk a member to safe ground on WalkToSafety");
    std::size_t const hold = block.find("HoldFarFromLeader(p, name);");
    if (hold == std::string::npos || hold > walk)
        return Fail("the walk must follow the poll's re-assertion of the hold, or the pin "
                    "would cancel it");

    std::size_t const fn = text.find("bool WalkHeldToSafeGround(");
    if (fn == std::string::npos)
        return Fail("cannot find WalkHeldToSafeGround");
    std::string const body = text.substr(fn, text.find("void EndFarHold(", fn) - fn);
    if (body.find("HeldWaitingForLeader(name)") == std::string::npos)
        return Fail("WalkHeldToSafeGround must walk only a member under the far hold");
    if (body.find("GroundedStep(") == std::string::npos)
        return Fail("WalkHeldToSafeGround must step over proved ground (GroundedStep)");
    if (body.find("LetHeldCharacterWalk(") == std::string::npos)
        return Fail("WalkHeldToSafeGround must tell the sweep the walk is deliberate");

    std::cout << "held ground walk source: all checks passed\n";
    return EXIT_SUCCESS;
}
