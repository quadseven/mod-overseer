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

    std::size_t const observation =
        text.find("OverseerDecisions::ObserveTerrainMismatch(");
    if (observation == std::string::npos)
        return Fail("terrain recovery must observe sustained raw-terrain mismatches");
    std::size_t const mismatchLog = text.find("remained below raw terrain", observation);
    if (mismatchLog == std::string::npos ||
        !Has(text, "last movement step", mismatchLog) ||
        !Has(text, "recovery reason", mismatchLog))
        return Fail("the sustained mismatch log must name its movement step and recovery reason");
    std::size_t const reasonSwitch = text.find("switch (verdict.remedy)", observation);
    std::size_t const reasonEnd = text.find("std::string const lastMovementStep", reasonSwitch);
    if (reasonSwitch == std::string::npos || reasonEnd == std::string::npos)
        return Fail("the sustained mismatch log must explain each recovery outcome");
    std::string const reasons = text.substr(reasonSwitch, reasonEnd - reasonSwitch);
    if (!Has(reasons, "TerrainRemedy::Nothing") ||
        !Has(reasons, "TerrainRemedy::LiftToSurface") ||
        !Has(reasons, "TerrainRemedy::GiveUp") ||
        !Has(reasons, "TerrainRemedy::NotFalling") ||
        !Has(reasons, "TerrainRemedy::ReturnToLastGround") ||
        !Has(reasons, "TerrainRemedy::SetDown"))
        return Fail("the sustained mismatch log must explain each recovery outcome");
    if (!Has(text, "std::string CurrentTerrainMovementStep(") ||
        !Has(text, "FoldMovementGenerator(motion->GetCurrentMovementGeneratorType())") ||
        !Has(text, "g_aimSnapshot.find(LowerName(name))") ||
        !Has(text, "mismatchMemory.lastMovementStep"))
        return Fail("the mismatch log must retain the sampled movement generator and aim step");

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
