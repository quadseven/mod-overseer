/*
 * Source contract for the bind map read behind the #744 and #746 hearths
 * (mod-overseer#274).
 *
 * The rule is proved in test_held_hearth_other_map.cpp. What only the adapter
 * can get wrong is whether it tells the two decisions where the stone is
 * bound. boundOnMemberMap defaults to false, so an adapter that never sets it
 * would stop both hearths entirely; one that sets it from the wrong map would
 * send a member across an ocean again, which is the 2026-10-19 split.
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

    // The far hold's ground check (#744).
    std::size_t const start = text.find("THE SPOT IT IS HELD ON IS JUDGED TOO (#744)");
    if (start == std::string::npos)
        return Fail("cannot find the far hold's ground check (#744)");
    std::size_t const end = text.find("OverseerDecisions::DecideHeldGround(ground)", start);
    if (end == std::string::npos)
        return Fail("cannot find the far hold's DecideHeldGround call");
    std::string const held = text.substr(start, end - start);
    std::size_t const bound =
        held.find("ground.boundOnMemberMap = p->m_homebindMapId == p->GetMapId();");
    if (bound == std::string::npos)
        return Fail("the far hold must tell DecideHeldGround whether the stone is bound on "
                    "the member's own map");
    std::size_t const needs = held.find("OverseerDecisions::HeldGroundNeedsSafeSpot(ground)");
    if (needs == std::string::npos || needs < bound)
        return Fail("the bind map must be read before HeldGroundNeedsSafeSpot is asked");

    // The revived sickness rule (#746).
    std::size_t const fn = text.find("void DriveRevivedSickness(");
    if (fn == std::string::npos)
        return Fail("cannot find DriveRevivedSickness");
    std::size_t const decide = text.find("OverseerDecisions::DecideRevivedSickGround(facts)", fn);
    if (decide == std::string::npos)
        return Fail("cannot find DriveRevivedSickness's DecideRevivedSickGround call");
    std::string const sick = text.substr(fn, decide - fn);
    if (sick.find("facts.boundOnMemberMap = bot->m_homebindMapId == bot->GetMapId();") ==
        std::string::npos)
        return Fail("the revived sickness rule must tell DecideRevivedSickGround whether the "
                    "stone is bound on the member's own map");

    std::cout << "held hearth other map source: all checks passed\n";
    return EXIT_SUCCESS;
}
