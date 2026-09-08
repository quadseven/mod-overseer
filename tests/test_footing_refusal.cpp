/*
 * A refused bearing keeps the ground it proved, and a refusal has a bound
 * that a rewritten target cannot re-arm.
 *
 * mod-overseer#312. The travel drive walks a bearing in strides and refused
 * the whole bearing on the first stride that failed, so a probe that held for
 * eleven strides answered exactly as one that broke on its first. Measured off
 * the shipped terrain at the coordinates of four consecutive refusals on
 * 2026-09-08: the four bearings broke at strides 5, 7, 12 and 13 of 15, having
 * proved 16, 24, 44 and 48 yards of ground that was then thrown away. Every
 * failing stride was a RISE of 8.8 to 16.9 yards - not one was a drop, and not
 * one was ground the probe could not find.
 *
 * The refusal was also announced once per ERRAND and said the errand's twenty
 * minute stall clock bounded it. Both halves are scoped to a target that is
 * rewritten from outside this module, and for a catch-up walk is rewritten
 * every poll with the leader's live position: one character was refused on six
 * consecutive polls across six minutes against five different targets, warning
 * six times and restarting the clock five times.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FootingHeld;
using OverseerDecisions::FootingRefusalState;
using OverseerDecisions::FootingRefusalVerdict;
using OverseerDecisions::FootingRefused;
using OverseerDecisions::ProvenStepIsWorthTaking;

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

void CheckCount(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

// The adapter's own numbers: TRAVEL_GROUND_SAMPLE_YARDS is four, so
// TRAVEL_STEP_MIN_YARDS is two strides, and TRAVEL_GROUND_REFUSAL_LIMIT is
// eight polls inside TRAVEL_GROUND_REFUSAL_RADIUS yards.
constexpr float MIN_STEP = 8.f;
constexpr float RADIUS = 10.f;
constexpr unsigned LIMIT = 8;
constexpr uint32_t KALIMDOR = 1;

// ------------------------------------------------------------------------
// The ground a refused bearing proved.

void TheFourMeasuredRefusalsAreAllWorthWalking()
{
    // Stride 5 of 15 on a 60 yard probe: four strides held, sixteen yards.
    Check("straight bearing, 16 proved yards",
          ProvenStepIsWorthTaking(16.f, MIN_STEP), true);
    Check("second bearing, 24 proved yards",
          ProvenStepIsWorthTaking(24.f, MIN_STEP), true);
    Check("third bearing, 44 proved yards",
          ProvenStepIsWorthTaking(44.f, MIN_STEP), true);
    Check("fourth bearing, 48 proved yards",
          ProvenStepIsWorthTaking(48.f, MIN_STEP), true);
}

void AProbeThatBrokeAtItsFirstStrideStillGetsNothing()
{
    // The other character measured that night stood at the foot of a 74 yard
    // wall: its straight bearing broke on stride ONE, so nothing was proved
    // and nothing may be walked. A cliff is still a cliff.
    Check("nothing proved", ProvenStepIsWorthTaking(0.f, MIN_STEP), false);
    Check("one stride proved", ProvenStepIsWorthTaking(4.f, MIN_STEP), false);
    Check("just under two strides",
          ProvenStepIsWorthTaking(7.99f, MIN_STEP), false);
    Check("exactly two strides", ProvenStepIsWorthTaking(8.f, MIN_STEP), true);
}

void AFullReachIsUnchanged()
{
    // A bearing that held all the way still walks all the way: this rule can
    // only ever shorten a step, never lengthen one.
    Check("the whole sixty yards", ProvenStepIsWorthTaking(60.f, MIN_STEP), true);
}

void ANonsenseFloorIsRefusedRatherThanFolded()
{
    // Reading a negative bound charitably would LOOSEN the rule, and every
    // mistake this predicate can make has to be the tight one.
    Check("negative floor, ample ground",
          ProvenStepIsWorthTaking(60.f, -1.f), false);
    Check("a floor of zero still admits a zero step",
          ProvenStepIsWorthTaking(0.f, 0.f), true);
}

// ------------------------------------------------------------------------
// The bound.

void TheFirstRefusalOfAnEpisodeIsTheOneThatSpeaks()
{
    FootingRefusalState state;
    FootingRefusalVerdict const first =
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    Check("the first poll says it", first.sayIt, true);
    Check("the first poll does not give up", first.giveUp, false);
    CheckCount("the first poll is poll one", first.consecutive, 1);

    FootingRefusalVerdict const second =
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    Check("the second poll is quiet", second.sayIt, false);
    CheckCount("the second poll is poll two", second.consecutive, 2);
}

void ARewrittenTargetCannotReArmTheBound()
{
    // This is the whole defect. Six polls, five different targets, one
    // character that never moved: under the old once-per-errand flag every one
    // of these was a fresh errand, so it warned six times and its twenty
    // minute clock restarted five times. Here the target is not an input at
    // all, so the count runs regardless.
    FootingRefusalState state;
    unsigned said = 0;
    for (unsigned poll = 0; poll < 6; ++poll)
    {
        FootingRefusalVerdict const v =
            FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
        if (v.sayIt)
            ++said;
    }
    CheckCount("six polls, one warning", said, 1);
    CheckCount("six polls counted", state.consecutive, 6);
}

void EightPollsWithoutMovingGivesTheErrandUp()
{
    FootingRefusalState state;
    for (unsigned poll = 1; poll < LIMIT; ++poll)
    {
        FootingRefusalVerdict const v =
            FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
        Check("under the limit does not give up", v.giveUp, false);
    }
    FootingRefusalVerdict const last =
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    Check("the eighth poll gives up", last.giveUp, true);
    CheckCount("and says how many", last.consecutive, LIMIT);
}

void MovingRestartsTheEpisode()
{
    // A character that covered 2,335 yards while being refused is not a
    // character stuck at one spot, whatever moved it. Its next refusal is
    // about different ground and starts a new episode.
    FootingRefusalState state;
    for (unsigned poll = 0; poll < LIMIT - 1; ++poll)
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    CheckCount("seven polls at the first spot", state.consecutive, LIMIT - 1);

    FootingRefusalVerdict const moved =
        FootingRefused(state, KALIMDOR, 514.6f, 760.0f, RADIUS, LIMIT);
    Check("thirty yards on is a new episode", moved.sayIt, true);
    Check("and does not give up", moved.giveUp, false);
    CheckCount("counting from one again", moved.consecutive, 1);
}

void JitterInsideTheRadiusIsNotMoving()
{
    FootingRefusalState state;
    FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    FootingRefusalVerdict const jittered =
        FootingRefused(state, KALIMDOR, 517.6f, 731.1f, RADIUS, LIMIT);
    Check("three yards of jitter says nothing new", jittered.sayIt, false);
    CheckCount("and keeps counting", jittered.consecutive, 2);
}

void AnotherMapIsAlwaysANewEpisode()
{
    FootingRefusalState state;
    FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    FootingRefusalVerdict const crossed =
        FootingRefused(state, 0, 514.6f, 729.1f, RADIUS, LIMIT);
    Check("the same coordinates on another map are another place",
          crossed.sayIt, true);
    CheckCount("counting from one", crossed.consecutive, 1);
}

void AStepEndsTheEpisode()
{
    FootingRefusalState state;
    for (unsigned poll = 0; poll < LIMIT - 1; ++poll)
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    FootingHeld(state);
    CheckCount("a step clears the count", state.consecutive, 0);
    Check("and the anchor with it", state.anchored, false);

    FootingRefusalVerdict const after =
        FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, LIMIT);
    Check("the next refusal speaks again", after.sayIt, true);
    Check("and is nowhere near the limit", after.giveUp, false);
}

void AZeroLimitNeverGivesUp()
{
    // A caller that wants the counting and not the release has to be able to
    // ask for it, the same way a reach of zero turns FloorUnderfoot off.
    FootingRefusalState state;
    for (unsigned poll = 0; poll < 40; ++poll)
    {
        FootingRefusalVerdict const v =
            FootingRefused(state, KALIMDOR, 514.6f, 729.1f, RADIUS, 0);
        Check("a zero limit never gives up", v.giveUp, false);
    }
    CheckCount("but still counts", state.consecutive, 40);
}

void AZeroRadiusOnlyReAnchorsOnAMapChange()
{
    FootingRefusalState state;
    FootingRefused(state, KALIMDOR, 514.6f, 729.1f, 0.f, LIMIT);
    FootingRefusalVerdict const far =
        FootingRefused(state, KALIMDOR, 9000.f, -9000.f, 0.f, LIMIT);
    Check("a zero radius ignores distance", far.sayIt, false);
    CheckCount("and keeps counting", far.consecutive, 2);
}

}  // namespace

int main()
{
    TheFourMeasuredRefusalsAreAllWorthWalking();
    AProbeThatBrokeAtItsFirstStrideStillGetsNothing();
    AFullReachIsUnchanged();
    ANonsenseFloorIsRefusedRatherThanFolded();

    TheFirstRefusalOfAnEpisodeIsTheOneThatSpeaks();
    ARewrittenTargetCannotReArmTheBound();
    EightPollsWithoutMovingGivesTheErrandUp();
    MovingRestartsTheEpisode();
    JitterInsideTheRadiusIsNotMoving();
    AnotherMapIsAlwaysANewEpisode();
    AStepEndsTheEpisode();
    AZeroLimitNeverGivesUp();
    AZeroRadiusOnlyReAnchorsOnAMapChange();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf(
        "ok: a refused bearing keeps the ground it proved, and the bound is "
        "anchored to a place\n");
    return EXIT_SUCCESS;
}
