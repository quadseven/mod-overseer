/*
 * Source contract for the split_failed walk-back fix (#1472).
 *
 * The rules are proved in test_split_failed_walk_back.cpp. What only the adapter
 * can get wrong is whether it asks them: that the ceiling consults the progress
 * hold before closing, that boss credit is read while STAGED_INSIDE, that a
 * stuck walk back to the door uses the hearthstone, and that the warning line is
 * said again when the patience changes.
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

    std::string const hold =
        Between(text, "WalkStrandedBackIn(leaderName, *portal, door, triggerId, states,",
                "static_cast<uint32>(states.size()), blockers,");
    Require(hold, "OverseerDecisions::SplitFailedHeldByProgress(", "the ceiling asks the hold");
    Require(hold, "if (assembling.stalled && !fightingOn)", "a fight is not closed on the ceiling");
    Require(hold, "CompletedEncounters(", "boss credit is read while staged inside");
    Require(hold, "coord.clearAdvancedAt = std::time(nullptr)", "credit stamps progress");
    Require(text, "OverseerDecisions::StagedInsideWarningStale(",
            "the warning is said again when the patience changes");
    Require(text, "coord.warnedPatience = assemblyLimits.patienceSeconds",
            "the stated patience is remembered");

    std::string const walk =
        Between(text, "uint32 WalkStrandedBackIn(", "std::vector<OverseerDecisions::DungeonRunEntryState> walking;");
    Require(walk, "OverseerDecisions::StrandedWalkBackFailed(", "a stuck walk back is bounded");
    Require(walk, "OverseerDecisions::DecideEvacuationWay(", "a failed walk may hearth");
    Require(walk, "DriveExitHearths(", "the stone is the existing hearth drive");
    Require(walk, "_strandedHearths[family]", "the episode is the family's own");
    Require(walk, "\"the walk back to the door failed\"",
            "the hearth is cast with its reason, which DriveExitHearths writes to the run timeline");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "split failed walk back source: all checks passed\n";
    return EXIT_SUCCESS;
}
