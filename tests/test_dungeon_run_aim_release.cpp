#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

int failures = 0;

void Check(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheEndedRunCanClearOnlyItsExactPositionalAim()
{
    std::string const runAim = "at:1:10,20,30";
    Check("the current run aim can be released",
          OverseerDecisions::RunAimMayBeReleased(runAim, runAim));
    Check("a newer positional aim is preserved",
          !OverseerDecisions::RunAimMayBeReleased(
              "at:1:11,21,31", runAim));
    Check("a different owner's counter aim is preserved",
          !OverseerDecisions::RunAimMayBeReleased("vendor", runAim));
    Check("a non-positional value is not released",
          !OverseerDecisions::RunAimMayBeReleased("vendor", "vendor"));
    Check("an empty recorded run aim releases nothing",
          !OverseerDecisions::RunAimMayBeReleased("", ""));
}

void RunCompletionUsesAnExactCompareAndSwap()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream contents;
    contents << file.rdbuf();
    std::string const source = contents.str();
    Check("module source is readable", !source.empty());
    std::size_t const end = source.find("void EndRunAndDecide(");
    Check("run finalization exists", end != std::string::npos);
    if (end == std::string::npos)
        return;
    std::string const finalizer = source.substr(end, 12000);
    std::size_t const release = finalizer.find(
        "_travelAims.ReleaseRunAim(");
    std::size_t const guardedRelease = finalizer.find("RunAimMayBeReleased(");
    Check("run-scoped owner release remains", release != std::string::npos);
    Check("stale run aim is considered after scoped release",
          release != std::string::npos && guardedRelease > release);
    std::size_t const clear = finalizer.find("CharacterDatabase.DirectExecute(");
    std::string const clearCall = clear == std::string::npos
        ? std::string() : finalizer.substr(clear, 800);
    Check("database clear compares the character and exact recorded aim",
          clearCall.find("WHERE name = '{}' AND") != std::string::npos &&
              clearCall.find("travel_npc = '{}' AND enabled = 1") !=
                  std::string::npos);
    Check("the exact fallback clear completes before the next travel poll",
          finalizer.find("CharacterDatabase.DirectExecute(") != std::string::npos);
    Check("the CAS uses the captured run leg",
          finalizer.find("Esc(leaderName), Esc(recordedRunAim)") != std::string::npos);
    Check("run-specific provenance gates the fallback CAS",
          finalizer.find("mayClearRecordedRunAim &&") != std::string::npos);
    Check("completion uses the run-scoped release path",
          finalizer.find("_travelAims.ReleaseRunAim(") != std::string::npos);
}

void GeneralTravelReleaseCannotEraseAChangedAim()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream contents;
    contents << file.rdbuf();
    std::string const source = contents.str();
    std::size_t const release = source.find("void Release(std::string const& name");
    Check("travel release exists", release != std::string::npos);
    if (release == std::string::npos)
        return;
    std::string const body = source.substr(release, 9000);
    Check("a claim counts only when its recorded aim still matches",
          body.find("claimedAim->second == standing") != std::string::npos);
    Check("a changed live aim fences the stale claim's release",
          body.find("changedSinceClaim") != std::string::npos &&
              body.find("the column changed after this book's claim") != std::string::npos);
    Check("the generic clear compares against the freshly read aim",
          body.find("AND travel_npc = '{}'\", Esc(name), Esc(standing)") !=
              std::string::npos);
    Check("run-scoped release requires the recorded aim",
          body.find("bool ReleaseRunAim(") != std::string::npos &&
              body.find("claimed->second == expectedAim") != std::string::npos);
    Check("run-scoped release recognizes only run-owned aim kinds",
          body.find("TravelOwner::WalkBackIn") != std::string::npos &&
              body.find("TravelOwner::Exit") != std::string::npos);
    std::size_t const scopedMismatch = body.find("if (mismatchesRunAim)");
    std::size_t const intentEnd = body.find("EndLeaderColumnIntent(name");
    std::size_t const mismatchReturn = body.find("return;", scopedMismatch);
    Check("a foreign run-end aim returns before ending the newer intent",
          scopedMismatch != std::string::npos && intentEnd != std::string::npos &&
              mismatchReturn != std::string::npos &&
              mismatchReturn < intentEnd && scopedMismatch < intentEnd);
}

}  // namespace

int main()
{
    TheEndedRunCanClearOnlyItsExactPositionalAim();
    RunCompletionUsesAnExactCompareAndSwap();
    GeneralTravelReleaseCannotEraseAChangedAim();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("an ended run clears only its exact recorded positional aim");
    return EXIT_SUCCESS;
}
