/*
 * A refused crossing stays refused for 60 minutes, or until the leader changes
 * map (#1143).
 *
 * Measured on the dev realm 2026-11-15: two family stand-in guests stayed on
 * map 1 for 207 and 79 minutes with their leader on map 0, and the way across
 * flipped between `sail` and `refuse` every 15 minutes. The refusal stood down
 * for 15 minutes only, so the crossing was priced as servable again, started,
 * and refused again at the same leg. This test reproduces the flip over a long
 * split: with the old 15 minute stand-down the way is `sail` at every other
 * quarter hour; with the committed refusal it is `refuse` until the bound.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <string>

using OverseerDecisions::DecideStrandedWay;
using OverseerDecisions::LONE_CROSSING_REFUSAL_SECONDS;
using OverseerDecisions::RefusedCrossingStands;
using OverseerDecisions::StrandedFacts;
using OverseerDecisions::StrandedWay;
using OverseerDecisions::StrandedWayName;

namespace
{
int failures = 0;
int checks = 0;

void Check(char const* what, bool holds)
{
    ++checks;
    if (holds)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckWay(char const* what, StrandedWay got, StrandedWay want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, StrandedWayName(got),
                StrandedWayName(want));
    ++failures;
}

// What the party poll does: price the crossing unless a refusal stands, then
// decide. `refusedAt` of -1 means no refusal on record.
StrandedWay PollAt(std::int64_t now, std::int64_t refusedAt, std::uint32_t refusedFor,
                   std::uint32_t leaderMap)
{
    StrandedFacts f;
    f.readable = true;
    f.alive = true;
    f.memberMap = 1;
    f.leaderMap = leaderMap;
    f.boundOnMemberMap = true;
    bool const standing =
        refusedAt >= 0 && RefusedCrossingStands(now, refusedAt, refusedFor, leaderMap);
    // The boat serves on paper; only the refusal keeps it from being priced.
    f.transportServes = !standing;
    return DecideStrandedWay(f);
}
}  // namespace

int main()
{
    std::int64_t const minute = 60;
    std::int64_t const refusedAt = 1000;

    Check("the bound is 60 minutes", LONE_CROSSING_REFUSAL_SECONDS == 60 * minute);

    // THE FLIP: the old rule priced it again at 15 minutes.
    CheckWay("refused just now", PollAt(refusedAt, refusedAt, 0, 0), StrandedWay::Refuse);
    CheckWay("15 minutes on is still refused (the flip)",
             PollAt(refusedAt + 15 * minute, refusedAt, 0, 0), StrandedWay::Refuse);
    CheckWay("30 minutes on is still refused", PollAt(refusedAt + 30 * minute, refusedAt, 0, 0),
             StrandedWay::Refuse);
    CheckWay("59 minutes on is still refused", PollAt(refusedAt + 59 * minute, refusedAt, 0, 0),
             StrandedWay::Refuse);

    // THE BOUND: after 60 minutes it is priced again.
    CheckWay("60 minutes on is priced again", PollAt(refusedAt + 60 * minute, refusedAt, 0, 0),
             StrandedWay::Sail);

    // A LEADER ON ANOTHER MAP is another crossing.
    CheckWay("a leader on another map is priced at once",
             PollAt(refusedAt + 5 * minute, refusedAt, 0, 530), StrandedWay::Sail);
    Check("the refusal does not stand for another leader map",
          !RefusedCrossingStands(refusedAt + minute, refusedAt, 0, 530));

    // NO REFUSAL ON RECORD prices as ever.
    CheckWay("no refusal prices as ever", PollAt(refusedAt, -1, 0, 0), StrandedWay::Sail);

    // A clock that ran backwards does not hold a refusal for ever.
    Check("a clock behind the refusal does not stand",
          !RefusedCrossingStands(refusedAt - 1, refusedAt, 0, 0));

    // The whole split: every quarter hour for 3 hours, the way changes only
    // at the bound, never back and forth inside it.
    int flips = 0;
    StrandedWay last = PollAt(refusedAt, refusedAt, 0, 0);
    for (int q = 1; q <= 3 * 4; ++q)
    {
        std::int64_t const now = refusedAt + q * 15 * minute;
        // A crossing that starts is refused again at the same leg and recorded.
        StrandedWay const way = PollAt(now, refusedAt, 0, 0);
        if (way != last)
            ++flips;
        last = way;
        if (q == 4)
            Check("the way is priced again at the 60 minute bound", way == StrandedWay::Sail);
        if (q < 4)
            Check("the way holds inside the bound", way == StrandedWay::Refuse);
    }
    Check("one change of way at the bound, not one per quarter hour", flips == 1);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
