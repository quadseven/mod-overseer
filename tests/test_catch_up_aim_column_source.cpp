/*
 * Source contract for the travel column's width (#1139).
 *
 * overseer_roster.travel_npc is TRAVEL_AIM_COLUMN_CHARS (32) wide and the
 * realm database runs in strict mode, so an over-long aim is refused by MySQL
 * (errno 1406) with no value in the error, while the module believes it wrote
 * it. On the dev realm three followers were logged as aimed at a shore-moved
 * catch-up point and no aim was written, because the catch-up walk start built
 * `at:<map>:<x>,<y>,<z>` with a bare ostringstream (six significant digits).
 *
 * What TravelAimAtPosition returns is proved in test_catch_up_aim.cpp. What
 * only the adapter can get wrong, and what this file pins:
 *
 *  - TravelAimBook::Claim refuses a target longer than TRAVEL_AIM_COLUMN_CHARS
 *    before it reads a fence or runs the UPDATE, says the refused value, and
 *    answers false, so it writes nothing.
 *  - FenceRefuses predicts that refusal.
 *  - The catch-up walk start sizes its aim with TravelAimAtPosition, and an
 *    empty answer returns before TravelAimBook::Claim is called.
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

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
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

    // --- TravelAimBook::Claim -------------------------------------------------
    std::string const claim = Body(text, "    bool Claim(std::string const& name, std::string const& target,");
    if (claim.empty())
        return Fail("cannot find TravelAimBook::Claim");
    std::size_t const guard = claim.find("if (target.size() > TRAVEL_AIM_COLUMN_CHARS)");
    if (guard == std::string::npos)
        return Fail("Claim must refuse a target longer than TRAVEL_AIM_COLUMN_CHARS");
    std::size_t const firstFence = claim.find("_state.find(name)");
    std::size_t const update = claim.find("UPDATE overseer_roster SET travel_npc");
    if (update == std::string::npos)
        return Fail("cannot find the travel_npc UPDATE in Claim");
    if (firstFence == std::string::npos || guard > firstFence || guard > update)
        return Fail("the width refusal must come before any fence and before the UPDATE");
    std::size_t const guardEnd = claim.find("\n        }\n", guard);
    if (guardEnd == std::string::npos)
        return Fail("cannot find the end of the width refusal block");
    std::string const refusal = claim.substr(guard, guardEnd - guard);
    if (refusal.find("return false;") == std::string::npos)
        return Fail("the width refusal must answer false");
    if (refusal.find("LOG_ERROR") == std::string::npos || refusal.find("target") ==
                                                              std::string::npos)
        return Fail("the width refusal must log the refused value");
    if (refusal.find("Execute") != std::string::npos ||
        refusal.find("UPDATE") != std::string::npos)
        return Fail("the width refusal must write nothing");

    // --- FenceRefuses ---------------------------------------------------------
    std::string const fence = Body(text, "    bool FenceRefuses(std::string const& name, std::string const& target,");
    if (fence.empty())
        return Fail("cannot find TravelAimBook::FenceRefuses");
    if (fence.find("target.size() > TRAVEL_AIM_COLUMN_CHARS") == std::string::npos)
        return Fail("FenceRefuses must predict the width refusal");

    // --- the catch-up walk start ----------------------------------------------
    char const* const claimCall =
        "_travelAims.Claim(name, aimText, OverseerDecisions::TravelOwner::CatchUp)";
    std::size_t const call = text.find(claimCall);
    if (call == std::string::npos)
        return Fail("cannot find the catch-up walk's claim");
    std::size_t const build = text.rfind("std::string const aimText", call);
    if (build == std::string::npos)
        return Fail("cannot find where the catch-up aim is built");
    std::string const between = text.substr(build, call - build);
    if (between.find("OverseerDecisions::TravelAimAtPosition(") == std::string::npos)
        return Fail("the catch-up aim must be built by TravelAimAtPosition");
    if (between.find("aim.str()") != std::string::npos)
        return Fail("the catch-up aim must not come from a bare ostringstream");
    std::size_t const empty = between.find("if (aimText.empty())");
    if (empty == std::string::npos)
        return Fail("an empty catch-up aim must be refused before the claim");
    std::size_t const emptyEnd = between.find("\n        }\n", empty);
    if (emptyEnd == std::string::npos ||
        between.substr(empty, emptyEnd - empty).find("return false;") == std::string::npos)
        return Fail("an empty catch-up aim must return before claiming");

    // No catch-up aim is assembled by hand anywhere in the file any more.
    if (text.find("aim << \"at:\" << leader->GetMapId()") != std::string::npos)
        return Fail("a hand-built `at:` aim from the leader's position is back");

    std::cout << "test_catch_up_aim_column_source: all cases passed\n";
    return EXIT_SUCCESS;
}
