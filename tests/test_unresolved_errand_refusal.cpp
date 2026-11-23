/*
 * A travel-column errand to a spawn that is not on the leader's map is refused
 * for a bounded time, not released and re-picked every poll (#1711).
 *
 * The live failure: the quest drive picked a quest whose giver is a creature
 * spawn on another continent and wrote its bare spawn id to the travel column.
 * The errand resolved the id on the leader's map, found nothing, released as
 * failed in the same second, and the quest drive wrote the same id again.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;
ErrandDeathLimits const kLimits{};

void Expect(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void ExpectNum(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, got, want);
    ++failures;
}

// One poll of the travel drive for a column holding `target`: the same
// ordering the adapter uses. A standing refusal clears the column; otherwise
// the errand resolves. `resolvable` is whether the spawn is on the leader's
// map. Returns true when the leader is sent on the errand.
struct Leader
{
    int64_t refusedAt{-1};
    int sends{0};
    int unresolvedReleases{0};
};

bool Poll(Leader& leader, std::string const& target, bool resolvable, int64_t now)
{
    ErrandDeathToll toll;
    toll.sinceRefused = leader.refusedAt < 0 ? -1 : now - leader.refusedAt;
    ErrandDeathVerdict const verdict = ErrandDeathBreaker(toll, kLimits);
    if (verdict.remedy == ErrandDeathRemedy::RefuseReissue)
        return false;  // column cleared, the leader is free for another intent
    if (resolvable)
    {
        ++leader.sends;
        return true;
    }
    UnresolvedErrandCallOff const call = ReadUnresolvedErrand(target, false, 1);
    ++leader.unresolvedReleases;
    if (call.refuse)
        leader.refusedAt = now;
    return false;
}
}  // namespace

int main()
{
    // A bare spawn id is refused, for a bound, with a reason that names the map.
    UnresolvedErrandCallOff const id = ReadUnresolvedErrand("16227", false, 530);
    Expect("a creature id is refused", id.refuse, true);
    ExpectNum("the bound is the errand cool-off", id.boundSeconds,
              kLimits.cooloffSeconds);
    Expect("the reason names the map", id.reason.find("530") != std::string::npos, true);
    Expect("the reason names the bound in minutes",
           id.reason.find(std::to_string(id.boundSeconds / 60) + " minutes") != std::string::npos,
           true);

    // What is not a bare spawn id is not this rule's: a keyword aim keeps its
    // old release, a point is the poison-aim breaker's, a guard is never ended.
    Expect("a keyword is not refused", ReadUnresolvedErrand("auctioneer", false, 1).refuse, false);
    Expect("a point is not refused", ReadUnresolvedErrand("at:36:1,2,3", false, 1).refuse, false);
    Expect("a guard is not refused", ReadUnresolvedErrand("16227", true, 1).refuse, false);
    Expect("an empty aim is not refused", ReadUnresolvedErrand("", false, 1).refuse, false);

    // ACCEPTANCE: repeated polls with the quest drive writing the same id every
    // five seconds. The leader is never sent, and the unresolved release
    // happens once per bound, not once per poll.
    {
        Leader leader;
        int64_t const bound = kLimits.cooloffSeconds;
        for (int64_t t = 0; t < 2 * bound; t += 5)
            Poll(leader, "16227", false, t);
        ExpectNum("never sent to an unresolvable errand", leader.sends, 0);
        ExpectNum("released as unresolved once per bound", leader.unresolvedReleases, 2);
    }

    // ACCEPTANCE: a spawn on the leader's own map still resolves and walks.
    {
        Leader leader;
        Expect("a resolvable spawn is sent", Poll(leader, "16227", true, 0), true);
        ExpectNum("and counted once", leader.sends, 1);
    }

    if (failures)
        return EXIT_FAILURE;
    std::puts("ok");
    return EXIT_SUCCESS;
}
