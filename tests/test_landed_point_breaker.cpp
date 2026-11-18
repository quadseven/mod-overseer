/*
 * A point aim the leader has reached and nobody cleared (the dev realm,
 * 2026-11-18).
 *
 * The live failure this pins. A town errand's `at:` point stood in a family
 * leader's travel column. On each arrival the release skipped the column write
 * ("a profession errand (skill 171) and errand 'at:...' are outstanding"),
 * remembered a landing, and stood the aim down for 15 minutes; the next poll
 * after that walked the 0 yards again. The aim was reached at 18:56, 20:57 and
 * 21:12 and never cleared, so the leader's vendor trip was refused as a
 * stranger's errand and no trainer walk could be written into the column. The
 * family stood still for over three hours.
 *
 * LandedPointAimStep counts arrivals at one identical point aim and trips once
 * the loop has proved itself over a real span of time, so the caller clears
 * that exact aim. A first arrival, a keyword, a different point or a long gap
 * never trips it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::LANDED_POINT_LIMITS;
using OverseerDecisions::LandedPointAimStep;
using OverseerDecisions::LandedPointLimits;
using OverseerDecisions::LandedPointState;

namespace
{

int failures = 0;

void Expect(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

std::string const kPoint = "at:0:-6673.9,-2174.3,244.2";
int64_t const kStandDown = 15 * 60;  // TRAVEL_LANDED_CEILING_SECONDS

// THE DEFECT ITSELF: the same point reached after each stand-down. The first
// arrival is the bridge's chance to clear it; the one after the stand-down
// proves nobody did.
void AnUnclearedPointIsClearedOnItsSecondArrival()
{
    LandedPointState state;
    Expect("the first arrival is left to its writer",
           LandedPointAimStep(state, kPoint, 1000), false);
    Expect("the arrival after the stand-down clears it",
           LandedPointAimStep(state, kPoint, 1000 + kStandDown), true);
    Expect("the state starts from nothing after a trip",
           LandedPointAimStep(state, kPoint, 1000 + 2 * kStandDown), false);
}

// Two arrivals seconds apart are one arrival read twice, not a loop.
void ArrivalsCloseTogetherDoNotTrip()
{
    LandedPointState state;
    Expect("first", LandedPointAimStep(state, kPoint, 1000), false);
    Expect("second, five seconds later", LandedPointAimStep(state, kPoint, 1005), false);
    Expect("third, ten seconds later", LandedPointAimStep(state, kPoint, 1010), false);
}

// Only a point counts: a keyword or a creature entry names a place that has not
// moved and its own writer ends it.
void OnlyAPointCounts()
{
    for (std::string const& aim : {std::string("vendor"), std::string("banker"), std::string("1407"),
                                  std::string("profession trainer"), std::string()})
    {
        LandedPointState state;
        LandedPointAimStep(state, aim, 1000);
        Expect(("a keyword '" + aim + "' never trips").c_str(),
               LandedPointAimStep(state, aim, 1000 + kStandDown), false);
    }
}

// A different aim in between is a different errand, and the count starts again.
void ADifferentAimRestartsTheCount()
{
    LandedPointState state;
    LandedPointAimStep(state, kPoint, 1000);
    Expect("another point", LandedPointAimStep(state, "at:0:-6600,-2100,244", 1000 + kStandDown),
           false);
    Expect("the first point again counts from one",
           LandedPointAimStep(state, kPoint, 1000 + 2 * kStandDown), false);
}

// A gap longer than the bound is a different visit, not the same loop.
void ALongGapRestartsTheCount()
{
    LandedPointState state;
    LandedPointAimStep(state, kPoint, 1000);
    int64_t const later = 1000 + LANDED_POINT_LIMITS.gapSeconds + 1;
    Expect("after a long gap", LandedPointAimStep(state, kPoint, later), false);
    Expect("and the next stand-down then trips", LandedPointAimStep(state, kPoint, later + kStandDown),
           true);
}

// The limits are the caller's to pass.
void LimitsAreHonoured()
{
    LandedPointLimits tight;
    tight.landings = 3;
    tight.minSeconds = 30;
    tight.gapSeconds = 60;
    LandedPointState state;
    Expect("1 of 3", LandedPointAimStep(state, kPoint, 0, tight), false);
    Expect("2 of 3", LandedPointAimStep(state, kPoint, 15, tight), false);
    Expect("3 of 3 at 30s", LandedPointAimStep(state, kPoint, 30, tight), true);
}

// THE LIVE TIMES: reached 20:57:18 and again 21:12:22 (904 seconds). The first
// is the bridge's chance; the second must clear, not wait a third round.
void TheLiveTimeline()
{
    LandedPointState state;
    int64_t const first = 20 * 3600 + 57 * 60 + 18;
    int64_t const second = 21 * 3600 + 12 * 60 + 22;
    Expect("20:57:18", LandedPointAimStep(state, kPoint, first), false);
    Expect("21:12:22", LandedPointAimStep(state, kPoint, second), true);
}

}  // namespace

int main()
{
    AnUnclearedPointIsClearedOnItsSecondArrival();
    ArrivalsCloseTogetherDoNotTrip();
    OnlyAPointCounts();
    ADifferentAimRestartsTheCount();
    ALongGapRestartsTheCount();
    LimitsAreHonoured();
    TheLiveTimeline();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a point aim reached again and never cleared is cleared, and only then\n");
    return EXIT_SUCCESS;
}
