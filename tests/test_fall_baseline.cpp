/*
 * The fall baseline, and putting it back under a character's feet.
 *
 * THE LIVE FAILURE THIS PINS. The core charges m_lastFallZ - landingZ on the
 * next landing (Player::HandleFall). A teleport sets that baseline to its
 * destination (Player.cpp:1532, and again on the client's ack at
 * MovementHandler.cpp:321), and the only thing that walks it back down again
 * is UpdateFallInformationIfNeed, which runs on a client movement packet and
 * on nothing else. This roster is moved by server-side splines, which send no
 * packets, so a height written once stays written while the character walks
 * away from it - and HandleFall runs BEFORE UpdateFallInformationIfNeed in the
 * same handler, so the stale figure is spent before anything corrects it.
 *
 * FOUR DEATHS ON 2026-09-06, all at full health, out of combat, standing:
 *
 *   19:15:22  Bork  died z 65.7   lifted to 157.3   91.6 yards = 1.41x max hp
 *   20:10:13  Ugga  died z 93.37  NEVER LIFTED
 *   20:10:13  Grog  died z 93.47  lifted to 158.8, 52 hp short of killing him
 *   20:10:40  Grug  died z 91.67  lifted to 149.1, 297 hp short
 *
 * The last three each need a baseline near z 162.4, and the party's travel aim
 * was z 162.425. So the lift is one way to leave a stale height and not the
 * only one, and what the four have in common is that the character was
 * STANDING when the core charged it. That is what this rule keys on.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FallBaselineHandedOver;
using OverseerDecisions::FallBaselineState;
using OverseerDecisions::FallBaselineStep;
using OverseerDecisions::FallBaselineVerdict;

namespace
{

int failures = 0;

// The core's fall arithmetic, from Player.cpp:14173-14175, so this file can
// price a drop without including anything. It is here to turn "the baseline
// was wrong by this many yards" into "and this is what it cost".
constexpr float FALL_DMG_EQU_SLOPE = 0.018f;
constexpr float FALL_DMG_EQU_INTERCEPT = -0.2426f;
constexpr float MIN_FALL_DMG_DIST = 13.48f;

// The core's gravity, from Movement/Spline/MovementUtil.cpp:24. Used to show
// that no chargeable fall fits between two one-second polls.
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

// Walk a character down from `fromZ` to `toZ` over `polls` one-second polls,
// standing the whole way, and return the last baseline handed over. This is
// the shape of every one of the four deaths: driven downhill by a spline, never
// falling, so the core never hears a packet and never re-bases.
float WalkDownStanding(FallBaselineState& state, float fromZ, float toZ,
                       int polls, time_t start)
{
    float const step = (fromZ - toZ) / static_cast<float>(polls);
    float standing = fromZ;
    float handed = fromZ;
    for (int i = 1; i <= polls; ++i)
    {
        standing -= step;
        FallBaselineVerdict const v =
            FallBaselineStep(state, true, false, standing, start + i);
        if (!v.rebase)
        {
            std::printf("FAIL poll %d of a standing walk handed nothing over\n", i);
            ++failures;
            return handed;
        }
        handed = v.z;
    }
    return handed;
}

// One case, priced both ways: what the core would have charged with the stale
// height still standing, and what it charges once the guard has walked the
// baseline down to the character's feet.
//
// `leastCost` is the share of the health bar the unguarded drop must account
// for. It is 1.0 - a one-shot from full - for three of the four. For Grog it
// is 0.99, and the missing hit point is real rather than slack: 162.425 down
// to 93.47 is 68.955 yards, which prices at 785.9 of his 787, one short of
// killing him outright. His row says seconds_since_full_health=5, so he was
// last seen at full health five seconds before he died and had taken at least
// a point of damage by the time the fall was charged. Asserting 1.0 there
// would be asserting something the evidence does not carry.
void OneMeasuredDeath(char const* who, float staleZ, float diedZ, float maxHp,
                      int seconds, float leastCost)
{
    float const unguarded = staleZ - diedZ;
    float const cost = FractionOfMaxHealth(unguarded);
    if (cost < leastCost)
    {
        std::printf("FAIL %s: %.2f yards costs %.4f of the bar, wanted >= %.4f\n",
                    who, unguarded, cost, leastCost);
        ++failures;
    }
    (void)maxHp;

    FallBaselineState state;
    FallBaselineHandedOver(state, staleZ, 1000);
    float const handed = WalkDownStanding(state, staleZ, diedZ, seconds, 1000);
    CheckZ(who, handed, diedZ);
    Check("and the landing is charged nothing at all",
          FractionOfMaxHealth(handed - diedZ) == 0.f, true);
}

// All four, including the one that was never lifted. A guard keyed on the lift
// would have saved only Bork; this one keys on standing, so it saves all four.
void TheFourMeasuredDeathsArePricedAtNothing()
{
    OneMeasuredDeath("Bork 19:15:22, lifted to 157.3", 157.3f, 65.7f, 695.f, 208, 1.0f);
    OneMeasuredDeath("Ugga 20:10:13, never lifted", 162.425f, 93.37f, 832.f, 120, 1.0f);
    OneMeasuredDeath("Grog 20:10:13, aim not lift", 162.425f, 93.47f, 787.f, 209, 0.99f);
    OneMeasuredDeath("Grug 20:10:40, aim not lift", 162.425f, 91.67f, 1421.f, 352, 1.0f);

    // Bork's is the one that is exact rather than merely sufficient, and it is
    // also the only one of the four the lift alone accounts for. The other two
    // lifts fall short, which is what stopped this being a fix keyed on lifts.
    Check("the lift height alone one-shot Bork from full health",
          FractionOfMaxHealth(157.3f - 65.7f) >= 1.f, true);
    Check("but the lift height could not have killed Grog",
          FractionOfMaxHealth(158.8f - 93.47f) * 787.f < 787.f, true);
    Check("nor Grug",
          FractionOfMaxHealth(149.1f - 91.67f) * 1421.f < 1421.f, true);
}

// The lesson of the 20:10:13 pair, as a rule rather than as a row: a character
// this module never moved is guarded exactly as one it did. Ugga had no lift
// behind her and died the same death, so `held` must not gate anything.
void ACharacterThisModuleNeverMovedIsGuardedTheSame()
{
    FallBaselineState never;
    Check("nothing has been handed over yet", never.held, false);

    FallBaselineVerdict const v = FallBaselineStep(never, true, false, 93.37f, 500);
    Check("and it is guarded anyway", v.rebase, true);
    CheckZ("at its own feet", v.z, 93.37f);

    // Priced: without this, the aim height would have killed her outright.
    Check("which is what the unguarded aim height would have cost her",
          FractionOfMaxHealth(162.425f - 93.37f) * 832.f >= 832.f, true);
}

// The exemption that keeps the fix honest. A character that really is falling
// keeps the baseline it fell from, so the core still charges for the drop.
void AGenuineFallIsStillCharged()
{
    FallBaselineState state;
    FallBaselineStep(state, true, false, 150.f, 1000);   // standing at the top
    CheckZ("the baseline is the lip it walks off", state.z, 150.f);

    for (int i = 1; i <= 4; ++i)
    {
        FallBaselineVerdict const v = FallBaselineStep(
            state, true, true, 150.f - 20.f * static_cast<float>(i), 1000 + i);
        Check("nothing is handed over mid-fall", v.rebase, false);
    }
    CheckZ("the height the fall began from is untouched", state.z, 150.f);
    Check("so the core still prices it as a real fall",
          FractionOfMaxHealth(150.f - 70.f) > 0.f, true);

    FallBaselineVerdict const after = FallBaselineStep(state, true, false, 70.f, 1005);
    Check("and the poll after the landing resumes", after.rebase, true);
    CheckZ("at the ground it landed on", after.z, 70.f);
}

// THE SAFETY ARGUMENT, ARITHMETIC RATHER THAN ASSERTED. No fall the core would
// charge for can begin and end inside one of the caller's one-second polls, so
// none can be missed while `falling` is the only exemption.
void NoChargeableFallFitsBetweenTwoPolls()
{
    float const inOneSecond = 0.5f * GRAVITY * 1.f * 1.f;
    Check("a body falls under 9.7 yards in its first second",
          inOneSecond < 9.7f, true);
    Check("which the core charges nothing for",
          FractionOfMaxHealth(inOneSecond) == 0.f, true);
    Check("indeed it is short of the charging floor",
          inOneSecond < MIN_FALL_DMG_DIST, true);

    // Falling the 13.48 yards the core starts charging for takes longer than
    // one poll, so at least one poll lands inside any chargeable fall.
    float t = 0.f;
    while (0.5f * GRAVITY * t * t < MIN_FALL_DMG_DIST)
        t += 0.001f;
    Check("and reaching the charging floor takes more than a second",
          t > 1.0f, true);
    Check("just over 1.18 seconds of it", t > 1.17f && t < 1.19f, true);
}

// Every state the terrain drive stands down for stands this down too, and none
// of them disturbs what is being held.
void EveryStoodDownStateHandsNothingOver()
{
    FallBaselineState state;
    FallBaselineHandedOver(state, 150.f, 1000);

    FallBaselineVerdict const v = FallBaselineStep(state, false, false, 60.f, 1010);
    Check("a character this module may not inspect is left alone", v.rebase, false);
    CheckZ("and nothing it was holding is disturbed", state.z, 150.f);
    Check("and it is still held for when that character is inspectable again",
          state.held, true);
}

// The server's own height goes back whichever way it has moved. A rule that
// only ever lowered the baseline would leave a character that climbed unable
// to be charged for a real drop afterwards.
void TheServersOwnHeightGoesBackEvenWhenItRises()
{
    FallBaselineState state;
    FallBaselineHandedOver(state, 100.f, 1000);

    CheckZ("walking down hands down",
           FallBaselineStep(state, true, false, 80.f, 1001).z, 80.f);

    FallBaselineVerdict const up = FallBaselineStep(state, true, false, 130.f, 1002);
    Check("climbing hands over too", up.rebase, true);
    CheckZ("the height it climbed to", up.z, 130.f);
    Check("so a real drop from up there is still chargeable",
          FractionOfMaxHealth(up.z - 70.f) > 0.f, true);
}

// Recording a lift is bookkeeping about what this module did, and changes
// nothing about what the rule decides. That is deliberate: the 20:10:13 pair
// is the reason it must not become a gate again.
void RecordingALiftDoesNotChangeTheVerdict()
{
    FallBaselineState lifted, plain;
    FallBaselineHandedOver(lifted, 158.8f, 1000);

    FallBaselineVerdict const a = FallBaselineStep(lifted, true, false, 93.5f, 1200);
    FallBaselineVerdict const b = FallBaselineStep(plain, true, false, 93.5f, 1200);
    Check("both are guarded", a.rebase && b.rebase, true);
    CheckZ("and both at the same feet", a.z, b.z);

    // And both stand down in the same states, lifted or not.
    Check("falling stands down the lifted one",
          FallBaselineStep(lifted, true, true, 93.5f, 1201).rebase, false);
    Check("falling stands down the other one too",
          FallBaselineStep(plain, true, true, 93.5f, 1201).rebase, false);
}

}  // namespace

int main()
{
    TheFourMeasuredDeathsArePricedAtNothing();
    ACharacterThisModuleNeverMovedIsGuardedTheSame();
    AGenuineFallIsStillCharged();
    NoChargeableFallFitsBetweenTwoPolls();
    EveryStoodDownStateHandsNothingOver();
    TheServersOwnHeightGoesBackEvenWhenItRises();
    RecordingALiftDoesNotChangeTheVerdict();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a standing character owes nothing for having got there\n");
    return EXIT_SUCCESS;
}
