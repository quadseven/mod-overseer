/* The hearth landing decision is useful only if the adapter checks each
 * proposed landing against the leader before issuing the hearth command. */

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
    std::size_t const start = text.find("bool DriveExitHearths(");
    std::size_t const end = text.find("// ---- a Stratholme run", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("cannot find DriveExitHearths");
    std::string const driver = text.substr(start, end - start);
    std::size_t const landing = driver.find("ExitHearthLandingWithinWalkRange(");
    std::size_t const cast = driver.find("DoHearth(bot, \"use\"");
    if (landing == std::string::npos || cast == std::string::npos || landing > cast)
        return Fail("the leader-range landing check must happen before every hearth cast");
    if (driver.find("CATCH_UP_FOOT_LIMIT_YARDS") == std::string::npos)
        return Fail("the exit hearth must use the family's existing catch-up walk limit");
    if (driver.find("facts.landingWithinLeaderWalkRange") == std::string::npos)
        return Fail("the adapter must pass its cohort landing check into the decision");

    std::cout << "exit hearth cohesion source contract: ok\n";
    return EXIT_SUCCESS;
}
