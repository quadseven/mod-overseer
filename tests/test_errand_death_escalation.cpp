/*
 * mod-overseer#776: an errand that keeps killing the party is refused for
 * longer each time, and the party's bodies count, not only the leader's.
 *
 * Measured on wow-dev 2026-09-27, `overseer_death` for the Alliance family:
 * the walk to the Stormwind auctioneer was released at 20:26 after three of
 * Grug's deaths to Crushridge Maulers, refused for fifteen minutes, sent again
 * at 20:53 over 3,856 yards, and released again. Between 20:56 and 21:00 Ugga
 * died four times to Blackrock Scouts in Redridge on that same errand while
 * the leader's own count stayed under three. About sixty deaths on
 * `town run` between 20:00 and 22:00.
 *
 * Compiled without AzerothCore so the rule stays a pure decision.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ErrandCooloffSeconds;
using OverseerDecisions::ErrandDeathBreaker;
using OverseerDecisions::ErrandDeathLimits;
using OverseerDecisions::ErrandDeathRemedy;
using OverseerDecisions::ErrandDeathToll;

namespace
{

int failures = 0;

void Check(char const* what, long got, long want)
{
    if (got != want)
    {
        std::printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        ++failures;
    }
}

void TheSecondReleaseOfOneTargetIsRefusedForTwiceAsLong()
{
    ErrandDeathLimits const limits;
    Check("first release", long(ErrandCooloffSeconds(1, limits)), 15 * 60);
    Check("no strike reads as the first", long(ErrandCooloffSeconds(0, limits)), 15 * 60);
    Check("second release in a row", long(ErrandCooloffSeconds(2, limits)), 30 * 60);
    Check("third", long(ErrandCooloffSeconds(3, limits)), 60 * 60);
    Check("capped at four hours", long(ErrandCooloffSeconds(9, limits)), 4 * 60 * 60);
}

void The2053ReissueIsStillRefused()
{
    // Released at 20:26 for the second time in a row; re-aimed 27 minutes later.
    ErrandDeathLimits const limits;
    ErrandDeathToll toll;
    toll.sinceRefused = 27 * 60;
    toll.strikes = 2;
    auto const verdict = ErrandDeathBreaker(toll, limits);
    Check("a second strike still refuses at 27 minutes", int(verdict.remedy),
          int(ErrandDeathRemedy::RefuseReissue));
    Check("and says how long is left", long(verdict.coolOffRemaining), 3 * 60);

    toll.strikes = 1;
    Check("one strike has run out by then", int(ErrandDeathBreaker(toll, limits).remedy),
          int(ErrandDeathRemedy::Continue));
}

void TheFollowersBodiesCallTheErrandOff()
{
    ErrandDeathLimits const limits;
    ErrandDeathToll toll;
    toll.deaths = 1;         // Grug
    toll.partyDeaths = 5;    // Grug plus Ugga's four in Redridge
    Check("five party deaths release it", int(ErrandDeathBreaker(toll, limits).remedy),
          int(ErrandDeathRemedy::Release));

    toll.partyDeaths = 4;
    Check("four do not", int(ErrandDeathBreaker(toll, limits).remedy),
          int(ErrandDeathRemedy::Continue));
}

void ARunsOwnAimIsStillOnlyDeclined()
{
    ErrandDeathLimits const limits;
    ErrandDeathToll toll;
    toll.runOwned = true;
    toll.partyDeaths = 9;
    Check("party deaths do not release a run's claim",
          int(ErrandDeathBreaker(toll, limits).remedy), int(ErrandDeathRemedy::Continue));
}

}  // namespace

int main()
{
    TheSecondReleaseOfOneTargetIsRefusedForTwiceAsLong();
    The2053ReissueIsStillRefused();
    TheFollowersBodiesCallTheErrandOff();
    ARunsOwnAimIsStillOnlyDeclined();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the walk that killed the family twice is refused for longer, and the "
                "followers' bodies count\n");
    return EXIT_SUCCESS;
}
