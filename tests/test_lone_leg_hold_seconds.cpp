/*
 * A ground-only lone-leg refusal is held for less time than a measured killer
 * leg (#1609).
 *
 * Seen on the dev realm on 2026-11-17: two members more than six thousand yards
 * behind their family were refused because the straight line crossed 180 and 210
 * unbroken yards of ground at level 63 or more, with no death on the leg. They
 * stood split from the leader for up to the far-catch-up hold's fifteen minutes.
 * The ground refusal is a prediction and the deaths refusal a measurement, so
 * the prediction is asked again sooner, and the hearth, flight and fetch rungs
 * with it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Eq(char const* what, int64_t got, int64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, static_cast<long long>(got),
                static_cast<long long>(want));
    ++failures;
}
}  // namespace

int main()
{
    int64_t const far = 900;
    Eq("deaths keep the far-catch-up ceiling", LoneLegHoldSeconds(LoneLegReason::Deaths, far), far);
    Eq("ground alone is bounded", LoneLegHoldSeconds(LoneLegReason::Ground, far),
       LONE_LEG_GROUND_HOLD_SECONDS);
    Eq("no reason keeps the ceiling", LoneLegHoldSeconds(LoneLegReason::None, far), far);
    // The bound is shorter than the deaths hold, and never longer than a
    // ceiling the caller passes that is shorter still.
    Eq("a ceiling below the bound wins", LoneLegHoldSeconds(LoneLegReason::Ground, 100), 100);
    if (LONE_LEG_GROUND_HOLD_SECONDS >= far)
    {
        std::printf("FAIL the ground bound is not shorter than the far hold\n");
        ++failures;
    }
    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
