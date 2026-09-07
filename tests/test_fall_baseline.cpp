/*
 * The fall baseline, and measuring a fall instead of asking about one.
 *
 * WHERE THIS GOT TO. #266 deployed the invariant that a character which is not
 * falling is standing somewhere, and a character that is standing somewhere
 * owes nothing for having got there. Phantom fall deaths continued. #283 added
 * an instrument to say why, and it answered on the first sample: the
 * stand-down mask was 16, FALL_GUARD_FALLING, on every phantom death, with an
 * age of 0 or 1 second. The guard was reached every poll and declined every
 * poll, on characters at full health whose measured descent was 1.63 and 1.88
 * yards. The flag said falling while the character stood still.
 *
 * AND IT IS RE-SET, NOT MERELY NEVER CLEARED. Player::TeleportTo reduces the
 * movement flags to MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE, which drops
 * FALLING, so every graveyard revival clears it. The recovery drive, which
 * cannot run at all while IsFalling is true, was observed running twice
 * between two masked deaths. Clear then, set again by the next death. Clearing
 * the flag once would fix nothing.
 *
 * SO THE GUARD MEASURES. Two positions a second apart cannot be stuck. The
 * core's gravity is 19.29110527, so free fall covers 9.65 yards in its first
 * second; a character on its feet is bounded by 7 yards per second of run
 * speed times the sine of the steepest walkable slope, under 5.4. The limit
 * sits between them.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FallBaselineHandedOver;
using OverseerDecisions::FallBaselineLimits;
using OverseerDecisions::FallBaselineMayInspect;
using OverseerDecisions::FallBaselineState;
using OverseerDecisions::FallBaselineStep;
using OverseerDecisions::FallBaselineVerdict;

namespace
{

int failures = 0;

// What the adapter passes. Seven yards per second, between a walk and a fall.
constexpr FallBaselineLimits LIMITS{7.0f};

// The core's fall arithmetic, from Player.cpp:14173-14175, so this file can
// price a drop without including anything.
constexpr float FALL_DMG_EQU_SLOPE = 0.018f;
constexpr float FALL_DMG_EQU_INTERCEPT = -0.2426f;
constexpr float MIN_FALL_DMG_DIST = 13.48f;

// Movement/Spline/MovementUtil.cpp:24.
constexpr float GRAVITY = 19.29110527f;

float FractionOfMaxHealth(float zDiff)
{
    if (zDiff < MIN_FALL_DMG_DIST)
        return 0.f;
    float const perc = FALL_DMG_EQU_SLOPE * zDiff + FALL_DMG_EQU_INTERCEPT;
    return perc > 0.f ? perc : 0.f;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckZ(char const* what, float got, float want)
{
    float const slack = got - want;
    if (slack > -0.01f && slack < 0.01f)
        return;
    std::printf("FAIL %s: got %.2f, wanted %.2f\n", what, got, want);
    ++failures;
}

// One poll of an ordinary living character on the ground.
FallBaselineVerdict Poll(FallBaselineState& state, float z, time_t now)
{
    return FallBaselineStep(state, true, false, z, now, LIMITS);
}

// THE ROW THAT MOTIVATED THE CHANGE. 01:40:12, Grug, full health 1221/1221,
// measured descent 1.63 yards, stand-down mask 16, age 1 second. Before #291
// the flag alone declined the guard and the baseline was never walked down.
// The flag is not an input any more, so this rebases.
void AStuckFallingFlagNoLongerDeclinesTheGuard()
{
    FallBaselineState state;
    Poll(state, 388.6f, 1000);                     // a first look, sets the reference
    FallBaselineVerdict const v = Poll(state, 387.0f, 1001);   // 1.63 yards lower

    Check("the guard runs even though the flag says falling", v.rebase, true);
    CheckZ("and hands over the character's own feet", v.z, 387.0f);
    Check("1.63 yards in a second is not a fall",
          1.63f <= LIMITS.fallingYardsPerSecond, true);
}

// The 01:40:09 row, the same shape on the other character: 1.88 yards.
void TheOtherMeasuredRowIsTheSame()
{
    FallBaselineState state;
    Poll(state, 398.4f, 2000);
    FallBaselineVerdict const v = Poll(state, 396.5f, 2001);
    Check("1.88 yards in a second is not a fall either", v.rebase, true);
    CheckZ("feet again", v.z, 396.5f);
}

// A REAL FALL IS STILL LEFT ALONE, and now for a reason that cannot be stuck.
void AGenuineFallIsStillCharged()
{
    FallBaselineState state;
    Poll(state, 150.f, 1000);
    CheckZ("the baseline is the lip it walks off", state.z, 150.f);

    // Four seconds of free fall, sampled once a second. Every one of these is
    // losing height far faster than a walk can.
    float const during[] = {130.f, 105.f, 75.f, 40.f};
    time_t t = 1000;
    for (float z : during)
    {
        ++t;
        FallBaselineVerdict const v = Poll(state, z, t);
        Check("nothing is handed over mid-fall", v.rebase, false);
    }
    CheckZ("the height the fall began from is untouched", state.z, 150.f);
    Check("so the core still prices it as a real fall",
          FractionOfMaxHealth(150.f - 40.f) > 0.f, true);

    // The poll after the landing resumes, by which time HandleFall has already
    // charged for the drop it was owed.
    FallBaselineVerdict const after = Poll(state, 40.f, t + 1);
    Check("and the poll after the landing resumes", after.rebase, true);
    CheckZ("at the ground it landed on", after.z, 40.f);
}

// THE LIMIT SITS BETWEEN THE TWO THINGS IT HAS TO SEPARATE, computed rather
// than asserted.
void TheLimitSeparatesAWalkFromAFall()
{
    float const freeFallFirstSecond = 0.5f * GRAVITY * 1.f * 1.f;
    Check("free fall covers more than the limit in its first second",
          freeFallFirstSecond > LIMITS.fallingYardsPerSecond, true);
    Check("and that is 9.65 yards",
          freeFallFirstSecond > 9.6f && freeFallFirstSecond < 9.7f, true);

    // Run speed 7 yards per second on the steepest slope the navmesh allows,
    // taken here as 50 degrees; sin(50) is a little under 0.766.
    float const steepestWalk = 7.0f * 0.766f;
    Check("the steepest walk is under the limit",
          steepestWalk < LIMITS.fallingYardsPerSecond, true);
    Check("and that is about 5.4 yards per second",
          steepestWalk > 5.3f && steepestWalk < 5.4f, true);
}

// Walking downhill for a long way is exactly the case that used to go stale,
// and it is the one a rate test has to get right rather than a flag.
void ALongWalkDownhillKeepsTheBaselineUnderTheFeet()
{
    FallBaselineState state;
    float z = 500.f;
    time_t t = 5000;
    Poll(state, z, t);
    float handed = z;
    for (int i = 0; i < 120; ++i)      // two minutes at 3 yards a second
    {
        z -= 3.f;
        ++t;
        FallBaselineVerdict const v = Poll(state, z, t);
        if (!v.rebase)
        {
            std::printf("FAIL a 3 yard per second walk was read as a fall at poll %d\n", i);
            ++failures;
            return;
        }
        handed = v.z;
    }
    CheckZ("the baseline followed it all the way down", handed, 140.f);
    Check("so the landing is charged nothing",
          FractionOfMaxHealth(handed - 140.f) == 0.f, true);
}

// THE CORE'S OWN GATE. When HandleFall will not charge, this does not write.
void TheAuraGateStandsItDown()
{
    FallBaselineState state;
    Poll(state, 200.f, 7000);
    FallBaselineVerdict const v =
        FallBaselineStep(state, true, true, 150.f, 7001, LIMITS);
    Check("an aura the core itself checks stands the guard down", v.rebase, false);
    CheckZ("and nothing it was holding is disturbed", state.z, 200.f);
}

// The six states this module has no opinion about, with the two untrustworthy
// flags deliberately absent from the list.
void TheStatesWithNoOpinionAreTheSixThatAreNotFlags()
{
    Check("an ordinary living character", FallBaselineMayInspect(true, false, false, false, false, false), true);
    Check("dead", FallBaselineMayInspect(false, false, false, false, false, false), false);
    Check("teleporting", FallBaselineMayInspect(true, true, false, false, false, false), false);
    Check("on a taxi", FallBaselineMayInspect(true, false, true, false, false, false), false);
    Check("in water", FallBaselineMayInspect(true, false, false, true, false, false), false);
    Check("on a transport", FallBaselineMayInspect(true, false, false, false, true, false), false);
    Check("in a vehicle", FallBaselineMayInspect(true, false, false, false, false, true), false);
}

// A GAP MUST NOT BE MEASURED ACROSS. A character that spent ten seconds on a
// boat has not fallen the difference, and reading it as one would leave the
// baseline stale for exactly the reason this whole change exists.
void ARateIsNeverMeasuredAcrossAGapItDidNotWatch()
{
    FallBaselineState state;
    Poll(state, 300.f, 9000);
    Check("the reference is set", state.seen, true);

    FallBaselineVerdict const away =
        FallBaselineStep(state, false, false, 300.f, 9001, LIMITS);
    Check("a state with no opinion hands nothing over", away.rebase, false);
    Check("and forgets where the character was", state.seen, false);

    // Back, 200 yards lower, which across one second would read as a fall.
    FallBaselineVerdict const back = Poll(state, 100.f, 9002);
    Check("the poll after the gap rebases rather than inventing a fall",
          back.rebase, true);
    CheckZ("at the feet", back.z, 100.f);
}

// The very first poll has nothing to measure against and must still act, or a
// character would never get a baseline until its second poll.
void TheFirstPollHasNoReferenceAndStillActs()
{
    FallBaselineState fresh;
    Check("nothing is remembered yet", fresh.seen, false);
    FallBaselineVerdict const v = Poll(fresh, 250.f, 11000);
    Check("and it still hands the feet over", v.rebase, true);
    CheckZ("at the feet", v.z, 250.f);
}

// The server's own height goes back whichever way it has moved: a rule that
// only ever lowered would leave a character that climbed unable to be charged.
//
// The descent here is a WALK, three yards in a second. Writing it as a twenty
// yard drop is what the first draft of this test did, carried over from the
// version of the rule that had no rate in it, and the rule correctly called
// that a fall and declined. The expectation was wrong, not the rule.
void TheServersOwnHeightGoesBackEvenWhenItRises()
{
    FallBaselineState state;
    FallBaselineHandedOver(state, 100.f, 1000);
    Poll(state, 100.f, 1001);
    CheckZ("walking down hands down", Poll(state, 97.f, 1002).z, 97.f);
    FallBaselineVerdict const up = Poll(state, 130.f, 1003);
    Check("climbing hands over too", up.rebase, true);
    CheckZ("the height it climbed to", up.z, 130.f);
    Check("so a real drop from up there is still chargeable",
          FractionOfMaxHealth(up.z - 70.f) > 0.f, true);
}

// Recording a lift is bookkeeping about what this module did and must not
// become a gate again, which is the lesson of the 20:10:13 pair.
void RecordingALiftDoesNotChangeTheVerdict()
{
    FallBaselineState lifted, plain;
    FallBaselineHandedOver(lifted, 158.8f, 1000);
    Poll(lifted, 158.8f, 1001);
    Poll(plain, 158.8f, 1001);

    FallBaselineVerdict const a = Poll(lifted, 157.0f, 1002);
    FallBaselineVerdict const b = Poll(plain, 157.0f, 1002);
    Check("both are guarded", a.rebase && b.rebase, true);
    CheckZ("and both at the same feet", a.z, b.z);
}

}  // namespace

int main()
{
    AStuckFallingFlagNoLongerDeclinesTheGuard();
    TheOtherMeasuredRowIsTheSame();
    AGenuineFallIsStillCharged();
    TheLimitSeparatesAWalkFromAFall();
    ALongWalkDownhillKeepsTheBaselineUnderTheFeet();
    TheAuraGateStandsItDown();
    TheStatesWithNoOpinionAreTheSixThatAreNotFlags();
    ARateIsNeverMeasuredAcrossAGapItDidNotWatch();
    TheFirstPollHasNoReferenceAndStillActs();
    TheServersOwnHeightGoesBackEvenWhenItRises();
    RecordingALiftDoesNotChangeTheVerdict();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the guard measures the fall instead of trusting a flag\n");
    return EXIT_SUCCESS;
}
