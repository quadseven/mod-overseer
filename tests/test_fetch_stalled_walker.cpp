/*
 * A member that is "walking back" but has not moved does not stop the leader
 * going back for the members that are held.
 *
 * The wedge: three members stood far from their leader on the same map. The
 * far-hold rule widens the walk a held follower is allowed to try, so members
 * inside the widened line were no longer held and were counted as "still
 * walking back"; the fetch refuses to start while anybody is walking back, so
 * the family can gather first. But those members were not walking: the follow
 * drive had logged that they had not moved ten yards in over five minutes.
 * Nobody was ever fetched, and the held members behind them waited for ever.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::FetchCandidate;
using OverseerDecisions::FetchLimits;
using OverseerDecisions::PickFetchTarget;

namespace
{
int failures = 0;

void Check(char const* what, bool holds)
{
    if (holds)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// The adapter's own numbers: the foot limit, the gathering line, the ceiling.
constexpr FetchLimits LIMITS{1500.f, 500.f, 20 * 60};

FetchCandidate Member(char const* name, float yards, bool held, bool stalled)
{
    FetchCandidate member;
    member.name = name;
    member.seen = true;
    member.sameMap = true;
    member.alive = true;
    member.heldTooFar = held;
    member.stalledWalkingBack = stalled;
    member.yards = yards;
    return member;
}
} // namespace

int main()
{
    // One member walking back and moving: the family gathers first, as before.
    Check("a member really walking back still holds the fetch",
          PickFetchTarget({Member("a", 3000.f, false, false), Member("b", 6400.f, true, false)},
                          LIMITS, false)
              .empty());

    // The same member has not moved for a whole stall window: it is not
    // gathering, so the fetch starts, nearest first. Before, it returned nobody
    // and the held member behind it waited for ever.
    Check("a stalled walker does not hold the fetch",
          PickFetchTarget({Member("a", 3000.f, false, true), Member("b", 6400.f, true, false)},
                          LIMITS, false) == "a");

    // Three members, none held, all stalled: the nearest one far enough to
    // need it is fetched, so the leader goes back instead of waiting.
    Check("three stalled walkers are fetched nearest first",
          PickFetchTarget({Member("a", 5800.f, false, true), Member("b", 5900.f, false, true),
                           Member("c", 5400.f, false, true)},
                          LIMITS, false) == "c");

    // A stalled walker inside the foot limit is left to its own walk.
    Check("a stalled walker inside the foot limit is not fetched",
          PickFetchTarget({Member("a", 1000.f, false, true)}, LIMITS, false).empty());

    // The regroup wait still owns the leader while it is under way.
    Check("a regroup wait in progress still refuses a fetch",
          PickFetchTarget({Member("a", 5000.f, false, true)}, LIMITS, true).empty());

    // A held member is unchanged by the new field.
    Check("a held member is fetched as before",
          PickFetchTarget({Member("a", 6400.f, true, false)}, LIMITS, false) == "a");

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
