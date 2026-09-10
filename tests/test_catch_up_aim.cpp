/*
 * When is a catching-up follower's aim worth rewriting?
 *
 * The live failure this pins is three consecutive party polls, thirty seconds
 * apart, each of which sent one follower somewhere different:
 *
 *   01:25:14 'Og' is sent to 'at:1:1972.61,-761.447,98.0276' ... 2248 yards of
 *            walking legs and 410 waypoints
 *   01:25:44 'Og' is sent to 'at:1:1973.41,-706.982,109.223' ... 3108 yards of
 *            walking legs and 563 waypoints
 *   01:26:44 'Og' is sent to 'at:1:1953.2,-593.484,112.2'    ... 2248 yards of
 *            walking legs and 410 waypoints
 *
 * The aim is the leader's live position and the leader was still grinding, so
 * every poll re-aimed. Every re-aim erased the errand record, and the surveyed
 * route lives in that record: 400 to 560 waypoints discarded and replanned, and
 * with them the cursor showing how far along the route the follower had got,
 * the twenty-minute unreachable backstop clock, the per-errand flight budget,
 * and the footing refusal bound whose own comment says it is deliberately NOT
 * cleared with the errand.
 *
 * Between the first and the last of those three lines the leader moved 169
 * yards. Under the rule this file tests, that is not a new destination: the
 * survey plans the same route to a point that has moved less than its own
 * resolution. Zero re-aims where there were three.
 *
 * The tests that matter most are the last two sections. A follower must never
 * be left parked at a point the leader has walked away from, and the near
 * regime must be byte-for-byte the rule that was there before, because that is
 * the regime the walk actually ends in.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::CatchUpAimFacts;
using OverseerDecisions::CatchUpAimIsStale;
using OverseerDecisions::CatchUpAimLimits;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// The live constants. FOLLOW_CATCH_UP_REAIM_YARDS is half of the hand-back
// distance; TRAVEL_ROUTE_MIN_YARDS is the length under which no surveyed route
// is planned at all; TRAVEL_ROUTE_GAIN_YARDS is how much nearer a surveyed
// route has to finish before it is worth walking, which is the survey's own
// resolution. None of the three is introduced by this rule.
CatchUpAimLimits const LIVE{50.f, 400.f, 200.f};

CatchUpAimFacts At(float gap, float drift)
{
    CatchUpAimFacts facts;
    facts.leaderOnTheGround = true;
    facts.followerGapToLeader = gap;
    facts.leaderDriftFromAim = drift;
    return facts;
}

// THE INCIDENT. The three aims above, with the distances the log reports.
void TheThreePollsThatEachDiscardedARoute()
{
    // 01:25:14 -> 01:25:44. The leader moved from (1972.61, -761.447) to
    // (1973.41, -706.982): 54.5 yards. The follower was 2,136 yards out.
    Check("54 yards at 2,136 out is not a new destination",
          CatchUpAimIsStale(At(2136.f, 54.5f), LIVE), false);

    // 01:25:44 -> 01:26:44. (1973.41, -706.982) to (1953.2, -593.484): 115.3
    // yards, with the follower 2,089 yards out.
    Check("115 yards at 2,089 out is not a new destination either",
          CatchUpAimIsStale(At(2089.f, 115.3f), LIVE), false);

    // And the whole window, first aim to last: 169 yards.
    Check("nor the whole ninety seconds of it at once",
          CatchUpAimIsStale(At(2136.f, 169.f), LIVE), false);

    // The old rule fired on all three, which is what the log shows.
    Check("the fifty yard rule fired on the first", 54.5f > LIVE.reaimYards, true);
    Check("and the second", 115.3f > LIVE.reaimYards, true);
    Check("and the third", 169.f > LIVE.reaimYards, true);
}

// A LEADER THAT HAS GENUINELY GONE SOMEWHERE ELSE IS STILL FOLLOWED. The rule
// damps the aim, it does not freeze it.
void ALeaderThatWalksPastTheSurveysResolutionIsReAimedAt()
{
    Check("two hundred yards is the line", CatchUpAimIsStale(At(2136.f, 200.f), LIVE),
          false);
    Check("and just past it is a new destination",
          CatchUpAimIsStale(At(2136.f, 200.1f), LIVE), true);
    Check("a leader that has crossed a zone is certainly one",
          CatchUpAimIsStale(At(2136.f, 1500.f), LIVE), true);
}

// ============================================================================
// THE NEAR REGIME IS THE OLD RULE, UNCHANGED. This is the half that keeps the
// walk able to end: an aim within fifty yards of the leader is one a follower
// can arrive at and still be inside the hand-back distance.
// ============================================================================

void InsideTheRouteLengthTheOldRuleIsUntouched()
{
    float const near[] = {0.f, 1.f, 99.f, 100.f, 250.f, 399.9f, 400.f};
    float const drifts[] = {0.f, 1.f, 49.9f, 50.f, 50.1f, 99.f, 200.f, 201.f, 1000.f};
    for (float gap : near)
        for (float drift : drifts)
            Check("inside the route length the answer is the fifty yard rule",
                  CatchUpAimIsStale(At(gap, drift), LIVE), drift > LIVE.reaimYards);
}

void TheRegimeChangesExactlyAtTheRouteLength()
{
    // A drift of 100 yards: acted on inside the route length, ignored outside
    // it. That one number either side of the line is the whole change.
    Check("100 yards of drift at 400 out is a re-aim",
          CatchUpAimIsStale(At(400.f, 100.f), LIVE), true);
    Check("the same 100 yards at 400.1 out is not",
          CatchUpAimIsStale(At(400.1f, 100.f), LIVE), false);
}

// ============================================================================
// THE PARKING PROOF. The looser allowance must not be able to leave a follower
// standing at a point the leader has walked away from. It cannot, because the
// allowance is capped rather than scaled: an aim is never more than the
// survey's resolution from the leader, and a follower that walks all the way to
// one is therefore inside the route length, which is the near regime.
// ============================================================================

void AFollowerCanNeverBeParkedOutOfReach()
{
    // The worst case the loose regime can produce: the leader stops just inside
    // the allowance and the follower walks the entire way to the stale aim.
    float const worstStaleness = LIVE.routeGainYards;  // 200
    Check("the worst stale aim is inside the route length",
          worstStaleness < LIVE.routeYards, true);
    // Arriving at it leaves the follower exactly that far out, which is the
    // near regime...
    Check("...so the next poll is judged by the fifty yard rule",
          worstStaleness <= LIVE.routeYards, true);
    // ...where that same drift is past the line and the aim is rewritten.
    Check("and that rule rewrites the aim",
          CatchUpAimIsStale(At(worstStaleness, worstStaleness), LIVE), true);
}

// THE PARKING CASE, WALKED. The worst thing the loose allowance can do is let
// a leader stop just inside it: 199 yards away, and then never move again. A
// rule that froze there would leave the follower standing 199 yards short of a
// hand-back that fires at 100, forever. It must not, and this walks it.
void ALeaderThatStopsJustInsideTheAllowanceIsStillReached()
{
    float const handBack = 100.f;  // FOLLOW_CATCH_UP_DONE_YARDS
    float gap = 2000.f;
    // The leader walks 199 yards on the first poll and then stands still. The
    // aim is left 199 yards behind him and nothing will ever move it again
    // except the follower getting close enough to change the regime.
    float aimStaleness = 199.f;
    int reaims = 0;
    int polls = 0;

    for (; polls < 500; ++polls)
    {
        if (gap <= handBack)
            break;
        if (CatchUpAimIsStale(At(gap, aimStaleness), LIVE))
        {
            ++reaims;
            aimStaleness = 0.f;  // the aim is the leader's own position again
        }
        // The follower walks toward the aim, and cannot pass it: its distance to
        // the aim is the gap less the staleness. When that runs out it is
        // standing on a stale point, which is exactly the parking case.
        float const toTheAim = gap - aimStaleness;
        float const walked = toTheAim < 80.f ? toTheAim : (toTheAim > 0.f ? 80.f : 0.f);
        gap -= walked;
        if (gap < 0.f)
            gap = 0.f;
    }

    Check("the follower is not left parked short of the hand-back", gap <= handBack,
          true);
    Check("and gets there in a bounded number of polls", polls < 500, true);
    // Exactly one re-aim was needed: the one the regime change forces when the
    // follower reaches the stale point. Zero would be the parked case, and more
    // than one would mean the leader moved, which it did not.
    Check("and it takes exactly one re-aim to close it", reaims == 1, true);
}

// The same walk with a leader that keeps grinding at the pace the log measured:
// 169 yards over the three polls in the trace, so about 56 a poll. The point is
// the count. The old rule re-aims on every poll of a walk this long, because 56
// is past its fifty yard line; this one re-aims about a quarter as often, and
// the follower still arrives.
void AMovingLeaderCostsFarFewerReAims()
{
    float const handBack = 100.f;
    float const leaderPace = 56.f;    // 169 yards over three polls, from the log
    float const followerPace = 136.f; // the follower gains on the leader
    float gap = 2101.f;
    float aimStaleness = 0.f;
    float oldStaleness = 0.f;  // the rule this replaces, keeping its own aim
    int reaims = 0;
    int oldReaims = 0;
    int polls = 0;

    for (; polls < 500; ++polls)
    {
        if (gap <= handBack)
            break;
        if (CatchUpAimIsStale(At(gap, aimStaleness), LIVE))
        {
            ++reaims;
            aimStaleness = 0.f;
        }
        // What the rule this replaces would have done, poll for poll, with its
        // own aim: re-aim whenever the leader has walked past fifty yards.
        if (oldStaleness > LIVE.reaimYards)
        {
            ++oldReaims;
            oldStaleness = 0.f;
        }
        aimStaleness += leaderPace;
        oldStaleness += leaderPace;
        gap -= (followerPace - leaderPace);
        if (gap < 0.f)
            gap = 0.f;
    }

    Check("the follower still arrives", gap <= handBack, true);
    Check("the old rule re-aimed on all but the first poll of the walk",
          oldReaims >= polls - 1, true);
    // A FRACTION AS OFTEN, AND THE LAST FEW POLLS ARE WHY IT IS NOT FEWER
    // STILL. Once the follower is inside TRAVEL_ROUTE_MIN_YARDS this rule IS
    // the old one, deliberately, so the tail of every walk re-aims exactly as
    // before. That tail is where the walk ends and is the part that must not
    // change; the long haul in front of it is the part that was costing a route
    // a poll.
    Check("and this one re-aims a fraction as often", reaims * 2 < oldReaims, true);
    Check("but it does re-aim, because the leader really did move", reaims > 0,
          true);
    // Each of those old re-aims is a surveyed route of 400 to 560 waypoints
    // discarded and replanned, so the count is the cost.
    Check("which is that many routes not thrown away", oldReaims - reaims > 10,
          true);
}

// ============================================================================
// A LEADER IN THE AIR IS NOT AN AIM, which is unchanged and is asked first.
// ============================================================================

void ALeaderInTheAirIsNeverAimedAt()
{
    float const gaps[] = {0.f, 100.f, 400.f, 2101.f, 1e6f};
    float const drifts[] = {0.f, 50.f, 200.f, 5000.f, 1e6f};
    for (float gap : gaps)
        for (float drift : drifts)
        {
            CatchUpAimFacts facts = At(gap, drift);
            facts.leaderOnTheGround = false;
            Check("a leader off the ground is never re-aimed at",
                  CatchUpAimIsStale(facts, LIVE), false);
        }
}

// THE INVARIANT: whatever the numbers, the answer is never looser than the
// survey's own resolution. No combination of readings can leave an aim more
// than TRAVEL_ROUTE_GAIN_YARDS stale without this saying so.
void NoReadingEverAllowsAnAimStalerThanTheSurveysResolution()
{
    float const gaps[] = {0.f, 1.f, 99.f, 400.f, 401.f, 2101.f, 1e5f, 1e6f};
    float const drifts[] = {200.1f, 201.f, 500.f, 1e4f, 1e6f};
    for (float gap : gaps)
        for (float drift : drifts)
            Check("past the survey's resolution the aim is always stale",
                  CatchUpAimIsStale(At(gap, drift), LIVE), true);

    // ...and under the fifty yard line it is never stale, at any gap. Both
    // bounds hold for every reading, so the rule is sandwiched between the two
    // constants it is written from and cannot escape either.
    float const small[] = {0.f, 1.f, 25.f, 49.9f, 50.f};
    for (float gap : gaps)
        for (float drift : small)
            Check("under the fifty yard line the aim is never stale",
                  CatchUpAimIsStale(At(gap, drift), LIVE), false);
}

// A NEGATIVE GAP CANNOT HAPPEN AND MUST NOT MISBEHAVE IF IT DOES. It reads as
// the near regime, which is the conservative direction: re-aim more often, not
// less.
void AnImpossibleReadingFallsToTheStricterRule()
{
    Check("a negative gap is judged by the fifty yard rule",
          CatchUpAimIsStale(At(-1.f, 60.f), LIVE), true);
    Check("and still refuses a small drift",
          CatchUpAimIsStale(At(-1.f, 10.f), LIVE), false);
}

}  // namespace

int main()
{
    TheThreePollsThatEachDiscardedARoute();
    ALeaderThatWalksPastTheSurveysResolutionIsReAimedAt();
    InsideTheRouteLengthTheOldRuleIsUntouched();
    TheRegimeChangesExactlyAtTheRouteLength();
    AFollowerCanNeverBeParkedOutOfReach();
    ALeaderThatStopsJustInsideTheAllowanceIsStillReached();
    AMovingLeaderCostsFarFewerReAims();
    ALeaderInTheAirIsNeverAimedAt();
    NoReadingEverAllowsAnAimStalerThanTheSurveysResolution();
    AnImpossibleReadingFallsToTheStricterRule();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("an aim is rewritten when the destination moved, not when the leader did\n");
    return EXIT_SUCCESS;
}
