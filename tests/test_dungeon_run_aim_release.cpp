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

// The run finalizer ends its claim, then retires its exact recorded leg,
// both through the movement ownership module (#1312). What those two calls do
// is proved by behavior in test_movement_ownership.cpp (the run that ends while
// a newer aim stands, and the run whose claim a restart lost); this pins that
// the finalizer asks them in that order and writes no SQL of its own.
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
        "bool const mayClearRecordedRunAim = _travelAims.ReleaseRunAim(");
    std::size_t const retire = finalizer.find(
        "_travelAims.RetireRunAim(leaderName, recordedRunAim, mayClearRecordedRunAim)");
    Check("run-scoped owner release remains", release != std::string::npos);
    Check("the exact leg is retired after the scoped release, gated on its answer",
          release != std::string::npos && retire != std::string::npos && retire > release);
    Check("the finalizer writes no travel column SQL of its own",
          finalizer.find("SET travel_npc") == std::string::npos);

    std::ifstream header("src/overseer_movement_ownership.h");
    std::stringstream headerContents;
    headerContents << header.rdbuf();
    std::string const module = headerContents.str();
    std::size_t const retireBody = module.find("RunAimRetireResult RetireRunAim(");
    Check("the module retires a run leg", retireBody != std::string::npos);
    if (retireBody == std::string::npos)
        return;
    std::string const body = module.substr(retireBody, 1200);
    Check("only the exact positional leg is retired",
          body.find("OverseerDecisions::RunAimMayBeReleased(") != std::string::npos);
    Check("by the synchronous compare-and-swap that the next read sees",
          body.find("_column.ClearNowIfStill(name, recordedRunAim)") != std::string::npos);
}

void ExitCrossingRecordsTheAimRunCompletionMustRelease()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream contents;
    contents << file.rdbuf();
    std::string const source = contents.str();
    std::size_t const crossing = source.find(
        "DungeonCrossingResult DriveDungeonCrossing(");
    Check("dungeon crossing exists", crossing != std::string::npos);
    if (crossing == std::string::npos)
        return;
    std::size_t const end = source.find(
        "// ---------------------------------------- dungeon run: the straggler",
        crossing);
    Check("dungeon crossing has a bounded body", end != std::string::npos);
    if (end == std::string::npos)
        return;
    std::string const body = source.substr(crossing, end - crossing);
    std::size_t const doorAim = body.find(
        "std::string const doorAim = OverseerDecisions::TravelAimAtPosition(");
    std::size_t const recordedAim = body.find(
        "coord.legAim[leaderName] = doorAim;");
    Check("the crossing records its exact door aim for run finalization",
          doorAim != std::string::npos && recordedAim != std::string::npos &&
              recordedAim > doorAim);
}

// Every travel release asks the movement ownership module, whose release is
// proved by behavior in test_movement_ownership.cpp: a changed column is not
// erased by a stale claim, and a run's release leaves a newer aim and its
// owner's claim in place. What the adapter can still get wrong is ending the
// newer owner's intent before it looks at the module's answer.
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
    Check("the general release asks the module",
          body.find("_wheel.Release(name, std::time(nullptr))") != std::string::npos);
    Check("the run-scoped release asks the module with the recorded aim",
          body.find("bool ReleaseRunAim(") != std::string::npos &&
              body.find("_wheel.ReleaseRunAim(name, expectedAim,") != std::string::npos);
    std::size_t const scopedMismatch = body.find("if (released.leftNewerAim)");
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
    ExitCrossingRecordsTheAimRunCompletionMustRelease();
    GeneralTravelReleaseCannotEraseAChangedAim();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("an ended run clears only its exact recorded positional aim");
    return EXIT_SUCCESS;
}
