/*
 * A run that opens reaches the staging point or fails with the leader closer
 * than he started, never 26 times further away (#1153).
 *
 * Measured in the trace on #396: the leader joined the corridor 224 yards out,
 * closed to 26, and then went 38, 154, 335, 423, 674 with the run still open.
 * The watchdog only closes on a leader above the point, and everything else
 * waited out the twelve-minute backstop.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::StagingRetreatLimits;
using OverseerDecisions::StagingRetreatReading;
using OverseerDecisions::StagingRetreatState;
using OverseerDecisions::StagingRetreated;

namespace
{

int failures = 0;

constexpr StagingRetreatLimits LIMITS{60.f, 100.f};

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Walk(StagingRetreatState& state, float yards)
{
    return StagingRetreated(state, StagingRetreatReading{true, yards, false}, LIMITS);
}

// Reads each distance in turn and returns the one the run closes on, or below
// zero when it never closes.
template <std::size_t N>
float ClosesAt(StagingRetreatState& state, float const (&yards)[N])
{
    for (float const reading : yards)
        if (Walk(state, reading))
            return reading;
    return -1.f;
}

void TheMeasuredTraceClosesCloserThanItStarted()
{
    StagingRetreatState state;
    float const trace[] = {224.f, 150.f, 80.f, 26.f, 38.f, 154.f, 335.f, 423.f, 674.f};
    float const closed = ClosesAt(state, trace);
    Check("the trace closes", closed > 0.f);
    Check("and closes with the leader closer than he started", closed < 224.f);
}

void AWalkThatKeepsClosingNeverCloses()
{
    StagingRetreatState state;
    float const walk[] = {224.f, 200.f, 205.f, 150.f, 158.f, 90.f, 26.f, 30.f, 12.f};
    Check("jitter on the way in is not a retreat", ClosesAt(state, walk) < 0.f);
}

void ALeaderWhoNeverGainedIsBoundedPastHisStart()
{
    // Reopened at 26 yards and walked straight out: this is the 26 times case,
    // and with no headway there is no nearer point to fail at.
    StagingRetreatState state;
    float const drift[] = {26.f, 38.f, 90.f, 126.f, 127.f, 674.f};
    Check("closes on the first reading past the bound",
          ClosesAt(state, drift) == 127.f);
}

void AnUnsurveyedDetourOnALongLegIsNotARetreat()
{
    // A leg of thousands of yards with no route keeps its first hundred-yard
    // bend, and a bend after real headway may give back half of that headway.
    StagingRetreatState state;
    float const bend[] = {5000.f, 5090.f, 4000.f, 4400.f};
    Check("half the headway is allowed back", ClosesAt(state, bend) < 0.f);
}

void GoingForwardOnASurveyedRouteMovesTheStart()
{
    // The Zul'Farrak approach: 976 yards out, 1807 at the far end of the ramp.
    StagingRetreatState state;
    Walk(state, 976.f);
    for (float yards = 1000.f; yards < 1807.f; yards += 200.f)
        Check("a route walking away is not a retreat",
              !StagingRetreated(state, StagingRetreatReading{true, yards, true}, LIMITS));
    Check("the far end of the route is the new start", state.startYards >= 1800.f);
    Check("and wandering from there is bounded by it", Walk(state, 1950.f));
}

void AReadingThatIsNotAboutWalkingRebases()
{
    // Combat, a flight, a death, a town stop: where he comes back is the new
    // mark, and a pull during a fight is not the leader walking away.
    StagingRetreatState state;
    Walk(state, 224.f);
    Walk(state, 26.f);
    Check("a held poll closes nothing",
          !StagingRetreated(state, StagingRetreatReading{false, 0.f, false}, LIMITS));
    Check("the first reading after it is the new best", !Walk(state, 300.f));
    Check("and the start moves out to meet it", state.startYards >= 300.f);
    Check("the retreat is measured from there", Walk(state, 401.f));
}

void ANegativeReadingIsNoReading()
{
    StagingRetreatState state;
    Walk(state, 224.f);
    Walk(state, 26.f);
    Check("no reading closes nothing", !Walk(state, -1.f));
}

}  // namespace

int main()
{
    TheMeasuredTraceClosesCloserThanItStarted();
    AWalkThatKeepsClosingNeverCloses();
    ALeaderWhoNeverGainedIsBoundedPastHisStart();
    AnUnsurveyedDetourOnALongLegIsNotARetreat();
    GoingForwardOnASurveyedRouteMovesTheStart();
    AReadingThatIsNotAboutWalkingRebases();
    ANegativeReadingIsNoReading();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("staging retreat: ok\n");
    return EXIT_SUCCESS;
}
