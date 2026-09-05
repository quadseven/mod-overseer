/*
 * The vertical gap bounds the step, not the errand.
 *
 * mod-overseer#203 bounded the short-step fallback against the errand's
 * endpoint and ran the bound before the navmesh was asked. The night it
 * merged, four of five family members were held in place on ordinary
 * overland errands 233 to 1,629 yards off, because every long walk over
 * hills ends more than twenty yards from where it starts. This file pins
 * the rule as it should have read: a far aim's height is not this step's
 * height, and only an aim within one step is bounded by the gap a step can
 * bridge.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::StepMayBridgeGap;

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

// The step and gap the adapter uses: TRAVEL_STEP_YARDS and the twenty yards
// #203 chose.
constexpr float STEP = 60.f;
constexpr float GAP = 20.f;

void AFarAimIsAlwaysSteppedToward()
{
    Check("gate ramp 300 yards off and 85 yards up",
          StepMayBridgeGap(300.f, 85.f, STEP, GAP), true);
    Check("1,629 yards off and far below",
          StepMayBridgeGap(1629.f, -140.f, STEP, GAP), true);
    Check("one yard past a step, 21 up",
          StepMayBridgeGap(61.f, 21.f, STEP, GAP), true);
}

void AnAimWithinOneStepIsBoundedByTheGap()
{
    Check("within a step and level", StepMayBridgeGap(30.f, 0.f, STEP, GAP), true);
    Check("within a step, exactly the gap up",
          StepMayBridgeGap(30.f, 20.f, STEP, GAP), true);
    Check("within a step, exactly the gap down",
          StepMayBridgeGap(30.f, -20.f, STEP, GAP), true);
    Check("within a step and a mountain top",
          StepMayBridgeGap(30.f, 21.f, STEP, GAP), false);
    Check("within a step and a cliff foot",
          StepMayBridgeGap(30.f, -21.f, STEP, GAP), false);
    Check("exactly a step away is still one step",
          StepMayBridgeGap(60.f, 25.f, STEP, GAP), false);
}

}  // namespace

int main()
{
    AFarAimIsAlwaysSteppedToward();
    AnAimWithinOneStepIsBoundedByTheGap();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the vertical gap bounds the step, not the errand\n");
    return EXIT_SUCCESS;
}
