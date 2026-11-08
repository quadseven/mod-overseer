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
using OverseerDecisions::STALE_COMBAT_STANDOFF_MOVE_YARDS;
using OverseerDecisions::STALE_COMBAT_STANDOFF_SECONDS;
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
        case StaleCombatVerdict::StandoffWatching: return "standoff watching";
        case StaleCombatVerdict::ReleaseStandoff:  return "release standoff";
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

// #1411, the measured case: a character at full health, a creature five yards
// off at full health, the character unmoved, for hours. On the 8 second poll
// it is watched for fifteen minutes and then released as a standoff.
StaleCombatFacts Standoff(time_t now)
{
    StaleCombatFacts f = Stale(now);
    f.hostilesInReach = 2;
    f.standoff = true;
    return f;
}

void AStandoffPastTheBoundIsReleased()
{
    Check("standoff bound is fifteen minutes", STALE_COMBAT_STANDOFF_SECONDS == 900);
    Check("a standoff allows five yards of drift", STALE_COMBAT_STANDOFF_MOVE_YARDS == 5.0f);
    time_t since = 0;
    StaleCombatStep step;
    time_t t = T0;
    for (; t < T0 + STALE_COMBAT_STANDOFF_SECONDS; t += 8)
    {
        step = DecideStaleCombat(Standoff(t), since);
        CheckVerdict("a standoff under the bound is watched", step.verdict,
                     StaleCombatVerdict::StandoffWatching);
        since = step.emptySince;
    }
    Check("the standoff began at the first poll", since == T0);
    step = DecideStaleCombat(Standoff(t), since);
    CheckVerdict("a standoff at the bound is released", step.verdict,
                 StaleCombatVerdict::ReleaseStandoff);
    Check("the standoff release says how long it was held", step.heldSeconds == t - T0);
}

void AStandoffUnderTheBoundIsNotReleased()
{
    StaleCombatStep const step =
        DecideStaleCombat(Standoff(T0 + STALE_COMBAT_STANDOFF_SECONDS - 1), T0);
    CheckVerdict("one second under the standoff bound is watched", step.verdict,
                 StaleCombatVerdict::StandoffWatching);
    Check("and the stretch is carried", step.emptySince == T0);
    // The empty-combat bound (ten minutes) does not release a standoff.
    StaleCombatStep const ten =
        DecideStaleCombat(Standoff(T0 + STALE_COMBAT_RELEASE_SECONDS), T0);
    CheckVerdict("ten minutes of standoff is not enough", ten.verdict,
                 StaleCombatVerdict::StandoffWatching);
}

// Damage on either side, or a move, makes the drive say standoff=false: that
// is a fight, and it restarts the clock however long the standoff ran.
void AHurtSideOrAMoveRestartsTheStandoff()
{
    time_t since = DecideStaleCombat(Standoff(T0), 0).emptySince;
    StaleCombatFacts hurt = Standoff(T0 + 840);
    hurt.standoff = false;
    StaleCombatStep step = DecideStaleCombat(hurt, since);
    CheckVerdict("a blow lands: a fight", step.verdict, StaleCombatVerdict::HeldByHostile);
    Check("the fight ends the stretch", step.emptySince == 0);
    since = DecideStaleCombat(Standoff(T0 + 848), step.emptySince).emptySince;
    Check("a new standoff starts after it", since == T0 + 848);
    step = DecideStaleCombat(Standoff(T0 + STALE_COMBAT_STANDOFF_SECONDS), since);
    CheckVerdict("fifteen minutes after the first poll is not fifteen unbroken",
                 step.verdict, StaleCombatVerdict::StandoffWatching);
}

// The hostiles leave mid-standoff: the stretch goes on as empty combat, which
// is released on its own (shorter) bound from the same start.
void HostilesLeavingKeepTheStretch()
{
    time_t const since = DecideStaleCombat(Standoff(T0), 0).emptySince;
    StaleCombatStep const step =
        DecideStaleCombat(Stale(T0 + STALE_COMBAT_RELEASE_SECONDS), since);
    CheckVerdict("no hostile left, ten minutes after a standoff began", step.verdict,
                 StaleCombatVerdict::Release);
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
    AStandoffPastTheBoundIsReleased();
    AStandoffUnderTheBoundIsNotReleased();
    AHurtSideOrAMoveRestartsTheStandoff();
    HostilesLeavingKeepTheStretch();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a combat flag with nothing behind it is released after ten minutes\n");
    return EXIT_SUCCESS;
}
