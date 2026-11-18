/*
 * The airborne guard (wow-overseer issue 2937).
 *
 * A family leader ran a straight spline to the trainer it was sent to, 186
 * yards above the ground, with a follower beside it. These tests replay that
 * reading, second by second, through the pure rule the module asks every
 * poll, and the exemptions that keep a jump, a fall, a taxi, a boat, a swimmer
 * and a flyer out of it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::AirborneAction;
using OverseerDecisions::AirborneFacts;
using OverseerDecisions::AirborneGuardLimits;
using OverseerDecisions::AirborneGuardState;
using OverseerDecisions::AirborneGuardStep;
using OverseerDecisions::AirborneVerdict;

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

constexpr AirborneGuardLimits LIMITS{10.f, 5, 30};

AirborneFacts Standing(float z, float groundZ)
{
    AirborneFacts f;
    f.alive = true;
    f.groundValid = true;
    f.z = z;
    f.groundZ = groundZ;
    return f;
}

// The measured line: z 324.8 over ground 138.0 at 23:24:49.
void TheMeasuredLineIsStoppedOnceAfterTheBound()
{
    AirborneGuardState state;
    AirborneFacts const air = Standing(324.8f, 138.0f);
    for (time_t t = 1000; t < 1005; ++t)
        Check(AirborneGuardStep(state, air, LIMITS, t).action == AirborneAction::Watch,
              "airborne inside the bound is watched, not stopped");
    AirborneVerdict const stop = AirborneGuardStep(state, air, LIMITS, 1005);
    Check(stop.action == AirborneAction::Stop, "five seconds airborne is stopped");
    Check(stop.gap > 186.f && stop.gap < 187.f, "the verdict carries the measured gap");
    Check(stop.seconds == 5, "the verdict carries how long it has been airborne");
    Check(AirborneGuardStep(state, air, LIMITS, 1006).action == AirborneAction::Watch,
          "a stop is said once, not every poll");
}

void AStopThatDidNotHoldIsRepeated()
{
    AirborneGuardState state;
    AirborneFacts const air = Standing(304.0f, 168.7f);
    for (time_t t = 1000; t <= 1005; ++t)
        AirborneGuardStep(state, air, LIMITS, t);
    Check(AirborneGuardStep(state, air, LIMITS, 1034).action == AirborneAction::Watch,
          "inside the repeat window it stays quiet");
    Check(AirborneGuardStep(state, air, LIMITS, 1035).action == AirborneAction::Stop,
          "still airborne after the repeat window, it is stopped again");
}

void AJumpAndAFallAreNotAirborne()
{
    AirborneGuardState state;
    // A jump: three yards for a second.
    Check(AirborneGuardStep(state, Standing(103.f, 100.f), LIMITS, 100).action ==
              AirborneAction::Nothing,
          "a jump is not airborne");
    // A fall off a 40 yard cliff: high for two readings, then on the ground.
    AirborneGuardStep(state, Standing(140.f, 100.f), LIMITS, 101);
    AirborneGuardStep(state, Standing(120.f, 100.f), LIMITS, 102);
    Check(AirborneGuardStep(state, Standing(100.f, 100.f), LIMITS, 103).action ==
              AirborneAction::Nothing,
          "a fall that lands is never stopped");
    Check(state.since == 0, "landing ends the episode");
    // And a later lift starts its own clock rather than inheriting the fall's.
    for (time_t t = 110; t < 114; ++t)
        AirborneGuardStep(state, Standing(200.f, 100.f), LIMITS, t);
    Check(AirborneGuardStep(state, Standing(200.f, 100.f), LIMITS, 114).action ==
              AirborneAction::Watch,
          "a new episode is timed from its own first reading");
}

void ExemptionsResetTheEpisode()
{
    struct Case
    {
        char const* what;
        void (*apply)(AirborneFacts&);
    } const cases[] = {
        {"a corpse", [](AirborneFacts& f) { f.alive = false; }},
        {"a character between worlds", [](AirborneFacts& f) { f.betweenWorlds = true; }},
        {"a taxi", [](AirborneFacts& f) { f.onTaxi = true; }},
        {"a transport", [](AirborneFacts& f) { f.onTransport = true; }},
        {"a vehicle", [](AirborneFacts& f) { f.inVehicle = true; }},
        {"a swimmer", [](AirborneFacts& f) { f.inWater = true; }},
        {"a flight, hover or feather fall aura", [](AirborneFacts& f) { f.mayFly = true; }},
        {"no floor measured", [](AirborneFacts& f) { f.groundValid = false; }},
    };
    for (Case const& c : cases)
    {
        AirborneGuardState state;
        AirborneFacts f = Standing(324.8f, 138.0f);
        c.apply(f);
        bool stopped = false;
        for (time_t t = 100; t < 160; ++t)
            stopped = stopped || AirborneGuardStep(state, f, LIMITS, t).action != AirborneAction::Nothing;
        Check(!stopped, c.what);
    }
    // An exempt reading in the middle of an episode resets it.
    AirborneGuardState state;
    AirborneFacts air = Standing(324.8f, 138.0f);
    for (time_t t = 100; t < 104; ++t)
        AirborneGuardStep(state, air, LIMITS, t);
    AirborneFacts taxi = air;
    taxi.onTaxi = true;
    AirborneGuardStep(state, taxi, LIMITS, 104);
    Check(AirborneGuardStep(state, air, LIMITS, 105).action == AirborneAction::Watch,
          "a taxi reading restarts the clock");
}

void UnderTheGroundIsNotThisGuards()
{
    // The second measured line ran 49 yards under the hills: the floor above
    // is not a floor below, and the below-the-world drive owns that reading.
    AirborneGuardState state;
    for (time_t t = 100; t < 130; ++t)
        Check(AirborneGuardStep(state, Standing(151.7f, 200.5f), LIMITS, t).action ==
                  AirborneAction::Nothing,
              "a character under the ground is not airborne");
}

void TheBoundIsExclusive()
{
    AirborneGuardState state;
    for (time_t t = 100; t < 130; ++t)
        Check(AirborneGuardStep(state, Standing(110.f, 100.f), LIMITS, t).action ==
                  AirborneAction::Nothing,
              "exactly gapYards up is still standing on something");
}
}  // namespace

int main()
{
    TheMeasuredLineIsStoppedOnceAfterTheBound();
    AStopThatDidNotHoldIsRepeated();
    AJumpAndAFallAreNotAirborne();
    ExemptionsResetTheEpisode();
    UnderTheGroundIsNotThisGuards();
    TheBoundIsExclusive();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("airborne guard: all checks passed\n");
    return EXIT_SUCCESS;
}
