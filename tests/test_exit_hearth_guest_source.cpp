/*
 * Source contract for the stand-in guest fact behind the EXIT hearths
 * (#1427).
 *
 * The rule is proved in test_exit_hearth.cpp. What only the adapter can get
 * wrong is whether it tells the decision that a member is a stand-in guest:
 * `standinGuest` defaults to false, so a builder that never sets it keeps a
 * guest's stone withheld and leaves the guest inside the dungeon, the
 * 2026-11-08 Blackrock Depths reading.
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

// The text from `from` to the next `until` after it, or empty.
std::string Between(std::string const& text, std::string const& from, std::string const& until)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return "";
    std::size_t const end = text.find(until, start);
    if (end == std::string::npos)
        return "";
    return text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::string const guestFact = "facts.standinGuest = IsStandinGuest(name);";

    std::string const exitHearths =
        Between(text, "bool DriveExitHearths(", "OverseerDecisions::ExitFailureHearthStep(facts)");
    if (exitHearths.empty())
        return Fail("cannot find DriveExitHearths' fact builder");
    if (exitHearths.find(guestFact) == std::string::npos)
        return Fail("DriveExitHearths must tell the decision whether the member is a stand-in "
                    "guest (#1427)");

    std::string const evacuation =
        Between(text, "if (groundGaveUp)\n", "OverseerDecisions::DecideEvacuationWay(");
    if (evacuation.empty())
        return Fail("cannot find the evacuation hearth's fact builder");
    if (evacuation.find(guestFact) == std::string::npos)
        return Fail("the evacuation hearth must tell the decision whether the member is a "
                    "stand-in guest (#1427)");

    std::cout << "exit hearth guest source contract: ok\n";
    return EXIT_SUCCESS;
}
