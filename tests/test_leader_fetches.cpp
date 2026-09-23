/*
 * The leader goes back for a member too far to walk, instead of the family
 * carrying on without it.
 *
 * Measured 2026-09-23 on the dev realm. The too-far-to-walk hold stood
 * followers still more than 1500 yards from their leader ("held still to wait
 * for leader for at most 900s"), and the family regroup then gave up on them
 * ("the family is NOT waiting for ... They are behind and the family carries
 * on"). The follower waited for a leader who walked away: Horde followers left
 * 1,000 to 2,000 yards back in the Barrens while the leader walked on to
 * Orgrimmar, Ragefire runs closed at BARRIER naming 'Oz (1902y out and 81y above
 * it)' and 'Uzza (1906y out ...)', and Alliance followers left in Winterspring
 * while the leader stood in Silithus.
 *
 * These tests pin the decisions that close that gap from the leader's end and
 * keep the going back bounded: which member, when to stop, what a dungeon run
 * does about it, and that the regroup and the party flight both stop treating
 * the fetched member as one the family has left.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::FamilyRegroup;
using OverseerDecisions::FetchCandidate;
using OverseerDecisions::FetchFacts;
using OverseerDecisions::FetchLimits;
using OverseerDecisions::FetchRunAnswer;
using OverseerDecisions::FetchRunPhase;
using OverseerDecisions::FetchStep;
using OverseerDecisions::FetchStepWord;
using OverseerDecisions::MemberFollowsForFlight;
using OverseerDecisions::PickFetchTarget;
using OverseerDecisions::ReadFamilyRegroup;
using OverseerDecisions::ReadFetch;
using OverseerDecisions::ReadRegroupClaim;
using OverseerDecisions::RegroupCarriedOnWithout;
using OverseerDecisions::RegroupClaim;
using OverseerDecisions::RegroupLimits;
using OverseerDecisions::RegroupMember;
using OverseerDecisions::RunLetsTheLeaderFetch;

namespace
{

int failures = 0;

// The adapter's own numbers: CATCH_UP_FOOT_LIMIT_YARDS, FOLLOW_CATCH_UP_YARDS
// and TRAVEL_BACKSTOP_SECONDS.
constexpr FetchLimits LIMITS{1500.f, 500.f, 20 * 60};
constexpr RegroupLimits REGROUP{500.f, 40.f};

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void Same(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void Step(char const* what, FetchStep got, FetchStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, FetchStepWord(got),
                FetchStepWord(want));
    ++failures;
}

FetchCandidate Held(char const* name, float yards)
{
    FetchCandidate member;
    member.name = name;
    member.seen = true;
    member.sameMap = true;
    member.alive = true;
    member.heldTooFar = true;
    member.yards = yards;
    return member;
}

FetchCandidate Beside(char const* name, float yards)
{
    FetchCandidate member = Held(name, yards);
    member.heldTooFar = false;
    return member;
}

FetchFacts Under(float yards, time_t seconds)
{
    FetchFacts facts;
    facts.targetFetchable = true;
    facts.targetYards = yards;
    facts.leaderFree = true;
    facts.fetchingForSeconds = seconds;
    return facts;
}

// THE MEASUREMENT. Two members held about 1,900 yards back, the rest beside the
// leader: he goes back, for the nearer one first.
void TheRagefireBarrierIsFetchedNotClosed()
{
    std::vector<FetchCandidate> const family = {Beside("Zug", 8.f), Beside("Zrog", 12.f),
                                                Held("Uzza", 1906.f), Held("Oz", 1902.f)};
    Same("the nearer of two held members", PickFetchTarget(family, LIMITS, false), "Oz");
}

// (b) Nearest first, and a tie goes to the name so two polls agree.
void TheNearestHeldMemberIsFetchedFirst()
{
    Same("nearest of three",
         PickFetchTarget({Held("Far", 9000.f), Held("Near", 1600.f), Held("Mid", 4000.f)},
                         LIMITS, false),
         "Near");
    Same("a tie goes to the name",
         PickFetchTarget({Held("Uzza", 2000.f), Held("Oz", 2000.f)}, LIMITS, false), "Oz");
}

// Nobody held, nobody fetched: the ordinary family is untouched.
void AFamilyWithNobodyHeldFetchesNobody()
{
    Same("all in formation",
         PickFetchTarget({Beside("a", 5.f), Beside("b", 20.f)}, LIMITS, false), "");
    Same("an empty family", PickFetchTarget({}, LIMITS, false), "");
}

// (c) THE PING-PONG BOUND, FIRST HALF. The leader leaves only a gathered family.
// A member walking back, or the regroup wait holding him for one, keeps him.
void TheLeaderDoesNotLeaveBeforeTheFamilyIsGathered()
{
    Same("the regroup wait is holding him",
         PickFetchTarget({Held("Oz", 1902.f)}, LIMITS, true), "");
    Same("a member still 900 yards back and walking",
         PickFetchTarget({Held("Oz", 1902.f), Beside("Zrog", 900.f)}, LIMITS, false), "");
    Same("at the gathered line exactly",
         PickFetchTarget({Held("Oz", 1902.f), Beside("Zrog", 500.f)}, LIMITS, false), "Oz");
}

// A member on another map, dead or out of the world is nothing a fetch can walk
// to, and nothing still walking back either.
void UnmeasuredMembersNeitherBlockNorAreFetched()
{
    FetchCandidate ghost = Held("Ghost", 2000.f);
    ghost.alive = false;
    FetchCandidate away = Held("Away", 2000.f);
    away.sameMap = false;
    FetchCandidate gone = Beside("Gone", 5000.f);
    gone.seen = false;
    Same("none of them", PickFetchTarget({ghost, away, gone}, LIMITS, false), "");
    Same("and they do not block one that can be fetched",
         PickFetchTarget({ghost, away, gone, Held("Oz", 1902.f)}, LIMITS, false), "Oz");
}

// (c) THE PING-PONG BOUND, SECOND HALF. A member fetched recently is not fetched
// again inside the stand-down, so the leader cannot go back and forth.
void AMemberFetchedRecentlyIsNotFetchedAgain()
{
    FetchCandidate oz = Held("Oz", 1902.f);
    oz.stoodDown = true;
    Same("stood down", PickFetchTarget({oz}, LIMITS, false), "");
    Same("the next one instead", PickFetchTarget({oz, Held("Uzza", 1906.f)}, LIMITS, false),
         "Uzza");
}

// A held member already inside the line is one whose hold lifts this poll.
void AHeldMemberInsideTheLineIsNotFetched()
{
    Same("at the line", PickFetchTarget({Held("Oz", 1500.f)}, LIMITS, false), "");
}

// A FETCH UNDER WAY, poll by poll, from 1,902 yards to the hold lifting.
void TheFetchEndsWhereTheHoldLifts()
{
    Step("1902 yards, one minute in", ReadFetch(Under(1902.f, 60), LIMITS),
         FetchStep::Continue);
    Step("1501 yards", ReadFetch(Under(1501.f, 240), LIMITS), FetchStep::Continue);
    Step("at the line", ReadFetch(Under(1500.f, 260), LIMITS), FetchStep::Arrived);
    Step("well inside", ReadFetch(Under(40.f, 300), LIMITS), FetchStep::Arrived);
}

// (c) The ceiling, and arriving on its last poll counts as arriving.
void TheFetchHasACeiling()
{
    Step("one second short", ReadFetch(Under(1800.f, 20 * 60 - 1), LIMITS),
         FetchStep::Continue);
    Step("at the ceiling", ReadFetch(Under(1800.f, 20 * 60), LIMITS), FetchStep::GiveUp);
    Step("arrived at the ceiling", ReadFetch(Under(1400.f, 20 * 60), LIMITS),
         FetchStep::Arrived);
}

// Anything that makes the fetch meaningless ends it before anything else.
void AFetchWithNothingToServeIsAbandoned()
{
    FetchFacts gone = Under(1800.f, 60);
    gone.targetFetchable = false;
    Step("the member died, left or changed map", ReadFetch(gone, LIMITS),
         FetchStep::Abandon);
    FetchFacts busy = Under(1800.f, 60);
    busy.leaderFree = false;
    Step("the leader was taken by a run or died", ReadFetch(busy, LIMITS),
         FetchStep::Abandon);
    FetchFacts fenced = Under(1800.f, 60);
    fenced.aimRefused = true;
    Step("the leader's column is somebody else's errand", ReadFetch(fenced, LIMITS),
         FetchStep::Abandon);
    FetchFacts late = Under(1400.f, 99999);
    late.leaderFree = false;
    Step("abandon outranks arriving", ReadFetch(late, LIMITS), FetchStep::Abandon);
}

// (a) GATHERING and before lend the leader; BARRIER goes back to GATHERING
// first; from ENTER on the run keeps him.
void ARunLendsTheLeaderWhileItIsAssembling()
{
    Check("no run", RunLetsTheLeaderFetch(FetchRunPhase::NoRun) == FetchRunAnswer::Fetch,
          true);
    Check("RESETTING",
          RunLetsTheLeaderFetch(FetchRunPhase::Resetting) == FetchRunAnswer::Fetch, true);
    Check("GATHERING",
          RunLetsTheLeaderFetch(FetchRunPhase::Gathering) == FetchRunAnswer::Fetch, true);
    Check("BARRIER",
          RunLetsTheLeaderFetch(FetchRunPhase::Barrier) == FetchRunAnswer::RegatherFirst,
          true);
    Check("ENTER onward",
          RunLetsTheLeaderFetch(FetchRunPhase::Committed) == FetchRunAnswer::Refuse, true);
}

RegroupMember HeldBehind(char const* name, float yards, bool fetched)
{
    RegroupMember member;
    member.name = name;
    member.seen = true;
    member.sameMap = true;
    member.alive = true;
    member.yards = yards;
    member.aimRefusedBecause =
        "it is held where it stands, too far from the leader to walk and with no "
        "flight that carries it";
    member.beingFetched = fetched;
    return member;
}

// THE MUTUAL WAIT ITSELF. Held and not fetched, the family carries on without
// it, which is the line the realm printed. Fetched, it is neither waited for
// nor carried on without.
void TheFamilyStopsCarryingOnWithoutTheFetchedMember()
{
    std::vector<RegroupMember> const before = {HeldBehind("Oz", 1902.f, false)};
    Check("before: carried on without",
          RegroupCarriedOnWithout(before, REGROUP, false).empty(), false);

    std::vector<RegroupMember> const after = {HeldBehind("Oz", 1902.f, true)};
    Check("after: the claim names the fetch",
          ReadRegroupClaim(after[0], REGROUP, false) == RegroupClaim::Fetched, true);
    Same("after: nobody carried on without", RegroupCarriedOnWithout(after, REGROUP, false),
         "");
    FamilyRegroup const verdict = ReadFamilyRegroup(after, REGROUP, false);
    Check("after: the leader is not held still for it", verdict.wait, false);
    Check("after: counted as fetched", verdict.fetched == 1, true);
    Check("after: not counted as left", verdict.notWaitedFor == 0, true);
}

// A fetched member that is dead is dead: the fetch is abandoned on its own line,
// and the regroup still says the true thing.
void ADeadFetchedMemberIsStillDead()
{
    RegroupMember ghost = HeldBehind("Oz", 1902.f, true);
    ghost.alive = false;
    Check("dead outranks fetched", ReadRegroupClaim(ghost, REGROUP, false) == RegroupClaim::Dead,
          true);
}

// The leader flies to the fetched member, so it is not a passenger he must wait
// for; every other held member still is.
void TheFetchedMemberIsNotAPassenger()
{
    Check("held and fetched", MemberFollowsForFlight(false, false, false, true, true), false);
    Check("held and not fetched", MemberFollowsForFlight(false, false, false, true, false),
          true);
    Check("an empty column still follows", MemberFollowsForFlight(true, false, false, false, false),
          true);
}

}  // namespace

int main()
{
    TheRagefireBarrierIsFetchedNotClosed();
    TheNearestHeldMemberIsFetchedFirst();
    AFamilyWithNobodyHeldFetchesNobody();
    TheLeaderDoesNotLeaveBeforeTheFamilyIsGathered();
    UnmeasuredMembersNeitherBlockNorAreFetched();
    AMemberFetchedRecentlyIsNotFetchedAgain();
    AHeldMemberInsideTheLineIsNotFetched();
    TheFetchEndsWhereTheHoldLifts();
    TheFetchHasACeiling();
    AFetchWithNothingToServeIsAbandoned();
    ARunLendsTheLeaderWhileItIsAssembling();
    TheFamilyStopsCarryingOnWithoutTheFetchedMember();
    ADeadFetchedMemberIsStillDead();
    TheFetchedMemberIsNotAPassenger();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("leader fetches: all passed\n");
    return EXIT_SUCCESS;
}
