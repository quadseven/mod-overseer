/*
 * The fall baseline this module hands the core, and taking it back again.
 *
 * THE LIVE FAILURE THIS PINS. A lift is Player::TeleportTo to the same x and y
 * at a higher z, and the core's near-teleport branch ends by setting the
 * character's fall baseline to the DESTINATION (Player.cpp:1532, and again on
 * the client's teleport ack at MovementHandler.cpp:321). Nothing lowers that
 * baseline while the character walks back down, because the only thing that
 * would is UpdateFallInformationIfNeed and that runs on a client movement
 * packet, which a server-side spline never sends. HandleFall then charges
 * m_lastFallZ - landingZ on the next landing, and it runs BEFORE
 * UpdateFallInformationIfNeed in the same handler.
 *
 * On 2026-09-06 'Bork' was lifted from z 142.2 to z 157.3 at 19:11:53 and died
 * at z 65.7 at 19:15:22 at full health, out of combat, moving 2.3 yards
 * horizontally and 0.6 yards DOWN in its last second. 157.3 - 65.7 = 91.6
 * yards, which the core's own equation prices at 1.41 times max health.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The adapter
 * asks the world where the character is and hands the answer to the core; this
 * file pins what the module decides to hand over.
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

// The module's own hold window, as the adapter passes it.
constexpr time_t HOLD = 600;

// The core's fall arithmetic, copied from Player.cpp:14173-14175 so this file
// can price a drop without including anything. It is here to turn "the
// baseline was wrong by this many yards" into "and that is what it cost".
constexpr float FALL_DMG_EQU_SLOPE = 0.018f;
constexpr float FALL_DMG_EQU_INTERCEPT = -0.2426f;
constexpr float MIN_FALL_DMG_DIST = 13.48f;

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

// A character this module has never moved is never touched, however far it
// walks. This is the common case and it has to cost nothing.
void ACharacterThisModuleNeverMovedIsNeverTouched()
{
    FallBaselineState state;
    time_t now = 1000;
    for (float z = 200.f; z > 40.f; z -= 10.f, now += 1)
    {
        FallBaselineVerdict const v =
            FallBaselineStep(state, true, false, z, now, HOLD);
        Check("unarmed character is left alone", v.rebase, false);
    }
    Check("and nothing was remembered about it", state.held, false);
}

// THE MEASURED INCIDENT. Bork is lifted to 157.3 and walks back down to 65.7
// over 208 seconds under a spline, so no client packet ever lowers the core's
// baseline. Without the guard the core charges the whole 91.6 yards on the
// next landing, which is more than a full health bar. With it, every poll
// hands back the character's own feet and the landing is worth nothing.
void TheWailingCavernsOneShotIsPricedAtNothing()
{
    float const liftZ = 157.3f;
    float const deathZ = 65.7f;

    // What the core would have billed with the lift's own destination still
    // standing as the baseline - the row that was actually written.
    CheckZ("the unguarded drop is the one that was measured", liftZ - deathZ,
           91.6f);
    Check("and the unguarded drop is a one-shot from full health",
          FractionOfMaxHealth(liftZ - deathZ) >= 1.f, true);

    FallBaselineState state;
    time_t const lifted = 5000;
    FallBaselineHandedOver(state, liftZ, lifted);
    Check("the lift arms the guard", state.held, true);
    CheckZ("with the height the lift chose", state.z, liftZ);

    // 208 one-second polls, walking down. Straight-line descent is only a
    // stand-in for the real path; what is being pinned is that every poll
    // hands over wherever the character is standing at that moment.
    float standing = liftZ;
    float const perPoll = (liftZ - deathZ) / 208.f;
    float handedOver = liftZ;
    for (int i = 1; i <= 208; ++i)
    {
        standing -= perPoll;
        FallBaselineVerdict const v =
            FallBaselineStep(state, true, false, standing, lifted + i, HOLD);
        if (!v.rebase)
        {
            std::printf("FAIL poll %d of the walk down handed nothing over\n", i);
            ++failures;
            break;
        }
        handedOver = v.z;
    }
    CheckZ("the last thing handed over is where the character died",
           handedOver, deathZ);
    Check("so the landing is charged nothing at all",
          FractionOfMaxHealth(handedOver - deathZ) == 0.f, true);
}

// The exemption that keeps the fix honest. A character that really is falling
// keeps the baseline it fell from, so the core still charges for the drop.
void AGenuineFallIsStillCharged()
{
    FallBaselineState state;
    time_t const lifted = 5000;
    FallBaselineHandedOver(state, 150.f, lifted);

    // Four seconds of a real fall, seen by four polls. A drop worth charging
    // for cannot hide between two of them: free fall covers about 9.6 yards in
    // its first second and the core charges nothing under 13.48.
    for (int i = 1; i <= 4; ++i)
    {
        FallBaselineVerdict const v = FallBaselineStep(
            state, true, true, 150.f - 20.f * static_cast<float>(i),
            lifted + i, HOLD);
        Check("nothing is handed over mid-fall", v.rebase, false);
    }
    CheckZ("the height the fall began from is still the baseline", state.z,
           150.f);
    Check("which the core prices as a real fall",
          FractionOfMaxHealth(150.f - 70.f) > 0.f, true);

    // The poll after the landing puts the baseline back under its feet, by
    // which time HandleFall has already charged for the drop it was owed.
    FallBaselineVerdict const after =
        FallBaselineStep(state, true, false, 70.f, lifted + 5, HOLD);
    Check("and the poll after the landing resumes", after.rebase, true);
    CheckZ("at the ground it landed on", after.z, 70.f);
}

// Every state the terrain drive stands down for stands this down too. A taxi,
// a flying mount, a boat, a vehicle, a swimmer, a teleport in progress or a
// dead character is somewhere this module has no opinion about.
void EveryStoodDownStateHandsNothingOver()
{
    FallBaselineState state;
    time_t const lifted = 5000;
    FallBaselineHandedOver(state, 150.f, lifted);

    FallBaselineVerdict const v =
        FallBaselineStep(state, false, false, 60.f, lifted + 10, HOLD);
    Check("a character this module may not inspect is left alone", v.rebase,
          false);
    CheckZ("and nothing it was holding is disturbed", state.z, 150.f);
    Check("and it stays armed for when the character is inspectable again",
          state.held, true);
}

// One height stays this module's problem for a bounded time and no longer.
void ResponsibilityForAHeightRunsOut()
{
    FallBaselineState state;
    time_t const lifted = 5000;
    FallBaselineHandedOver(state, 150.f, lifted);

    FallBaselineVerdict const inside =
        FallBaselineStep(state, true, false, 100.f, lifted + HOLD - 1, HOLD);
    Check("one second inside the window still holds the baseline down",
          inside.rebase, true);

    FallBaselineVerdict const out =
        FallBaselineStep(state, true, false, 100.f, lifted + HOLD, HOLD);
    Check("the window closes on the second it says it does", out.rebase,
          false);
    Check("and the height stops being this module's problem", state.held,
          false);

    FallBaselineVerdict const later =
        FallBaselineStep(state, true, false, 40.f, lifted + HOLD + 60, HOLD);
    Check("nothing is handed over afterwards", later.rebase, false);
}

// The window is measured from the lift and not refreshed by the polls, so a
// character that is lifted again gets a fresh claim and one that is not does
// not accumulate an unbounded one.
void ASecondLiftIsAFreshClaimAndAPollIsNot()
{
    FallBaselineState state;
    FallBaselineHandedOver(state, 150.f, 5000);
    for (int i = 1; i < 500; ++i)
        FallBaselineStep(state, true, false, 149.f, 5000 + i, HOLD);
    Check("polling does not extend the claim",
          FallBaselineStep(state, true, false, 100.f, 5000 + HOLD, HOLD).rebase,
          false);

    FallBaselineHandedOver(state, 200.f, 5000 + HOLD);
    CheckZ("a second lift re-arms it at the new height", state.z, 200.f);
    FallBaselineVerdict const v =
        FallBaselineStep(state, true, false, 120.f, 5000 + HOLD + 1, HOLD);
    Check("and the window runs again from there", v.rebase, true);
    CheckZ("under the character's own feet", v.z, 120.f);
}

// ZERO IS THE OFF SWITCH, AND IT IS SPELT AS THE SAFE READING. The dangerous
// meaning of "no window" would be a guard that never expires, so a caller that
// wants no bound has to say a number rather than say nothing.
void ANonPositiveWindowDoesNothingAtAll()
{
    for (time_t hold : {static_cast<time_t>(0), static_cast<time_t>(-1)})
    {
        FallBaselineState state;
        FallBaselineHandedOver(state, 150.f, 5000);
        FallBaselineVerdict const v =
            FallBaselineStep(state, true, false, 60.f, 5001, hold);
        Check("a non-positive window hands nothing over", v.rebase, false);
        Check("and forgets the height rather than holding it forever",
              state.held, false);
    }
}

// Whichever way the server and the client disagree about where a character is,
// the server's own figure is the honest baseline: it is either where a spline
// has walked the character to, or the height a silent client has yet to report
// leaving. Handing back a HIGHER standing z than the last one is therefore
// right, and is the case a "only ever lower it" rule would have got wrong.
void TheServersOwnHeightIsWhatGoesBackEvenWhenItRises()
{
    FallBaselineState state;
    time_t const lifted = 5000;
    FallBaselineHandedOver(state, 100.f, lifted);

    FallBaselineVerdict const down =
        FallBaselineStep(state, true, false, 80.f, lifted + 1, HOLD);
    CheckZ("walking down hands down", down.z, 80.f);

    FallBaselineVerdict const up =
        FallBaselineStep(state, true, false, 130.f, lifted + 2, HOLD);
    Check("climbing back above the lift still hands over", up.rebase, true);
    CheckZ("the height it climbed to", up.z, 130.f);

    // The point of that: a character standing at 130 whose baseline was left
    // at 80 would be charged nothing for a real 60-yard drop afterwards.
    Check("so a real drop from up there is still chargeable",
          FractionOfMaxHealth(up.z - 70.f) > 0.f, true);
}

}  // namespace

int main()
{
    ACharacterThisModuleNeverMovedIsNeverTouched();
    TheWailingCavernsOneShotIsPricedAtNothing();
    AGenuineFallIsStillCharged();
    EveryStoodDownStateHandsNothingOver();
    ResponsibilityForAHeightRunsOut();
    ASecondLiftIsAFreshClaimAndAPollIsNot();
    ANonPositiveWindowDoesNothingAtAll();
    TheServersOwnHeightIsWhatGoesBackEvenWhenItRises();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a lift does not leave a fall for the core to charge for\n");
    return EXIT_SUCCESS;
}
