/*
 * The death-rate breaker on a travel errand: the AGENTS.md rule that says a
 * destination costing more than roughly three deaths in five minutes is not
 * worth the crossing.
 *
 * EVERY FIXTURE BELOW IS A MEASURED ROW, not a number chosen to make the
 * predicate fire. The clock is `acore_characters.overseer_death` for one
 * roster on 2026-09-06, counted in seconds past 20:00:00 UTC:
 *
 *   47 deaths between 20:18 and 20:47, 41 of them to one level 65 elite
 *   14 of those were the AIMED LEADER'S OWN, 20:18:05 .. 20:46:25
 *   43 of those 47 came after the moment this rule first says stop
 *
 * Compiled without AzerothCore so the rule stays a pure decision.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::ErrandDeathBreaker;
using OverseerDecisions::ErrandDeathLimits;
using OverseerDecisions::ErrandDeathRemedy;
using OverseerDecisions::ErrandDeathToll;
using OverseerDecisions::ErrandDeathWindow;

namespace
{

int failures = 0;

char const* RemedyName(ErrandDeathRemedy remedy)
{
    switch (remedy)
    {
        case ErrandDeathRemedy::Continue:
            return "Continue";
        case ErrandDeathRemedy::Release:
            return "Release";
        case ErrandDeathRemedy::RefuseReissue:
            return "RefuseReissue";
        case ErrandDeathRemedy::DeclineRunOwned:
            return "DeclineRunOwned";
    }
    return "?";
}

void CheckRemedy(char const* what, ErrandDeathRemedy got, ErrandDeathRemedy want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RemedyName(got), RemedyName(want));
    ++failures;
}

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

// The aimed leader's own deaths, seconds past 20:00:00 UTC on 2026-09-06.
// 20:18:05, 20:19:20, 20:20:26, 20:21:47, 20:24:15, 20:32:12, 20:33:45,
// 20:35:15, 20:36:42, 20:38:12, 20:39:27, 20:41:31, 20:44:58, 20:46:25.
std::vector<long> const& TheLeadersOwnDeaths()
{
    static std::vector<long> const rows{1085, 1160, 1226, 1307, 1455, 1932, 2025,
                                        2115, 2202, 2292, 2367, 2491, 2698, 2785};
    return rows;
}

// The errand was outstanding by 20:18:05 and was not at 20:14:59, both read off
// `overseer_death.travel_target`. 20:15:00 is the earliest moment it can have
// started, which is the reading that gives the rule its LONGEST window and so
// its easiest firing - deliberately, because the interesting question is
// whether it fires at all, and a later start only delays it.
long const kErrandStarted = 900;

uint32_t DeathsInWindow(long at, long window)
{
    uint32_t count = 0;
    for (long const row : TheLeadersOwnDeaths())
        if (row > at - window && row <= at)
            ++count;
    return count;
}

// Replay the table poll by poll and hand back the first moment the rule says
// stop, or -1 if it never does.
long TheMomentItWouldHaveFired(ErrandDeathLimits const& limits)
{
    for (long const at : TheLeadersOwnDeaths())
    {
        ErrandDeathToll toll;
        toll.deaths = DeathsInWindow(at, ErrandDeathWindow(at - kErrandStarted, limits));
        if (ErrandDeathBreaker(toll, limits).remedy == ErrandDeathRemedy::Release)
            return at;
    }
    return -1;
}

void TheThirdBodyInFiveMinutesEndsTheErrand()
{
    ErrandDeathLimits const limits;

    // 20:20:26. The two deaths before it are 20:18:05 and 20:19:20, so the
    // third arrives 141 seconds after the first - well inside five minutes.
    CheckInt("the errand is released at 20:20:26", TheMomentItWouldHaveFired(limits), 1226);

    // What that would have bought, measured rather than asserted: 43 of the 47
    // deaths this roster suffered between 20:18 and 20:47 came after that
    // moment. The rule was not close to firing - it was 26 minutes late.
    ErrandDeathToll atTheThird;
    atTheThird.deaths = DeathsInWindow(1226, ErrandDeathWindow(1226 - kErrandStarted, limits));
    CheckInt("three deaths are counted at 20:20:26", long(atTheThird.deaths), 3);
    CheckInt("the window used at 20:20:26 is the full five minutes",
             ErrandDeathWindow(1226 - kErrandStarted, limits), 300);
}

void TwoBodiesAreNotThree()
{
    ErrandDeathLimits const limits;

    // 20:19:20, the poll before. Two deaths in the window, and the errand runs.
    ErrandDeathToll toll;
    toll.deaths = DeathsInWindow(1160, ErrandDeathWindow(1160 - kErrandStarted, limits));
    CheckInt("two deaths are counted at 20:19:20", long(toll.deaths), 2);
    CheckRemedy("two deaths do not end the errand", ErrandDeathBreaker(toll, limits).remedy,
                ErrandDeathRemedy::Continue);
}

void AQuietErrandSaysNothingAtAll()
{
    ErrandDeathLimits const limits;
    ErrandDeathToll toll;
    CheckRemedy("an errand with no deaths behind it", ErrandDeathBreaker(toll, limits).remedy,
                ErrandDeathRemedy::Continue);
    CheckInt("and nothing to report about a cool-off",
             long(ErrandDeathBreaker(toll, limits).coolOffRemaining), 0);
}

void AFreshErrandDoesNotInheritTheLastErrandsCorpses()
{
    ErrandDeathLimits const limits;

    // The same table, read by an errand that started at 20:24:00 - after five
    // of these deaths and before the rest. At 20:24:15 it is fifteen seconds
    // old, so it answers for fifteen seconds of table and one body, not for
    // five minutes of somebody else's crossing.
    long const startedLate = 1440;  // 20:24:00
    long const at = 1455;           // 20:24:15
    CheckInt("a fifteen second errand is judged over fifteen seconds",
             ErrandDeathWindow(at - startedLate, limits), 15);

    ErrandDeathToll toll;
    toll.deaths = DeathsInWindow(at, ErrandDeathWindow(at - startedLate, limits));
    CheckInt("and counts one body, not five", long(toll.deaths), 1);
    CheckRemedy("so a fresh errand is not released before it has walked",
                ErrandDeathBreaker(toll, limits).remedy, ErrandDeathRemedy::Continue);

    // The same instant judged by the errand that really was outstanding - which
    // by then has been running nine minutes - reads the full window.
    CheckInt("while the errand that was actually running reads five minutes",
             ErrandDeathWindow(at - kErrandStarted, limits), 300);
}

void AnErrandWithNoAgeIsAskedNothing()
{
    ErrandDeathLimits const limits;
    CheckInt("an errand claimed this instant has no window", ErrandDeathWindow(0, limits), 0);
    CheckInt("and a clock that has not been stamped yet has none either",
             ErrandDeathWindow(-5, limits), 0);
}

void AReleasedTargetStaysRefusedWhileSomethingReArmsIt()
{
    ErrandDeathLimits const limits;

    // Measured: the column was empty at 20:55 and carried the same errand again
    // by 21:00, written by something outside this module. The re-aimed errand
    // is brand new, so its own toll is zero and nothing but the refusal
    // remembers what it cost.
    ErrandDeathToll reArmed;
    reArmed.deaths = 0;
    reArmed.sinceRefused = 300;

    auto const verdict = ErrandDeathBreaker(reArmed, limits);
    CheckRemedy("a re-aimed target that killed them is refused, not walked", verdict.remedy,
                ErrandDeathRemedy::RefuseReissue);
    CheckInt("and says how much of the cool-off is left", long(verdict.coolOffRemaining), 600);
}

void TheCoolOffEndsAndTheErrandMayBeTriedAgain()
{
    ErrandDeathLimits const limits;

    ErrandDeathToll justInside;
    justInside.sinceRefused = limits.cooloffSeconds - 1;
    CheckRemedy("one second short of the cool-off is still refused",
                ErrandDeathBreaker(justInside, limits).remedy, ErrandDeathRemedy::RefuseReissue);

    ErrandDeathToll lapsed;
    lapsed.sinceRefused = limits.cooloffSeconds;
    CheckRemedy("a lapsed cool-off lets the errand run again",
                ErrandDeathBreaker(lapsed, limits).remedy, ErrandDeathRemedy::Continue);

    ErrandDeathToll never;
    never.sinceRefused = -1;
    CheckRemedy("and a target never refused is not cooling off",
                ErrandDeathBreaker(never, limits).remedy, ErrandDeathRemedy::Continue);
}

void ARunsOwnAimIsDeclinedRatherThanReleasedIntoAWall()
{
    ErrandDeathLimits const limits;

    ErrandDeathToll dying;
    dying.deaths = 9;
    dying.runOwned = true;
    CheckRemedy("a run's aim is declined, not released",
                ErrandDeathBreaker(dying, limits).remedy, ErrandDeathRemedy::DeclineRunOwned);

    ErrandDeathToll reClaimed;
    reClaimed.sinceRefused = 10;
    reClaimed.runOwned = true;
    CheckRemedy("and a run that re-Claims a refused target wins the argument",
                ErrandDeathBreaker(reClaimed, limits).remedy,
                ErrandDeathRemedy::DeclineRunOwned);

    // Declining is not the same as being noisy about it: a run whose party is
    // not dying has nothing to decline and says so by saying nothing.
    ErrandDeathToll quiet;
    quiet.deaths = 1;
    quiet.runOwned = true;
    CheckRemedy("a quiet run is not declined either", ErrandDeathBreaker(quiet, limits).remedy,
                ErrandDeathRemedy::Continue);
}

void TheFourRemediesDoNotReadAlike()
{
    ErrandDeathLimits const limits;

    ErrandDeathToll release;
    release.deaths = limits.deaths;

    ErrandDeathToll refuse;
    refuse.sinceRefused = 1;

    ErrandDeathToll decline;
    decline.deaths = limits.deaths;
    decline.runOwned = true;

    ErrandDeathToll carryOn;

    CheckRemedy("this errand is killing it", ErrandDeathBreaker(release, limits).remedy,
                ErrandDeathRemedy::Release);
    CheckRemedy("this errand already killed it", ErrandDeathBreaker(refuse, limits).remedy,
                ErrandDeathRemedy::RefuseReissue);
    CheckRemedy("this errand is killing it and is not mine to stop",
                ErrandDeathBreaker(decline, limits).remedy,
                ErrandDeathRemedy::DeclineRunOwned);
    CheckRemedy("nothing is wrong", ErrandDeathBreaker(carryOn, limits).remedy,
                ErrandDeathRemedy::Continue);
}

}  // namespace

int main()
{
    TheThirdBodyInFiveMinutesEndsTheErrand();
    TwoBodiesAreNotThree();
    AQuietErrandSaysNothingAtAll();
    AFreshErrandDoesNotInheritTheLastErrandsCorpses();
    AnErrandWithNoAgeIsAskedNothing();
    AReleasedTargetStaysRefusedWhileSomethingReArmsIt();
    TheCoolOffEndsAndTheErrandMayBeTriedAgain();
    ARunsOwnAimIsDeclinedRatherThanReleasedIntoAWall();
    TheFourRemediesDoNotReadAlike();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the errand that killed them 47 times is released at the third body\n");
    return EXIT_SUCCESS;
}
