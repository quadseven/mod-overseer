/*
 * Source contract for the far hold after a refused hearth (mod-overseer#744).
 *
 * The rule is proved in test_held_ground_combat.cpp. What only the adapter can
 * get wrong is what happens after the cast is refused. On wow-dev 2026-10-19
 * (roll 64b) ten hearths under the far hold were refused, six "in combat" and
 * four "moving". Each one set `hearthTried`, and the ground check (#744) was
 * skipped for any hold whose `hearth` was set, so the member was never judged
 * again: no walk to a graveyard, no second hearth, held still until the 900s
 * ceiling. Zrog died 9s after its refusal and Ugga 33s after hers.
 *
 * So: the ground check runs again on a hold whose hearth was refused, it
 * counts that stone out for the rest of the hold, and it tells the decision
 * whether the member is in combat.
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
    std::size_t const end = text.find("OverseerDecisions::DecideHeldGround(ground)", start);
    if (end == std::string::npos)
        return Fail("cannot find the far hold's DecideHeldGround call");
    std::string const held = text.substr(start, end - start);

    std::size_t const refused =
        held.find("bool const hearthRefused = lethal != _lethalLegHeld.end() &&");
    if (refused == std::string::npos ||
        held.find("lethal->second.hearthTried &&", refused) == std::string::npos ||
        held.find("!lethal->second.hearthCast;", refused) == std::string::npos)
        return Fail("the far hold must name a hold whose hearth was tried and refused");
    std::size_t const gate = held.find(
        "if (lethal == _lethalLegHeld.end() || !lethal->second.hearth || hearthRefused)");
    if (gate == std::string::npos || gate < refused)
        return Fail("the ground check must run again on a hold whose hearth was refused");
    std::size_t const ready = held.find("!HearthPendingFor(name) && !hearthRefused;", gate);
    if (ready == std::string::npos)
        return Fail("a refused hearth must not count as a ready stone for the rest of the hold");
    std::size_t const combat = held.find("ground.inCombat = p->IsInCombat();", gate);
    if (combat == std::string::npos)
        return Fail("the far hold must tell DecideHeldGround whether the member is in combat");
    std::size_t const needs =
        held.find("OverseerDecisions::HeldGroundNeedsSafeSpot(ground)", gate);
    if (needs == std::string::npos || needs < combat || needs < ready)
        return Fail("combat and the refused hearth must be read before HeldGroundNeedsSafeSpot");

    std::cout << "held ground combat source: all checks passed\n";
    return EXIT_SUCCESS;
}
