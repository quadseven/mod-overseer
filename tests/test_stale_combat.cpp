/*
 * A combat flag that never clears is released, and a real fight never is
 * (#1390).
 *
 * The live failure: a quester refused `character is in combat` on every walk,
 * crossing and vendor errand for more than 29 hours, at one position, with no
 * fight, death or kill line between the refusals. The rule in
 * src/overseer_stale_combat.h releases a character that has been in combat
 * with no hostile in reach for ten unbroken minutes in the open world.
 *
 * THE NEGATIVE TESTS MATTER MOST: a live attacker, a stretch under the bound,
 * a hostile that comes back mid-stretch, and an instanced map must all keep
 * the character in combat. A rule that dropped any of those would cut real
 * fights short.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_stale_combat.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::DecideStaleCombat;
using OverseerDecisions::SayStaleCombatHeld;
using OverseerDecisions::STALE_COMBAT_REACH_YARDS;
using OverseerDecisions::STALE_COMBAT_RELEASE_SECONDS;
using OverseerDecisions::STALE_COMBAT_SAY_HELD_SECONDS;
using OverseerDecisions::StaleCombatFacts;
using OverseerDecisions::StaleCombatStep;
using OverseerDecisions::StaleCombatVerdict;

namespace
{

int failures = 0;

char const* Name(StaleCombatVerdict v)
{
    switch (v)
    {
        case StaleCombatVerdict::NotInCombat:   return "not in combat";
        case StaleCombatVerdict::OutOfScope:    return "out of scope";
        case StaleCombatVerdict::HeldByHostile: return "held by hostile";
        case StaleCombatVerdict::Watching:      return "watching";
        case StaleCombatVerdict::Release:       return "release";
    }
    return "unknown";
}

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckVerdict(char const* what, StaleCombatVerdict got, StaleCombatVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

constexpr time_t T0 = 1794000000;

StaleCombatFacts Stale(time_t now)
{
    StaleCombatFacts f;
    f.alive = true;
    f.inCombat = true;
    f.onInstanceMap = false;
    f.hostilesInReach = 0;
    f.now = now;
    return f;
}

void TheBoundsAreTheLiveOnes()
{
    Check("release bound is ten minutes", STALE_COMBAT_RELEASE_SECONDS == 600);
    Check("reach is forty yards", STALE_COMBAT_REACH_YARDS == 40.0f);
    Check("held line every thirty minutes", STALE_COMBAT_SAY_HELD_SECONDS == 1800);
}

// The measured case, replayed on the engagement poll's 8 second clock: empty
// combat from T0, released on the first poll at or past ten minutes.
void EmptyCombatPastTheBoundIsReleased()
{
    time_t since = 0;
    StaleCombatStep step;
    time_t t = T0;
    for (; t < T0 + STALE_COMBAT_RELEASE_SECONDS; t += 8)
    {
        step = DecideStaleCombat(Stale(t), since);
        CheckVerdict("empty combat under the bound is watched", step.verdict,
                     StaleCombatVerdict::Watching);
        since = step.emptySince;
    }
    Check("the stretch began at the first poll", since == T0);
    step = DecideStaleCombat(Stale(t), since);
    CheckVerdict("empty combat at the bound is released", step.verdict,
                 StaleCombatVerdict::Release);
    Check("the release says how long it was held", step.heldSeconds == t - T0);
    Check("the release forgets the stretch", step.emptySince == 0);
}

void UnderTheBoundIsNotReleased()
{
    StaleCombatStep const step =
        DecideStaleCombat(Stale(T0 + STALE_COMBAT_RELEASE_SECONDS - 1), T0);
    CheckVerdict("one second under the bound is watched", step.verdict,
                 StaleCombatVerdict::Watching);
    Check("and the stretch is carried", step.emptySince == T0);
}

void ALiveAttackerIsNeverReleased()
{
    StaleCombatFacts f = Stale(T0 + 29 * 3600);
    f.hostilesInReach = 1;
    StaleCombatStep const step = DecideStaleCombat(f, T0);
    CheckVerdict("a hostile in reach holds, however long", step.verdict,
                 StaleCombatVerdict::HeldByHostile);
    Check("and restarts the stretch", step.emptySince == 0);
}

// A hostile that comes back mid-stretch restarts the clock: only an unbroken
// stretch counts, so a fight with a gap in it is never released at its end.
void AHostileMidStretchRestartsTheClock()
{
    time_t since = DecideStaleCombat(Stale(T0), 0).emptySince;
    StaleCombatFacts back = Stale(T0 + 300);
    back.hostilesInReach = 2;
    since = DecideStaleCombat(back, since).emptySince;
    Check("a hostile ends the stretch", since == 0);
    since = DecideStaleCombat(Stale(T0 + 308), since).emptySince;
    Check("the next empty poll starts a new one", since == T0 + 308);
    StaleCombatStep const step = DecideStaleCombat(Stale(T0 + 600), since);
    CheckVerdict("ten minutes after the first poll is not ten unbroken minutes",
                 step.verdict, StaleCombatVerdict::Watching);
}

void OutOfCombatOrDeadForgets()
{
    StaleCombatFacts f = Stale(T0 + 900);
    f.inCombat = false;
    StaleCombatStep step = DecideStaleCombat(f, T0);
    CheckVerdict("out of combat", step.verdict, StaleCombatVerdict::NotInCombat);
    Check("out of combat forgets the stretch", step.emptySince == 0);
    f = Stale(T0 + 900);
    f.alive = false;
    step = DecideStaleCombat(f, T0);
    CheckVerdict("dead", step.verdict, StaleCombatVerdict::NotInCombat);
}

void AnInstancedMapIsNotJudged()
{
    StaleCombatFacts f = Stale(T0 + 3600);
    f.onInstanceMap = true;
    StaleCombatStep const step = DecideStaleCombat(f, T0);
    CheckVerdict("dungeon, raid, battleground or arena", step.verdict,
                 StaleCombatVerdict::OutOfScope);
    Check("and nothing is carried", step.emptySince == 0);
}

void AClockFromTheFutureRestarts()
{
    StaleCombatStep const step = DecideStaleCombat(Stale(T0), T0 + 5000);
    CheckVerdict("a stretch that began after now restarts", step.verdict,
                 StaleCombatVerdict::Watching);
    Check("from now", step.emptySince == T0);
}

void TheHeldLineIsBounded()
{
    Check("not under thirty minutes",
          !SayStaleCombatHeld(T0 + STALE_COMBAT_SAY_HELD_SECONDS - 1, T0, 0));
    Check("at thirty minutes, never said",
          SayStaleCombatHeld(T0 + STALE_COMBAT_SAY_HELD_SECONDS, T0, 0));
    Check("not again a minute later",
          !SayStaleCombatHeld(T0 + STALE_COMBAT_SAY_HELD_SECONDS + 60, T0,
                              T0 + STALE_COMBAT_SAY_HELD_SECONDS));
    Check("again thirty minutes later",
          SayStaleCombatHeld(T0 + 2 * STALE_COMBAT_SAY_HELD_SECONDS, T0,
                             T0 + STALE_COMBAT_SAY_HELD_SECONDS));
    Check("never without a combat start", !SayStaleCombatHeld(T0 + 99999, 0, 0));
}

}  // namespace

int main()
{
    TheBoundsAreTheLiveOnes();
    EmptyCombatPastTheBoundIsReleased();
    UnderTheBoundIsNotReleased();
    ALiveAttackerIsNeverReleased();
    AHostileMidStretchRestartsTheClock();
    OutOfCombatOrDeadForgets();
    AnInstancedMapIsNotJudged();
    AClockFromTheFutureRestarts();
    TheHeldLineIsBounded();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a combat flag with nothing behind it is released after ten minutes\n");
    return EXIT_SUCCESS;
}
