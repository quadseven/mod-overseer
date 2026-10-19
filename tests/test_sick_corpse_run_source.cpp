/*
 * Source contract for a sick ghost's corpse run (mod-overseer#746).
 *
 * The decision is proved in test_sick_corpse_run.cpp. What only the adapter
 * can get wrong is whether DriveGhostRecovery's CorpseRun branch ASKS it and
 * hands its answer back to the caller, which is what keeps the stuck-revival
 * ladder off the ghost. Before #746 that branch returned false outright, and
 * the ladder took the spirit healer 45 s into the death.
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

    std::size_t const drive = text.find("bool DriveGhostRecovery(");
    if (drive == std::string::npos)
        return Fail("cannot find DriveGhostRecovery");
    std::size_t const start = text.find("case OverseerDecisions::GhostRecovery::CorpseRun:", drive);
    if (start == std::string::npos)
        return Fail("cannot find DriveGhostRecovery's CorpseRun branch");
    std::size_t const end = text.find("case OverseerDecisions::GhostRecovery::Wait:", start);
    if (end == std::string::npos)
        return Fail("cannot find the end of the CorpseRun branch");
    std::string const branch = text.substr(start, end - start);

    std::size_t const asks = branch.find("OverseerDecisions::LadderWaitsForCorpseRun(");
    if (asks == std::string::npos)
        return Fail("the CorpseRun branch must ask LadderWaitsForCorpseRun (#746)");
    if (branch.find("run.sick = facts.sick;") == std::string::npos)
        return Fail("the CorpseRun branch must pass the verdict's own sick fact (#746)");
    if (branch.substr(0, asks).find("return") != std::string::npos)
        return Fail("the CorpseRun branch returns before it asks LadderWaitsForCorpseRun (#746)");
    if (branch.find("return false;") != std::string::npos)
        return Fail("the CorpseRun branch hands the ghost to the ladder unasked (#746)");
    if (branch.find("return waits;") == std::string::npos)
        return Fail("the CorpseRun branch must return the stand-down it decided (#746)");

    std::cout << "sick corpse run source: all checks passed\n";
    return EXIT_SUCCESS;
}
