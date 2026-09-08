/*
 * The economy errand budget, decided without a world.
 *
 * WHAT IT IS FOR. A character whose economy errands all SUCCEED can still be
 * doing nothing else. Measured on the dev realm over thirty minutes of one
 * character's world log: nine errands, alternating `vendor` and `repair`,
 * holding the quest drive down for 1181 seconds of 1781 - 66.3% - with ten
 * arrivals for ten sendings and not one failure anywhere in it. His
 * `quests_rewarded` had not moved in twelve hours; the four siblings who were
 * issued no travel errands at all in the same window were gaining levels.
 *
 * Every other guard on that drive is keyed to an errand going BADLY - deaths,
 * an unreachable destination, refused footing, a twenty-minute backstop - so a
 * short errand that arrives is invisible to all of them. The harm is the rate,
 * and no single one of the nine errands is wrong.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - It is a BUDGET and not a cooldown, because the measured loop ALTERNATES
 *     roles. A rule of the shape "not the same errand twice running" never
 *     fires on five `vendor` interleaved with three `repair`, so the case that
 *     pins the alternation is the case that says why this shape was chosen.
 *   - It REFILLS, so a character that has been questing is never held off. That
 *     is what keeps the rule off the four siblings who do not have this
 *     problem, and a budget that only ever fills is a rule that eventually
 *     refuses everyone.
 *   - The bucket is CLAMPED at both ends, and not at the same place. Credit
 *     banked by a character that has not seen a counter in a day is not a
 *     licence to spend a day at one. Above the line it carries one bucket of
 *     DEBT, because the rule is asked once per poll and a total that stopped at
 *     the line would discard the last poll's overshoot instead of repaying it -
 *     worth a case of its own because that leak is invisible in every test
 *     except one that counts the share over hours.
 *   - First sight of a character is not a gap to drain. `markedAt` of zero is
 *     "never looked at", and reading it as an epoch timestamp would hand out
 *     decades of credit on the first poll.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

using OverseerDecisions::ErrandBudgetLimits;
using OverseerDecisions::ErrandOverspent;
using OverseerDecisions::ErrandSpend;
using OverseerDecisions::ErrandSpendAfter;

namespace
{

// The limits mod_overseer.cpp passes in, named here so a failure reads as the
// rule rather than as a pair of numbers: a 1800 second window, 420 of it
// spendable.
ErrandBudgetLimits const LIMITS{};

// A wall clock that is obviously not an epoch, so a test that accidentally
// reads `markedAt == 0` as a time cannot pass by coincidence.
time_t const T0 = 1700000000;

int failures = 0;

void CheckHeld(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: overspent is %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckSeconds(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: bucket holds %lld, wanted %lld\n", what, got, want);
    ++failures;
}

// A character this rule has never looked at spends its first errand with no
// delay whatever. This is the property that keeps the rule off everybody who
// does not have the problem.
void AFreshCharacterIsNeverHeld()
{
    ErrandSpend spend;
    CheckHeld("untouched character", ErrandOverspent(spend, LIMITS), false);

    // ...and its first mark only starts the clock. A `markedAt` of zero read as
    // an epoch timestamp would drain decades here; the bucket must simply take
    // the sixty seconds it was handed.
    spend = ErrandSpendAfter(spend, T0, 60, LIMITS);
    CheckSeconds("first errand", spend.seconds, 60);
    CheckHeld("after one errand", ErrandOverspent(spend, LIMITS), false);
}

// THE MEASURED LOOP, replayed. Nine errands alternating vendor and repair, the
// real durations off the world log, with the real gaps between them. The rule
// has to bite somewhere inside it.
//
// AND ONLY THAT, DELIBERATELY. A bucket that starts empty lets the first burst
// through in full - that is the same property as "a fresh character is never
// held", seen from the other side - so WHICH errand catches it is a fact about
// how long this character had been quiet beforehand, not about the rule.
// Pinning a particular index here would be pinning the warm-up. The share the
// rule actually holds to is pinned below, over a run long enough for the
// warm-up not to dominate.
void TheMeasuredLoopIsCaught()
{
    // Errand hold in seconds, and the wall seconds from this errand's start to
    // the next one's, straight off the log.
    long long const held[] = {60, 280, 60, 81, 60, 60, 60, 60, 460};
    long long const stride[] = {60, 300, 81, 140, 60, 160, 80, 220, 460};

    ErrandSpend spend;
    time_t now = T0;
    int caughtAt = -1;

    for (int i = 0; i < 9; ++i)
    {
        if (ErrandOverspent(spend, LIMITS))
        {
            caughtAt = i;
            break;
        }
        spend = ErrandSpendAfter(spend, now, held[i], LIMITS);
        now += static_cast<time_t>(stride[i]);
    }

    if (caughtAt < 0)
    {
        std::printf("FAIL measured loop: ran all nine errands without being held\n");
        ++failures;
    }
}

// THE SHARE ITSELF, WHICH IS THE WHOLE POINT OF THE RULE. The measured loop is
// replayed for six hours against a caller that obeys the verdict - it skips an
// errand it is told to skip, exactly as the drive does - and the errand seconds
// that got through are counted.
//
// THIS IS THE CASE THAT WOULD CATCH A WRONG DRAIN. Every other case here can be
// satisfied by a rule that fires sometimes; only this one says the rule holds a
// character to the share it was given, which is the sentence the fix claims.
// Measured against 66.3% before, the same loop through this rule must come out
// at or under the allowance, with a little room for the warm-up burst that a
// bucket starting empty always lets past.
void TheShareIsHeldOverTheLongRun()
{
    long long const held[] = {60, 280, 60, 81, 60, 60, 60, 60, 460};
    long long const stride[] = {60, 300, 81, 140, 60, 160, 80, 220, 460};

    // THE DRIVE'S OWN POLL, because the rule is asked on it and not once per
    // errand. Fifteen seconds is TRAVEL_POLL_MS. Modelling this as one decision
    // per errand instead would let a single 460 second errand - and the log has
    // one - spend the whole allowance in one go before anything looked, which
    // is precisely the overshoot the per-poll ask exists to stop.
    long long const POLL = 15;

    ErrandSpend spend;
    time_t const start = T0;
    time_t now = start;
    time_t const finish = start + 6 * 60 * 60;

    long long errandSeconds = 0;
    for (int i = 0; now < finish; i = (i + 1) % 9)
    {
        // The errand runs, one poll at a time, until it lands or the budget
        // runs out under it.
        for (long long spent = 0; spent < held[i] && now < finish; spent += POLL)
        {
            if (ErrandOverspent(spend, LIMITS))
                break;
            spend = ErrandSpendAfter(spend, now, POLL, LIMITS);
            errandSeconds += POLL;
            now += static_cast<time_t>(POLL);
        }
        // ...then whatever is left of the gap before the next one, during which
        // the character quests and the bucket drains.
        long long const gap = stride[i] - held[i];
        for (long long idle = 0; idle < gap && now < finish; idle += POLL)
        {
            spend = ErrandSpendAfter(spend, now, 0, LIMITS);
            now += static_cast<time_t>(POLL);
        }
    }

    long long const wall = static_cast<long long>(finish - start);
    long long const allowed = wall * LIMITS.spendSeconds / LIMITS.windowSeconds;
    // The warm-up burst is bounded by one full bucket, so that is the whole of
    // the slack this may need. Anything beyond it is a drain that does not
    // match the constants.
    long long const ceiling = allowed + LIMITS.spendSeconds;

    if (errandSeconds > ceiling)
    {
        std::printf("FAIL long run: %lld errand seconds in %lld, wanted at most %lld\n",
                    errandSeconds, wall, ceiling);
        ++failures;
    }
    // ...and it must not have strangled the errands entirely: a rule that
    // refuses everything would also pass the line above, and would be a worse
    // bug than the one it replaced.
    if (errandSeconds <= 0)
    {
        std::printf("FAIL long run: no errand ran at all in %lld seconds\n", wall);
        ++failures;
    }
}

// THE CASE THAT SAYS WHY THIS IS A BUDGET. The measured nine never repeat a
// role twice running, so a "not the same errand twice" rule sees nothing. This
// pins that the budget does not care which counter it was.
void AlternatingRolesDoNotEscape()
{
    ErrandSpend spend;
    time_t now = T0;
    // Strict alternation, each errand a minute long, one every two minutes.
    for (int i = 0; i < 20 && !ErrandOverspent(spend, LIMITS); ++i)
    {
        spend = ErrandSpendAfter(spend, now, 60, LIMITS);
        now += 120;
    }
    // A minute of errand every two minutes is half the wall clock against an
    // allowance of just under a quarter, so this must be caught.
    CheckHeld("strict alternation at 50 percent", ErrandOverspent(spend, LIMITS), true);
}

// A character that spends its allowance and no more holds level forever. This
// is the rule's own definition of "a share of a window" and is the case that
// would catch a drain rate that does not match the constants.
void SpendingExactlyTheAllowanceHoldsLevel()
{
    ErrandSpend spend;
    time_t now = T0;
    // 420 in 1800 is 7 seconds of errand for every 30 of wall clock.
    for (int i = 0; i < 200; ++i)
    {
        spend = ErrandSpendAfter(spend, now, 7, LIMITS);
        now += 30;
    }
    CheckHeld("spending exactly the allowance", ErrandOverspent(spend, LIMITS), false);
}

// AND IT REFILLS. A held-off character that then quests comes back under the
// line, because a refusal that outlives its reason is its own bug.
void QuestingRefillsTheBudget()
{
    ErrandSpend spend;
    spend = ErrandSpendAfter(spend, T0, LIMITS.spendSeconds, LIMITS);
    CheckHeld("saturated", ErrandOverspent(spend, LIMITS), true);

    // One full window of doing something else drains the whole allowance.
    spend = ErrandSpendAfter(spend, T0 + static_cast<time_t>(LIMITS.windowSeconds), 0,
                             LIMITS);
    CheckSeconds("after a quiet window", spend.seconds, 0);
    CheckHeld("after a quiet window", ErrandOverspent(spend, LIMITS), false);
}

// CLAMPED AT BOTH ENDS. A long absence banks no credit, and a single enormous
// errand cannot fill the bucket past the line.
void TheBucketIsClamped()
{
    ErrandSpend spend;
    spend = ErrandSpendAfter(spend, T0, 60, LIMITS);
    // A whole day away from any counter.
    spend = ErrandSpendAfter(spend, T0 + 24 * 60 * 60, 0, LIMITS);
    CheckSeconds("a day away banks nothing", spend.seconds, 0);

    // ...and the credit is not negative, so the very next errand still counts
    // in full rather than being absorbed.
    spend = ErrandSpendAfter(spend, T0 + 24 * 60 * 60, 90, LIMITS);
    CheckSeconds("first errand after a day", spend.seconds, 90);

    // ...and the debt a single enormous errand can leave is bounded at one
    // spare bucket, so it is cleared by one window of drain and never more.
    ErrandSpend huge;
    huge = ErrandSpendAfter(huge, T0, 10 * 60 * 60, LIMITS);
    CheckSeconds("one enormous errand", huge.seconds, 2 * LIMITS.spendSeconds);
    huge = ErrandSpendAfter(huge, T0 + static_cast<time_t>(LIMITS.windowSeconds), 0,
                            LIMITS);
    CheckSeconds("one window later", huge.seconds, LIMITS.spendSeconds);
    huge = ErrandSpendAfter(huge, T0 + static_cast<time_t>(LIMITS.windowSeconds) + 60, 0,
                            LIMITS);
    CheckHeld("just past one window", ErrandOverspent(huge, LIMITS), false);
}

// A clock that goes backwards, and a hold that comes back negative, are caller
// bugs and not refunds. Neither may hand out spending.
void ABackwardsClockIsNotARefund()
{
    ErrandSpend spend;
    spend = ErrandSpendAfter(spend, T0, 300, LIMITS);
    CheckSeconds("before the clock moves", spend.seconds, 300);

    spend = ErrandSpendAfter(spend, T0 - 5000, 0, LIMITS);
    CheckSeconds("clock went backwards", spend.seconds, 300);

    spend = ErrandSpendAfter(spend, T0 - 5000, -600, LIMITS);
    CheckSeconds("negative hold", spend.seconds, 300);
}

// A window or an allowance of zero is a rule that has been switched off, not a
// rule that divides by zero or refuses everybody.
void DegenerateLimitsRefuseNobody()
{
    ErrandBudgetLimits off;
    off.windowSeconds = 0;
    off.spendSeconds = 0;

    ErrandSpend spend;
    spend = ErrandSpendAfter(spend, T0, 9999, off);
    CheckSeconds("switched off", spend.seconds, 0);
    CheckHeld("switched off", ErrandOverspent(spend, off), false);
}

}  // namespace

int main()
{
    AFreshCharacterIsNeverHeld();
    TheMeasuredLoopIsCaught();
    TheShareIsHeldOverTheLongRun();
    AlternatingRolesDoNotEscape();
    SpendingExactlyTheAllowanceHoldsLevel();
    QuestingRefillsTheBudget();
    TheBucketIsClamped();
    ABackwardsClockIsNotARefund();
    DegenerateLimitsRefuseNobody();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the economy errand budget holds\n");
    return EXIT_SUCCESS;
}
