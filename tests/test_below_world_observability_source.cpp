/* Source contract for below-world travel and recovery health reporting. */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
bool Has(std::string const& text, char const* needle, std::size_t from = 0)
{
    return text.find(needle, from) != std::string::npos;
}

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

    std::size_t const catchUp = text.find("bool CatchUpToward(");
    std::size_t const catchUpEnd = text.find("    // WHAT STANDS ON THE LINE", catchUp);
    if (catchUp == std::string::npos || catchUpEnd == std::string::npos)
        return Fail("cannot find CatchUpToward");
    std::string const aim = text.substr(catchUp, catchUpEnd - catchUp);
    if (!Has(aim, "MoveAimToShore(leader, ax, ay, az)"))
        return Fail("catch-up aim must measure ground and replace the raw z");
    if (!Has(aim, "AimShore::NoGround"))
        return Fail("catch-up aim must refuse when ground cannot be measured");
    if (!Has(text, "z = ground;"))
        return Fail("catch-up aim must write the measured surface height");

    if (!Has(text, "most recently '{}' at level {} \"\n                 \"in zone {} at map {} position"))
        return Fail("death log must include the coordinates stored in overseer_death");

    std::size_t const probe = text.find("static std::string ProbeState(Player* bot)");
    std::size_t const probeEnd = text.find("static ", probe + 1);
    if (probe == std::string::npos || probeEnd == std::string::npos)
        return Fail("cannot find ProbeState");
    std::string const health = text.substr(probe, probeEnd - probe);
    if (!Has(health, "recoveries_per_hour"))
        return Fail("health state must expose the recoveries-per-hour metric");
    if (!Has(text, "OverseerDecisions::RecoveriesPerHour(times, now)"))
        return Fail("health state must calculate recoveries over the last hour");

    std::cout << "below-world travel and health observability are wired\n";
    return EXIT_SUCCESS;
}
