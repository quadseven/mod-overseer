/*
 * A lone member is not walked down a leg that keeps killing it (#697).
 *
 * Measured on the dev realm 2026-09-24. A level 27-28 member behind its family
 * in Ashenvale was walked toward its leader by the catch-up walk and died about
 * 37 times that day, most of them to Ghostpaw Alphas (27-28), Wildthorn Lurkers
 * (28-29) and Searing Infernals (29-30). Each death was followed by the spirit
 * healer and the same walk, at a fresh aim, so the errand death breaker, which
 * judges an errand over its own life, never saw more than one death.
 *
 * The rule judges the LEG: deaths on any walk to the family inside the window,
 * and the level of what stands on the line to the family. A leg that fails
 * either is not walked; the member hearths to the family, waits for the family's
 * hearth regroup, or is held and gone back for.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Step(char const* what, LoneLegVerdict got, LoneLegStep want, LoneLegReason why)
{
    if (got.step == want && got.why == why)
        return;
    std::printf("FAIL %s: got '%s' (%s), wanted '%s' (%s)\n", what, LoneLegStepWord(got.step),
                LoneLegReasonWord(got.why), LoneLegStepWord(want), LoneLegReasonWord(why));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void Count(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

LoneLegLimits const LIMITS{};

// Seconds after 12:00:00 UTC, for readability.
int64_t At(int h, int m, int s)
{
    return (h - 12) * 3600 + m * 60 + s;
}

// The measured leg: the member at the Ashenvale graveyard it kept reviving at,
// its leader at Zoram'gar Outpost, 4,300 yards off, nothing read on the ground.
LoneLegFacts Measured()
{
    LoneLegFacts facts;
    facts.legYards = 4300.f;
    // Its bind was in Durotar, on the same map and thousands of yards away.
    facts.bindYardsFromLeader = 5800.f;
    facts.hearthReady = true;
    return facts;
}

// THE MEASUREMENT. The overseer_death rows at 12:21:55, 12:23:59 and 12:25:35,
// each after a spirit healer and a fresh catch-up aim at the same leader. The
// third death is the one the walk may not follow.
void TheThirdDeathOnTheLegEndsTheWalk()
{
    std::vector<int64_t> deaths{At(12, 21, 55), At(12, 23, 59)};
    LoneLegFacts facts = Measured();
    facts.legDeaths = LoneLegDeathsInWindow(deaths, At(12, 25, 0), LIMITS);
    Count("two deaths in the window", facts.legDeaths, 2);
    Step("twice is not yet more than twice", DecideLoneLeg(facts, LIMITS), LoneLegStep::Walk,
         LoneLegReason::None);

    deaths.push_back(At(12, 25, 35));
    facts.legDeaths = LoneLegDeathsInWindow(deaths, At(12, 26, 0), LIMITS);
    Count("three deaths in the window", facts.legDeaths, 3);
    Step("the third death holds it", DecideLoneLeg(facts, LIMITS), LoneLegStep::Wait,
         LoneLegReason::Deaths);
}

// The deaths are the member's on ANY walk to its family, which is what the
// errand breaker could not count: each of these was a different aim.
void ADeathOutsideTheWindowIsNotCounted()
{
    std::vector<int64_t> deaths{At(12, 0, 0), At(12, 21, 55), At(12, 23, 59)};
    Count("the 12:00 death is 31 minutes old at 12:31",
          LoneLegDeathsInWindow(deaths, At(12, 31, 0), LIMITS), 2);
    Count("and 29 minutes old at 12:29", LoneLegDeathsInWindow(deaths, At(12, 29, 0), LIMITS),
          3);
}

// THE GROUND. The line from the Splintertree landing to the leader crosses
// Astranaar, and a level 28 member reads 209 unbroken yards of guards at level
// 40 there. A party is refused at ten levels over two hundred yards; a lone
// walker at three levels over 120. Five readings of level 31 (150 yards) is
// ground the party rule walks and the lone rule does not.
void GroundWellAboveItsLevelEndsTheWalkBeforeAnyDeath()
{
    RouteReading line;
    line.characterLevel = 28;
    line.sampleSpacingYards = 30.f;
    line.worstLevelAtSample = {0, 29, 30, 31, 31, 31, 31, 31, 30, 0};

    Check("the party rule walks it",
          JudgeRoute(line, RouteLimits{10, 200.f}).survivable, true);
    RouteVerdict const lone = JudgeRoute(line, LoneLegRouteLimits(LIMITS));
    Check("the lone rule does not", lone.survivable, false);

    LoneLegFacts facts = Measured();
    facts.ground = lone;
    Step("ground three levels up, no deaths yet", DecideLoneLeg(facts, LIMITS),
         LoneLegStep::Wait, LoneLegReason::Ground);

    // Same-level wildlife is not "well above": the Ghostpaw Alphas were 27-28.
    line.worstLevelAtSample = {28, 28, 29, 30, 30, 30, 30, 30, 29, 28};
    facts.ground = JudgeRoute(line, LoneLegRouteLimits(LIMITS));
    Step("ground at or near its level", DecideLoneLeg(facts, LIMITS), LoneLegStep::Walk,
         LoneLegReason::None);

    // Three readings of it is ninety yards, which a lone walker may cross.
    line.worstLevelAtSample = {0, 31, 31, 31, 0, 0, 31, 31, 31, 0};
    facts.ground = JudgeRoute(line, LoneLegRouteLimits(LIMITS));
    Step("two short stretches", DecideLoneLeg(facts, LIMITS), LoneLegStep::Walk,
         LoneLegReason::None);
}

// The family's doorstep is not judged: a walk this short is `follow`'s.
void TheDoorstepIsNotJudged()
{
    LoneLegFacts facts = Measured();
    facts.legDeaths = 5;
    facts.legYards = 500.f;
    Step("at the doorstep line", DecideLoneLeg(facts, LIMITS), LoneLegStep::Walk,
         LoneLegReason::None);
    facts.legYards = 501.f;
    Step("one yard past it", DecideLoneLeg(facts, LIMITS), LoneLegStep::Wait,
         LoneLegReason::Deaths);
}

// WHAT IT DOES INSTEAD, in the order a player would.
void TheFamilysInnComesFirstThenItsOwnHearthThenTheWait()
{
    LoneLegFacts facts = Measured();
    facts.legDeaths = 3;

    facts.hearthRegroupInPlay = true;
    Step("the family is meeting at its inn", DecideLoneLeg(facts, LIMITS),
         LoneLegStep::HearthRegroup, LoneLegReason::Deaths);
    facts.hearthRegroupInPlay = false;

    facts.bindYardsFromLeader = 300.f;
    Step("bound beside the family, stone ready", DecideLoneLeg(facts, LIMITS),
         LoneLegStep::Hearth, LoneLegReason::Deaths);
    facts.hearthReady = false;
    Step("bound beside the family, stone on cooldown", DecideLoneLeg(facts, LIMITS),
         LoneLegStep::Wait, LoneLegReason::Deaths);
    facts.hearthReady = true;
    facts.bindYardsFromLeader = -1.f;
    Step("bound on another map", DecideLoneLeg(facts, LIMITS), LoneLegStep::Wait,
         LoneLegReason::Deaths);
    facts.bindYardsFromLeader = 5800.f;
    Step("bound in Durotar, the measured case", DecideLoneLeg(facts, LIMITS),
         LoneLegStep::Wait, LoneLegReason::Deaths);
}

// THE LEADER COMES ALL THE WAY BACK for a member held off a lethal leg. A far
// hold lifts at the foot limit and the member walks the rest; for this member
// the rest is the ground that killed it, so the fetch arrives beside it.
void TheLeaderComesAllTheWayBackForIt()
{
    FetchLimits limits;
    limits.footLimitYards = 1500.f;
    limits.gatheredYards = 500.f;
    limits.ceilingSeconds = 20 * 60;
    limits.lethalArrivalYards = 100.f;

    FetchCandidate member;
    member.name = "Zrog";
    member.seen = true;
    member.sameMap = true;
    member.alive = true;
    member.heldTooFar = true;
    member.yards = 1200.f;

    Check("a far hold inside the foot limit is not gone back for",
          PickFetchTarget({member}, limits, false).empty(), true);
    member.heldOffLethalLeg = true;
    Check("a member held off a lethal leg at 1200 yards is",
          PickFetchTarget({member}, limits, false) == "Zrog", true);
    member.yards = 90.f;
    Check("but not once the leader is beside it",
          PickFetchTarget({member}, limits, false).empty(), true);

    FetchFacts fetch;
    fetch.leaderFree = true;
    fetch.targetFetchable = true;
    fetch.targetYards = 1200.f;
    fetch.fetchingForSeconds = 60;
    Check("a far fetch arrives at the foot limit",
          ReadFetch(fetch, limits) == FetchStep::Arrived, true);
    fetch.targetHeldOffLethalLeg = true;
    Check("a lethal-leg fetch keeps going at 1200 yards",
          ReadFetch(fetch, limits) == FetchStep::Continue, true);
    fetch.targetYards = 90.f;
    Check("and arrives beside it", ReadFetch(fetch, limits) == FetchStep::Arrived, true);

    limits.lethalArrivalYards = 0.f;
    fetch.targetYards = 1200.f;
    Check("an unset arrival reads as the foot limit",
          ReadFetch(fetch, limits) == FetchStep::Arrived, true);
}

// THE FAMILY'S RECOVERY PREFERS ITS INN. A regroup wait holds the leader for a
// member that is walking back, and this one will not walk.
void AFailedRunWithAMemberHeldOffALethalLegMeetsAtTheInn()
{
    RunFailureFacts facts;
    facts.outcome = "staging_failed";
    facts.reason = "the barrier never assembled";
    facts.leaderYardsFromStaging = 200.f;
    facts.farthestMemberYards = 800.f;
    facts.hearthRegroupReady = true;
    Check("an 800 yard straggler is regrouped for",
          RunRecoveryHeuristic(facts) == RunRecovery::Regroup, true);
    facts.memberHeldOffLethalLeg = true;
    Check("unless it is held off a lethal leg",
          RunRecoveryHeuristic(facts) == RunRecovery::HearthRegroup, true);
    facts.hearthRegroupReady = false;
    Check("with no inn to meet at, the ladder is unchanged",
          RunRecoveryHeuristic(facts) == RunRecovery::Regroup, true);
}

// THE LEADER THAT FLEW THE FAMILY TO ASHENVALE. A town leader with no errand
// may not carry `new rpg`, and a flight status upstream rolled for it in the
// gap before that was enforced is cancelled while it is still on the ground.
void AnUnissuedFlightIsCancelledOnATownLeader()
{
    Check("town leader walking to a flight master",
          TownLeaderUnissuedFlight(false, true, false, false) == UnissuedFlightStep::Cancel,
          true);
    Check("already in the air",
          TownLeaderUnissuedFlight(false, true, false, true) == UnissuedFlightStep::Airborne,
          true);
    Check("the module's own flight",
          TownLeaderUnissuedFlight(false, true, true, false) == UnissuedFlightStep::Leave, true);
    Check("a leader that may travel",
          TownLeaderUnissuedFlight(true, true, false, false) == UnissuedFlightStep::Leave, true);
    Check("no flight status at all",
          TownLeaderUnissuedFlight(false, false, false, false) == UnissuedFlightStep::Leave,
          true);
}

}  // namespace

int main()
{
    TheThirdDeathOnTheLegEndsTheWalk();
    ADeathOutsideTheWindowIsNotCounted();
    GroundWellAboveItsLevelEndsTheWalkBeforeAnyDeath();
    TheDoorstepIsNotJudged();
    TheFamilysInnComesFirstThenItsOwnHearthThenTheWait();
    TheLeaderComesAllTheWayBackForIt();
    AFailedRunWithAMemberHeldOffALethalLegMeetsAtTheInn();
    AnUnissuedFlightIsCancelledOnATownLeader();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_lone_leg: all passed\n");
    return EXIT_SUCCESS;
}
