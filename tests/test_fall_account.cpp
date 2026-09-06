/*
 * A recorded drop is checked, not eyeballed.
 *
 * mod-overseer#243. For a day five characters were believed to be falling to
 * their deaths, because a self-attributed kill is how the core attributes
 * fall damage and nobody could check the distance against the damage. The
 * core's own counters then said the deaths really were DAMAGE_FALL, while
 * yards_fallen read 0.00, 0.07, 26.39, 4.09 and 0.29. Both facts are true:
 * upstream's dismount hands Player::HandleFall a fall the character never
 * took, so the column and the damage are measuring different things.
 *
 * This file pins the core's arithmetic so the column can be read against it:
 * what a drop costs, what it takes to kill outright, and above all that an
 * unsampled row is not a row that did not fall.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using OverseerDecisions::AccountForFall;
using OverseerDecisions::FALL_DAMAGE_MIN_YARDS;
using OverseerDecisions::FallAccount;
using OverseerDecisions::FallAccountName;
using OverseerDecisions::FallDamageShare;
using OverseerDecisions::LethalFallYards;

namespace
{

int failures = 0;

void Near(char const* what, float got, float want)
{
    if (std::fabs(got - want) <= 0.001f)
        return;
    std::printf("FAIL %s: got %.4f, wanted %.4f\n", what, got, want);
    ++failures;
}

void Is(char const* what, FallAccount got, FallAccount want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, FallAccountName(got),
                FallAccountName(want));
    ++failures;
}

// The core deals nothing below its own minimum distance, and no rate changes
// that. This is what rules a fall out for four of the five rows outright.
void AShortDropCostsNothingAtAnyRate()
{
    Near("just under the gate", FallDamageShare(13.47f), 0.f);
    Near("just under the gate at ten times the rate",
         FallDamageShare(13.47f, 0.f, 10.f), 0.f);
    Near("one second of free fall is 9.6 yards",
         FallDamageShare(9.6f, 0.f, 10.f), 0.f);
    Near("a flat zero costs nothing", FallDamageShare(0.f), 0.f);
    if (!(FallDamageShare(FALL_DAMAGE_MIN_YARDS) > 0.f))
    {
        std::printf("FAIL the gate itself should charge something\n");
        ++failures;
    }
}

// The threshold that matters: what it takes to kill from full health. It is
// a share of MAX HEALTH, so it is the same 69 yards for every character.
void SixtyNineYardsKillsWhoeverYouAre()
{
    Near("stock realm lethal distance", LethalFallYards(), 69.0333f);
    Near("sixty-nine yards is a hair short", FallDamageShare(69.f), 0.9994f);
    Near("the share clamps at max health", FallDamageShare(500.f), 1.f);
    Is("69.0 yards", AccountForFall(69.0f), FallAccount::Survivable);
    Is("69.1 yards", AccountForFall(69.1f), FallAccount::EnoughToKill);
    // Twice the rate halves the drop it takes, and a rate of zero disables
    // fall damage altogether so nothing is lethal.
    Near("lethal distance at rate 2", LethalFallYards(0.f, 2.f), 41.2556f);
    Near("lethal distance with 10 yards of safe fall",
         LethalFallYards(10.f), 79.0333f);
    Near("no fall damage at rate zero", FallDamageShare(500.f, 0.f, 0.f), 0.f);
}

// The five instrumented rows of #243, read as the column actually wrote them.
void TheIncidentRowsCannotAccountForTheDeaths()
{
    Is("3107 Bork, fell 0.00", AccountForFall(0.00f), FallAccount::NoDrop);
    Is("3108 Og, fell 0.07", AccountForFall(0.0689621f),
       FallAccount::TooShortToHurt);
    Is("3109 Bork, fell 26.39", AccountForFall(26.39f), FallAccount::Survivable);
    Is("3110 Grug, fell 4.09", AccountForFall(4.09f),
       FallAccount::TooShortToHurt);
    Is("3111 Og, fell 0.29", AccountForFall(0.29f), FallAccount::TooShortToHurt);
    // The one row with a real descent still only costs a quarter of the bar.
    Near("26.39 yards is 23% of max health", FallDamageShare(26.39f), 0.2324f);
}

// The distinction the whole column exists for, and the one a report folds
// away: nobody looked is not the same finding as it did not fall.
void UnsampledIsNotNoDrop()
{
    Is("the unsampled marker", AccountForFall(-1.f), FallAccount::Unsampled);
    Is("any negative is unsampled", AccountForFall(-0.001f),
       FallAccount::Unsampled);
    Is("a sampled zero", AccountForFall(0.f), FallAccount::NoDrop);
    if (AccountForFall(-1.f) == AccountForFall(0.f))
    {
        std::printf("FAIL unsampled and no drop must not fold together\n");
        ++failures;
    }
}

}  // namespace

int main()
{
    AShortDropCostsNothingAtAnyRate();
    SixtyNineYardsKillsWhoeverYouAre();
    TheIncidentRowsCannotAccountForTheDeaths();
    UnsampledIsNotNoDrop();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a recorded drop is checked, not eyeballed\n");
    return EXIT_SUCCESS;
}
