/*
 * Source contract for a run that hearths its family out (#1316).
 *
 * The rules are proved in test_split_run_recovery.cpp. What only the adapter can
 * get wrong is whether it asks them: who counts as hearthed out, whether an
 * abandoned run can be proved complete, and when the run-over mark is dropped.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Require(std::string const& text, char const* needle, char const* what)
{
    if (text.find(needle) != std::string::npos)
        return;
    std::cerr << "FAIL " << what << ": missing '" << needle << "'\n";
    ++failures;
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    std::size_t const end = start == std::string::npos ? start : text.find(to, start);
    if (end == std::string::npos)
    {
        std::cerr << "FAIL cannot find '" << from << "' to '" << to << "'\n";
        ++failures;
        return "";
    }
    return text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    // Only an accepted cast counts: operator[] on attempts inserts zeros.
    std::string const cast = Between(text, "if (refusal.empty())", "could not hearth out of map");
    Require(cast, "episode.hearthedOut.insert(name)", "an accepted cast records the hearth");
    std::string const helper =
        Between(text, "static bool EpisodeHearthedSomebodyOut", "bool DriveExitHearths");
    Require(helper, "episode.hearthedOut.find(name)", "the helper reads the accepted casts");
    Require(text, "facts.familyHearthedOut = familyHearthedOut;", "the hearth step is told");

    // An abandoned run is not proved complete, and its reason is carried.
    std::string const adopt = Between(text, "uint32 adoptedInside = 0;", "adopting it at {}");
    Require(adopt, "AdoptedExitProvesComplete(adoptedCompletion)", "the proof asks the mask");
    Require(adopt, "coord.stalledReason = runOver->second.reason", "the reason is carried");
    if (adopt.find("coord.provedComplete = true;") != std::string::npos)
    {
        std::cerr << "FAIL an adopted EXIT must not assume completion\n";
        ++failures;
    }

    // The mark outlives a failed EXIT and is dropped by a fresh run.
    Require(text, "DungeonRunPhaseStartsAFreshRun(coord.phase)", "a fresh run drops the mark");
    Require(Between(text, "void FailSplitInside(", "void EndRunAndDecide"),
            "_runOver[FamilyOfCoordinator(coord)]", "a closed split marks the run over");

    // The hold reads the rule rather than the constant.
    Require(text, "assemblyLimits.patienceSeconds = OverseerDecisions::StagedInsideBackstopSeconds",
            "the hold asks how long it may wait");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "split run recovery source: all checks passed\n";
    return EXIT_SUCCESS;
}
