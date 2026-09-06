/*
 * A run does not open on top of an errand somebody else is still running (#168).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter reads the leader's travel aim and counts unanswered
 * economy rows; what is pinned here is when that means the coordinator waits,
 * when it means it goes anyway, and the one property that matters more than
 * either, which is that the wait always ends.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonRunMaintenanceHold;
using OverseerDecisions::IsMaintenanceErrand;
using OverseerDecisions::MaintenanceHold;

namespace
{

int failures = 0;
constexpr time_t BOUND = 20 * 60;

char const* Name(MaintenanceHold hold)
{
    switch (hold)
    {
        case MaintenanceHold::Open:        return "Open";
        case MaintenanceHold::Walking:     return "Walking";
        case MaintenanceHold::Transacting: return "Transacting";
        case MaintenanceHold::Overdue:     return "Overdue";
    }
    return "?";
}

void Check(char const* what, MaintenanceHold got, MaintenanceHold want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void TheThreeEconomyErrandsAreRecognised()
{
    // The same three the bridge treats as economy errands. A role missing from
    // this list is a pass the coordinator would go on trampling silently.
    CheckBool("vendor", IsMaintenanceErrand("vendor"), true);
    CheckBool("banker", IsMaintenanceErrand("banker"), true);
    CheckBool("repair", IsMaintenanceErrand("repair"), true);
}

void NothingElseIsOneOfThem()
{
    // A trainer errand is somebody's profession, not a transaction, and the
    // coordinator has always been free to take it over. Widening this would
    // change behaviour that is working.
    CheckBool("trainer", IsMaintenanceErrand("trainer"), false);
    CheckBool("profession trainer", IsMaintenanceErrand("profession trainer"), false);
    CheckBool("innkeeper", IsMaintenanceErrand("innkeeper"), false);
    CheckBool("empty", IsMaintenanceErrand(""), false);
    // An `at:` aim is a place, which is what the coordinator writes for its own
    // staging point. Reading one as an errand would make the run wait for
    // itself.
    CheckBool("a place", IsMaintenanceErrand("at:1:-705,-2045,66"), false);
}

void AnIdleRosterOpensTheRun()
{
    Check("nothing at all", DungeonRunMaintenanceHold("", 0, 0, BOUND),
          MaintenanceHold::Open);
    Check("a trainer errand is not ours",
          DungeonRunMaintenanceHold("trainer", 0, 0, BOUND), MaintenanceHold::Open);
    // A long-idle campaign has no hold clock. Reporting Overdue here would
    // claim a fault on a run that was free to start.
    Check("idle for hours", DungeonRunMaintenanceHold("", 0, 10 * BOUND, BOUND),
          MaintenanceHold::Open);
}

void AWalkIsNotInterrupted()
{
    Check("walking to the repairer",
          DungeonRunMaintenanceHold("repair", 0, 60, BOUND), MaintenanceHold::Walking);
    Check("walking to the vendor",
          DungeonRunMaintenanceHold("vendor", 0, 60, BOUND), MaintenanceHold::Walking);
}

void AQueueThatHasNotBeenAnsweredIsNotInterrupted()
{
    // THE WINDOW THIS EXISTS FOR. The travel drive releases the aim the moment
    // they arrive, so between arriving and the rows being answered the aim
    // column is empty and only the queue says a trip is happening. A hold that
    // watched the aim alone would start a run in exactly those seconds, which
    // is the worst moment to walk them away.
    Check("arrived, rows outstanding",
          DungeonRunMaintenanceHold("", 3, 60, BOUND), MaintenanceHold::Transacting);
    Check("one row is enough",
          DungeonRunMaintenanceHold("", 1, 60, BOUND), MaintenanceHold::Transacting);
}

void WalkingIsReportedAheadOfTransacting()
{
    // Both are routinely true: the first member arrives and gets a row while
    // the last is still on the road. The answers differ only in what the log
    // says, and a journey that may be going wrong is the one an operator can
    // act on.
    Check("both at once", DungeonRunMaintenanceHold("repair", 5, 60, BOUND),
          MaintenanceHold::Walking);
}

void TheWaitAlwaysEnds()
{
    // The property that matters most. The aim is written by a process outside
    // the worldserver, so if that process dies mid-errand the column can hold a
    // role nothing will ever clear. An unbounded hold would stop a hundred-run
    // campaign with nothing in any log to say why.
    Check("a walk that never finishes",
          DungeonRunMaintenanceHold("repair", 0, BOUND + 1, BOUND),
          MaintenanceHold::Overdue);
    Check("a queue nobody answers",
          DungeonRunMaintenanceHold("", 9, BOUND + 1, BOUND),
          MaintenanceHold::Overdue);
    Check("both, past the bound",
          DungeonRunMaintenanceHold("vendor", 9, BOUND + 1, BOUND),
          MaintenanceHold::Overdue);
    // Exactly at the bound is still held: the comparison is strictly greater,
    // so a bound of N gives a full N seconds rather than N minus one poll.
    Check("exactly at the bound",
          DungeonRunMaintenanceHold("repair", 0, BOUND, BOUND),
          MaintenanceHold::Walking);
}

void AZeroBoundNeverHolds()
{
    // Useful to a caller that wants the old behaviour back without editing
    // this file, and it makes the policy explicit rather than leaving it to an
    // accident of the comparison.
    Check("zero bound, walking", DungeonRunMaintenanceHold("repair", 0, 1, 0),
          MaintenanceHold::Overdue);
    // But an idle roster is still Open rather than Overdue: there is nothing
    // to be overdue about.
    Check("zero bound, idle", DungeonRunMaintenanceHold("", 0, 1, 0),
          MaintenanceHold::Open);
}

} // namespace

int main()
{
    TheThreeEconomyErrandsAreRecognised();
    NothingElseIsOneOfThem();
    AnIdleRosterOpensTheRun();
    AWalkIsNotInterrupted();
    AQueueThatHasNotBeenAnsweredIsNotInterrupted();
    WalkingIsReportedAheadOfTransacting();
    TheWaitAlwaysEnds();
    AZeroBoundNeverHolds();
    if (!failures)
        std::printf("a run waits for the errand it would trample, and the wait ends\n");
    return failures ? 1 : 0;
}
