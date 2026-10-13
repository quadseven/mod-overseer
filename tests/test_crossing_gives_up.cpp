/*
 * A campaign crossing that makes no progress hands the dungeon back instead of
 * walking forever (#926, refs wow-overseer#870).
 *
 * Measured: one leader's walk to the Menethil berth was restarted 424 times
 * over 4.7 hours. The travel drive's no-progress backoff refused the berth aim,
 * the IDLE crossing claimed it again on the next poll and the refusal released
 * it again, 131 times in 12 minutes; and the crossing's own half-hour backstop
 * zeroed its clock, so the next poll walked again with a fresh half hour.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::CrossingAttemptLimits;
using OverseerDecisions::CrossingAttemptReading;
using OverseerDecisions::CrossingAttempts;
using OverseerDecisions::CrossingAttemptVerdict;
using OverseerDecisions::CrossingBerthClaim;
using OverseerDecisions::CrossingAttemptEnd;
using OverseerDecisions::CrossingHandedBack;
using OverseerDecisions::CrossingRefusalEnd;
using OverseerDecisions::EndCrossingAttempt;
using OverseerDecisions::NoProgressBackoffReason;
using OverseerDecisions::OpenCrossingAttempt;
using OverseerDecisions::ReadCrossingBerthClaim;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

std::string const DUNGEON = "razorfen kraul";
std::string const BERTH = "at:0:-3724.3,-583.6,4.8";
std::int64_t const START = 1000000;
std::int64_t const MINUTE = 60;

// One attempt: claimed at `yards` from the berth, walked no nearer than
// `nearest`, ended at `endAt`.
CrossingAttemptReading Attempt(CrossingAttempts& record, float yards, float nearest,
                               std::int64_t at, std::int64_t endAt,
                               CrossingAttemptLimits const& limits)
{
    OpenCrossingAttempt(record, DUNGEON, BERTH, yards, at);
    OpenCrossingAttempt(record, DUNGEON, BERTH, nearest, at + MINUTE);
    return EndCrossingAttempt(record, endAt, limits);
}
}  // namespace

int main()
{
    CrossingAttemptLimits const limits;
    Check("two attempts, 100 yards, two hours", limits.attempts == 2 &&
                                                    limits.gainYards == 100.f &&
                                                    limits.cooloffSeconds == 2 * 60 * 60);

    // TWO FAILED ATTEMPTS WITH NO GAIN GIVE UP, WITH THE COOL-OFF.
    {
        CrossingAttempts record;
        CrossingAttemptReading const first =
            Attempt(record, 3100.f, 3080.f, START, START + 4 * MINUTE, limits);
        Check("the first stalled attempt is counted", first.counted && first.attempts == 1);
        Check("the first stalled attempt does not give up",
              first.verdict == CrossingAttemptVerdict::Continue);
        Check("nothing is handed back after one", !CrossingHandedBack(record, DUNGEON,
                                                                      START + 5 * MINUTE));

        std::int64_t const ended = START + 23 * MINUTE;
        CrossingAttemptReading const second =
            Attempt(record, 3085.f, 3070.f, START + 19 * MINUTE, ended, limits);
        Check("the second stalled attempt gives up",
              second.verdict == CrossingAttemptVerdict::GiveUp);
        Check("it says two attempts", second.attempts == 2);
        Check("it carries the two-hour cool-off", second.cooloffSeconds == 2 * 60 * 60);
        Check("it says how long the record ran", second.seconds == 23 * MINUTE);
        Check("the dungeon is handed back for the cool-off",
              CrossingHandedBack(record, DUNGEON, ended + MINUTE) == 2 * 60 * 60 - MINUTE);
        Check("and only that dungeon",
              !CrossingHandedBack(record, "wailing caverns", ended + MINUTE));
        Check("and only until the cool-off ends",
              !CrossingHandedBack(record, DUNGEON, ended + 2 * 60 * 60));

        // AFTER THE COOL-OFF THE COUNT STARTS AGAIN: one more stalled attempt
        // is the first of a new two, not a third that gives up at once.
        CrossingAttemptReading const after = Attempt(record, 3070.f, 3070.f,
                                                     ended + 2 * 60 * 60 + MINUTE,
                                                     ended + 2 * 60 * 60 + 5 * MINUTE, limits);
        Check("after the cool-off one stall does not give up again",
              after.verdict == CrossingAttemptVerdict::Continue && after.attempts == 1);
    }

    // AN ATTEMPT THAT GAINED 100 YARDS ANSWERS CONTINUE.
    {
        CrossingAttempts record;
        Attempt(record, 3100.f, 3090.f, START, START + 4 * MINUTE, limits);
        CrossingAttemptReading const gained =
            Attempt(record, 3090.f, 2990.f, START + 20 * MINUTE, START + 50 * MINUTE, limits);
        Check("an attempt that got 100 yards nearer continues",
              gained.verdict == CrossingAttemptVerdict::Continue);
        Check("and it is not a failure in a row", gained.attempts == 0);
        Check("it says how far it got", gained.gained >= 100.f);
        Check("nothing is handed back", !CrossingHandedBack(record, DUNGEON, START + 51 * MINUTE));

        // The gain is measured from where it got to, not from the start again.
        CrossingAttemptReading const next =
            Attempt(record, 2990.f, 2950.f, START + 51 * MINUTE, START + 55 * MINUTE, limits);
        Check("the next stall is the first of a new two",
              next.verdict == CrossingAttemptVerdict::Continue && next.attempts == 1);
        CrossingAttemptReading const last =
            Attempt(record, 2950.f, 2940.f, START + 70 * MINUTE, START + 74 * MINUTE, limits);
        Check("two stalls after the gain give up",
              last.verdict == CrossingAttemptVerdict::GiveUp);

        // 99 yards is not 100.
        CrossingAttempts close;
        Attempt(close, 3100.f, 3100.f, START, START + 4 * MINUTE, limits);
        Check("99 yards nearer is still no gain",
              Attempt(close, 3100.f, 3001.f, START + 20 * MINUTE, START + 24 * MINUTE, limits)
                      .verdict == CrossingAttemptVerdict::GiveUp);
    }

    // AN OVERDUE REFUSE DOES NOT RESET THE COUNT. The backoff ends the first
    // attempt; the crossing's backstop then reads overdue and refuses, which
    // zeroes its own clock. Neither that end nor the next poll's walk starts
    // the count again.
    {
        CrossingAttempts record;
        Attempt(record, 3100.f, 3090.f, START, START + 4 * MINUTE, limits);
        CrossingAttemptReading const overdue =
            EndCrossingAttempt(record, START + 30 * MINUTE, limits);
        Check("the overdue end of an attempt already ended is not a second failure",
              !overdue.counted && overdue.attempts == 1 &&
                  overdue.verdict == CrossingAttemptVerdict::Continue);
        Check("the record keeps its count across the refusal", record.failed == 1);
        CrossingAttemptReading const again =
            Attempt(record, 3090.f, 3085.f, START + 31 * MINUTE, START + 61 * MINUTE, limits);
        Check("the walk after the overdue refusal is the second attempt, and gives up",
              again.verdict == CrossingAttemptVerdict::GiveUp && again.attempts == 2);

        // An attempt the backstop itself ends counts once, as any other end.
        CrossingAttempts byBackstop;
        Attempt(byBackstop, 3100.f, 3095.f, START, START + 31 * MINUTE, limits);
        Check("an overdue backstop is a failed attempt", byBackstop.failed == 1);
    }

    // ANOTHER DUNGEON OR ANOTHER BERTH IS A NEW QUESTION.
    {
        CrossingAttempts record;
        Attempt(record, 3100.f, 3090.f, START, START + 4 * MINUTE, limits);
        OpenCrossingAttempt(record, "wailing caverns", BERTH, 3000.f, START + 10 * MINUTE);
        Check("another dungeon starts a fresh record",
              record.failed == 0 && record.dungeon == "wailing caverns");
    }

    // WHILE THE TRAVEL DRIVE REFUSES THE BERTH AIM, THE CROSSING CLAIMS NOTHING.
    {
        std::int64_t const backoff = 15 * MINUTE;
        Check("an aim never refused is claimed",
              ReadCrossingBerthClaim(-1, backoff) == CrossingBerthClaim::Claim);
        Check("an aim refused this second is not claimed",
              ReadCrossingBerthClaim(0, backoff) == CrossingBerthClaim::StandBack);
        Check("an aim refused twelve minutes ago is not claimed",
              ReadCrossingBerthClaim(12 * MINUTE, backoff) == CrossingBerthClaim::StandBack);
        Check("an aim whose refusal has run out is claimed again",
              ReadCrossingBerthClaim(backoff, backoff) == CrossingBerthClaim::Claim);
    }

    // THE GATE ITSELF, poll by poll, as the IDLE crossing runs it: a claim
    // opens an attempt; a poll that reads StandBack ends it, by the reason the
    // travel drive refused with, and claims nothing; the polls after it in the
    // same refusal count nothing more; the claim after the refusal has run out
    // is the second attempt.
    {
        Check("the no-progress backoff's own reason reads as the backoff",
              CrossingRefusalEnd(NoProgressBackoffReason) ==
                  CrossingAttemptEnd::NoProgressBackoff);
        Check("a refusal for deaths reads as a plain refusal",
              CrossingRefusalEnd("died 3 times on the way") == CrossingAttemptEnd::Refused);

        std::int64_t const backoff = 15 * MINUTE;
        std::int64_t const refusedAt = START + 4 * MINUTE;
        CrossingAttempts record;
        int claims = 0;
        CrossingAttemptReading last;
        // One poll every five seconds from START until the second refusal
        // has stood back for a minute.
        std::int64_t const secondRefusedAt = refusedAt + backoff + 4 * MINUTE;
        for (std::int64_t now = START; now < secondRefusedAt + MINUTE; now += 5)
        {
            std::int64_t const sinceRefused = now < refusedAt ? -1
                                              : now < secondRefusedAt ? now - refusedAt
                                                                      : now - secondRefusedAt;
            if (ReadCrossingBerthClaim(sinceRefused, backoff) == CrossingBerthClaim::StandBack)
            {
                CrossingAttemptReading const ended = EndCrossingAttempt(record, now, limits);
                if (ended.counted)
                    last = ended;
                continue;
            }
            ++claims;
            OpenCrossingAttempt(record, DUNGEON, BERTH, 3100.f, now);
        }
        Check("no poll claims the berth aim while the travel drive refuses it",
              claims == static_cast<int>((refusedAt - START) / 5 +
                                         (secondRefusedAt - (refusedAt + backoff)) / 5));
        Check("the second refusal gives the dungeon up",
              last.verdict == CrossingAttemptVerdict::GiveUp && last.attempts == 2 &&
                  CrossingHandedBack(record, DUNGEON, secondRefusedAt) == limits.cooloffSeconds);
    }

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_crossing_gives_up: all checks passed\n");
    return 0;
}
