/*
 * The shared progress ratchet, especially the distinction between no reading
 * and a real zero distance. Compiled without AzerothCore so the travel rule
 * remains testable as a pure decision.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::Ratchet;
using OverseerDecisions::RatchetLimits;
using OverseerDecisions::RatchetProgressed;
using OverseerDecisions::RatchetReading;
using OverseerDecisions::RatchetState;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void AZeroDistanceIsAReadingAfterTheFirstPoll()
{
    RatchetLimits const limits{RatchetReading::DistanceToTarget, 0.f, 60};
    RatchetState state;

    auto const first = Ratchet(state, 0.f, 100, limits);
    Check("first zero distance counts as the initial reading", first.progressed, true);
    Check("first zero distance is remembered as seen", state.seen, true);

    auto const second = Ratchet(state, 0.f, 101, limits);
    Check("repeated zero distance is not progress", second.progressed, false);
    Check("repeated zero distance does not reset the clock", state.since == 100, true);
    Check("repeated zero distance eventually stalls", Ratchet(state, 0.f, 161, limits).stalled,
          true);
}

void ADistanceReadingStillUsesTheMargin()
{
    RatchetLimits const limits{RatchetReading::DistanceToTarget, 1.f, 60};
    RatchetState state;

    Ratchet(state, 20.f, 100, limits);
    auto const unchanged = Ratchet(state, 19.5f, 101, limits);
    Check("distance inside the margin is not progress", unchanged.progressed, false);
    auto const nearer = Ratchet(state, 18.5f, 102, limits);
    Check("distance beyond the margin is progress", nearer.progressed, true);
}

void OtherReadingsKeepZeroAsARealReading()
{
    RatchetLimits const count{RatchetReading::CountAchieved, 0.f, 60};
    RatchetState countState;
    Check("count zero is not progress", Ratchet(countState, 0.f, 100, count).progressed,
          false);

    RatchetLimits const movement{RatchetReading::DistanceFromLastMark, 0.f, 60};
    RatchetState movementState;
    Check("movement zero is not progress",
          Ratchet(movementState, 0.f, 100, movement).progressed, false);
}

void ThePureComparisonKeepsItsExistingThreeArgumentMeaning()
{
    RatchetLimits const limits{RatchetReading::DistanceToTarget, 0.f, 60};
    Check("three argument comparison assumes an existing mark",
          RatchetProgressed(0.f, 0.f, limits), false);
}

}  // namespace

int main()
{
    AZeroDistanceIsAReadingAfterTheFirstPoll();
    ADistanceReadingStillUsesTheMargin();
    OtherReadingsKeepZeroAsARealReading();
    ThePureComparisonKeepsItsExistingThreeArgumentMeaning();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the ratchet distinguishes zero from unseen\n");
    return EXIT_SUCCESS;
}
