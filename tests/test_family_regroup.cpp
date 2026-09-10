/*
 * Does the family wait for the member it left behind, and can that wait ever
 * fail to end?
 *
 * The live failure this pins is an experience gap rather than a stuck walk.
 * Four of five characters were within fifteen yards of the leader and the fifth
 * was 2,101 yards back, and over one window:
 *
 *   Bork  16588 -> 27165   +10577
 *   Grug   2737 ->  9949    +7212
 *   Ugga   6657 -> 12477    +5820
 *   Grog  levelled 31 -> 32
 *   Og      621 ->  1531     +910
 *
 * The one left behind earned an order of magnitude less than the group, purely
 * from being behind. A dungeon run already refuses to proceed while the roster
 * is apart; ordinary activity had no equivalent, so the leader ground its way
 * across a zone while a member could not catch it.
 *
 * The half of these tests that matters most is the second half. A hold that
 * never releases is worse than the bug it fixes, so every reason a member
 * cannot be waited for is asserted here by name, and the last section walks a
 * whole 2,000 yard rejoin poll by poll to show that the wait both starts and
 * stops.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::FamilyRegroup;
using OverseerDecisions::ReadFamilyRegroup;
using OverseerDecisions::ReadRegroupClaim;
using OverseerDecisions::RegroupCarriedOnWithout;
using OverseerDecisions::RegroupClaim;
using OverseerDecisions::RegroupClaimIsWaitedFor;
using OverseerDecisions::RegroupClaimName;
using OverseerDecisions::RegroupLimits;
using OverseerDecisions::RegroupMember;

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

void CheckClaim(char const* what, RegroupClaim got, RegroupClaim want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RegroupClaimName(got),
                RegroupClaimName(want));
    ++failures;
}

void CheckName(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// The live constants: FOLLOW_CATCH_UP_YARDS is the distance past which the
// catch-up walk starts, and FOLLOW_CATCH_UP_DONE_YARDS is where that walk hands
// back to `follow`. Neither is introduced by this rule.
RegroupLimits const LIVE{500.f, 100.f};

// A member that asks nothing of anybody: present, on the map, alive, near.
RegroupMember Near(char const* name, float yards)
{
    RegroupMember member;
    member.name = name;
    member.seen = true;
    member.sameMap = true;
    member.alive = true;
    member.yards = yards;
    return member;
}

// THE INCIDENT, as the party poll would have read it. Three at the leader's
// elbow, one at 2,101 yards.
std::vector<RegroupMember> TheNightItHappened()
{
    return {Near("one", 12.f), Near("two", 9.f), Near("three", 15.f),
            Near("behind", 2101.f)};
}

void TheFamilyWaitsForTheMemberItLeftBehind()
{
    FamilyRegroup const verdict = ReadFamilyRegroup(TheNightItHappened(), LIVE, false);
    Check("2,101 yards back is a family that waits", verdict.wait, true);
    CheckName("and it says which member it is waiting for", verdict.waitingFor,
              "behind");
    Check("and how far back that member is", verdict.worstYards == 2101.f, true);
    Check("nobody is being carried on without", verdict.notWaitedFor == 0u, true);
    CheckName("so there is nothing to report about carrying on",
              RegroupCarriedOnWithout(TheNightItHappened(), LIVE, false), "");
}

void AFamilyStandingTogetherWaitsForNobody()
{
    std::vector<RegroupMember> const together = {Near("one", 12.f), Near("two", 9.f),
                                                Near("three", 15.f), Near("four", 3.f)};
    FamilyRegroup const verdict = ReadFamilyRegroup(together, LIVE, false);
    Check("a family in formation does not wait", verdict.wait, false);
    CheckName("and names nobody", verdict.waitingFor, "");
    Check("and carries on without nobody", verdict.notWaitedFor == 0u, true);
}

// THE TWO LINES ARE THE FOLLOW GAP'S OWN AND BOTH ARE INCLUSIVE AT THE NEAR
// SIDE, so a member sitting exactly on one does not flap between two verdicts.
void TheLinesAreWhereTheFollowGapPutThem()
{
    CheckClaim("just inside the split line", ReadRegroupClaim(Near("m", 499.9f), LIVE, false),
               RegroupClaim::InFormation);
    CheckClaim("exactly on it", ReadRegroupClaim(Near("m", 500.f), LIVE, false),
               RegroupClaim::InFormation);
    CheckClaim("just past it", ReadRegroupClaim(Near("m", 500.1f), LIVE, false),
               RegroupClaim::Rejoining);
    CheckClaim("standing on the leader", ReadRegroupClaim(Near("m", 0.f), LIVE, false),
               RegroupClaim::InFormation);
}

// THE HYSTERESIS, WHICH IS THE WHOLE REASON THERE ARE TWO NUMBERS. Waiting
// begins at 500 and ends at 100, so a member sitting at 300 yards cannot make
// the family start waiting and cannot make it stop either.
void WaitingStartsAtOneLineAndEndsAtTheOther()
{
    CheckClaim("300 yards does not start a wait",
               ReadRegroupClaim(Near("m", 300.f), LIVE, false), RegroupClaim::InFormation);
    CheckClaim("but 300 yards does not end one either",
               ReadRegroupClaim(Near("m", 300.f), LIVE, true), RegroupClaim::Rejoining);
    CheckClaim("the wait ends at the hand-back distance",
               ReadRegroupClaim(Near("m", 100.f), LIVE, true), RegroupClaim::InFormation);
    CheckClaim("and not one yard sooner",
               ReadRegroupClaim(Near("m", 100.1f), LIVE, true), RegroupClaim::Rejoining);

    // The same member, read both ways, gives different answers between the two
    // lines and identical answers outside them. That IS the hysteresis, stated
    // rather than implied.
    float const between[] = {100.1f, 200.f, 300.f, 499.9f, 500.f};
    for (float yards : between)
        Check("between the lines the two readings differ",
              ReadRegroupClaim(Near("m", yards), LIVE, false) !=
                  ReadRegroupClaim(Near("m", yards), LIVE, true),
              true);
    float const outside[] = {0.f, 50.f, 99.f, 100.f, 501.f, 2101.f};
    for (float yards : outside)
        Check("outside them the two readings agree",
              ReadRegroupClaim(Near("m", yards), LIVE, false) ==
                  ReadRegroupClaim(Near("m", yards), LIVE, true),
              true);
}

// ============================================================================
// THE DEADLOCK SECTION. A hold that never releases is worse than the bug, so
// every member the family must NOT wait for is asserted by name.
// ============================================================================

void AMemberOnAnotherMapNeverHoldsTheFamily()
{
    RegroupMember away = Near("away", 0.f);
    away.sameMap = false;
    CheckClaim("another map is not a distance", ReadRegroupClaim(away, LIVE, false),
               RegroupClaim::AnotherMap);
    Check("and is never waited for", RegroupClaimIsWaitedFor(
              ReadRegroupClaim(away, LIVE, false)), false);

    // And no number handed in alongside it changes that, including the enormous
    // ones a cross-map subtraction produces. `follow` cannot cross a map and
    // neither can an `at:` aim, so there is no walk for the family to wait on.
    float const anything[] = {-1.f, 0.f, 3.f, 99.f, 501.f, 2101.f, 10560.f, 1e9f};
    for (float yards : anything)
    {
        away.yards = yards;
        CheckClaim("no distance overrules a map boundary",
                   ReadRegroupClaim(away, LIVE, false), RegroupClaim::AnotherMap);
        CheckClaim("in either direction of the hysteresis",
                   ReadRegroupClaim(away, LIVE, true), RegroupClaim::AnotherMap);
    }

    std::vector<RegroupMember> family = TheNightItHappened();
    family.back().sameMap = false;
    FamilyRegroup const verdict = ReadFamilyRegroup(family, LIVE, false);
    Check("so the family carries on rather than waiting forever", verdict.wait, false);
    Check("and counts the one it left", verdict.notWaitedFor == 1u, true);
    CheckName("and says who and why", RegroupCarriedOnWithout(family, LIVE, false),
              "behind (on another map)");
}

void ADeadMemberNeverHoldsTheFamily()
{
    RegroupMember corpse = Near("corpse", 2101.f);
    corpse.alive = false;
    CheckClaim("a corpse two thousand yards back is not rejoining",
               ReadRegroupClaim(corpse, LIVE, false), RegroupClaim::Dead);
    Check("and is never waited for",
          RegroupClaimIsWaitedFor(ReadRegroupClaim(corpse, LIVE, false)), false);
    CheckClaim("nor once the family is already waiting",
               ReadRegroupClaim(corpse, LIVE, true), RegroupClaim::Dead);

    // ...AND IT COMES BACK THE MOMENT IT IS ALIVE AGAIN, which is what keeps
    // this from being an abandonment. The revival drive owns the corpse; the
    // instant it stands up it is a member the family waits for.
    corpse.alive = true;
    CheckClaim("a revived member is waited for again",
               ReadRegroupClaim(corpse, LIVE, false), RegroupClaim::Rejoining);
}

void AMemberNothingCanFindNeverHoldsTheFamily()
{
    RegroupMember gone = Near("gone", 2101.f);
    gone.seen = false;
    CheckClaim("a name that resolves to nobody is not a distance",
               ReadRegroupClaim(gone, LIVE, false), RegroupClaim::NotInTheWorld);
    Check("and is never waited for",
          RegroupClaimIsWaitedFor(ReadRegroupClaim(gone, LIVE, false)), false);

    // Not seen outranks everything, including a map answer the caller filled in
    // anyway, because there is no character there to have a map.
    gone.sameMap = false;
    gone.alive = false;
    CheckClaim("and it outranks every other reading",
               ReadRegroupClaim(gone, LIVE, false), RegroupClaim::NotInTheWorld);
}

void AMemberAnUnmeasuredDistanceNeverHoldsTheFamily()
{
    RegroupMember unread = Near("unread", -1.f);
    CheckClaim("a negative distance is the caller saying it did not measure one",
               ReadRegroupClaim(unread, LIVE, false), RegroupClaim::AnotherMap);
    Check("and is never waited for",
          RegroupClaimIsWaitedFor(ReadRegroupClaim(unread, LIVE, false)), false);
}

void AMemberADungeonRunOwnsNeverHoldsTheFamily()
{
    RegroupMember onRun = Near("onrun", 2101.f);
    onRun.ownedByARun = true;
    CheckClaim("a run owns where this member should be",
               ReadRegroupClaim(onRun, LIVE, false), RegroupClaim::OnARun);
    Check("and is never waited for",
          RegroupClaimIsWaitedFor(ReadRegroupClaim(onRun, LIVE, false)), false);
}

void AMemberTheFamilyAlreadyGaveUpOnNeverHoldsItAgain()
{
    RegroupMember again = Near("again", 2101.f);
    again.stoodDown = true;
    CheckClaim("a stood-down member does not restart the wait",
               ReadRegroupClaim(again, LIVE, false), RegroupClaim::StoodDown);
    Check("and is never waited for",
          RegroupClaimIsWaitedFor(ReadRegroupClaim(again, LIVE, false)), false);
    // ...AND THE STAND-DOWN LIFTING PUTS IT BACK. Bounded, not permanent.
    again.stoodDown = false;
    CheckClaim("once the stand-down lifts it is waited for again",
               ReadRegroupClaim(again, LIVE, false), RegroupClaim::Rejoining);
}

// THE INVARIANT THAT MAKES THE HOLD SAFE AT ALL: over every combination of the
// five facts and every distance, the family waits ONLY for a member that is in
// the world, on the leader's map, alive, free of a run, not stood down, and
// with a real distance past the line. Nothing else can ever hold it.
void NothingButAWalkableStragglerCanEverHoldTheFamily()
{
    float const yards[] = {-1e6f, -1.f, 0.f, 99.f, 100.f, 100.1f, 499.9f,
                           500.f, 500.1f, 2101.f, 1e6f};
    for (int seen = 0; seen < 2; ++seen)
        for (int sameMap = 0; sameMap < 2; ++sameMap)
            for (int alive = 0; alive < 2; ++alive)
                for (int onRun = 0; onRun < 2; ++onRun)
                    for (int stood = 0; stood < 2; ++stood)
                        for (int waiting = 0; waiting < 2; ++waiting)
                            for (float y : yards)
                            {
                                RegroupMember m;
                                m.name = "m";
                                m.seen = seen != 0;
                                m.sameMap = sameMap != 0;
                                m.alive = alive != 0;
                                m.ownedByARun = onRun != 0;
                                m.stoodDown = stood != 0;
                                m.yards = y;
                                RegroupClaim const claim =
                                    ReadRegroupClaim(m, LIVE, waiting != 0);
                                float const line = waiting ? LIVE.rejoinYards
                                                           : LIVE.splitYards;
                                bool const walkable = m.seen && m.sameMap && m.alive &&
                                                      !m.ownedByARun && !m.stoodDown &&
                                                      m.yards >= 0.f && m.yards > line;
                                Check("only a walkable straggler is ever waited for",
                                      RegroupClaimIsWaitedFor(claim), walkable);

                                std::vector<RegroupMember> const one{m};
                                FamilyRegroup const verdict =
                                    ReadFamilyRegroup(one, LIVE, waiting != 0);
                                Check("and the family verdict says the same",
                                      verdict.wait, walkable);
                                // A wait always names somebody. A refusal never
                                // does, so no caller can act on a stale name.
                                Check("a wait always names a member",
                                      verdict.wait == !verdict.waitingFor.empty(), true);
                            }
}

// AN EMPTY FAMILY WAITS FOR NOBODY, which is the fail-open direction and the
// right one here: the alternative is a leader held still because a roster query
// came back empty.
void AnEmptyFamilyWaitsForNobody()
{
    std::vector<RegroupMember> const nobody;
    FamilyRegroup const verdict = ReadFamilyRegroup(nobody, LIVE, false);
    Check("no members, no wait", verdict.wait, false);
    Check("and nobody carried on without", verdict.notWaitedFor == 0u, true);
    CheckName("and nothing to say", RegroupCarriedOnWithout(nobody, LIVE, false), "");
}

// TWO STRAGGLERS: the family waits for the one furthest back, because that is
// the one the wait is about and because the backstop clock ratchets on this
// number. Reporting the nearer one would look like progress while the member
// that needs the wait fell further behind.
void TheFamilyWaitsForTheOneFurthestBack()
{
    std::vector<RegroupMember> const two = {Near("near", 700.f), Near("far", 2101.f),
                                           Near("beside", 4.f)};
    FamilyRegroup const verdict = ReadFamilyRegroup(two, LIVE, false);
    Check("it waits", verdict.wait, true);
    CheckName("for the one furthest back", verdict.waitingFor, "far");
    Check("and reports that member's gap", verdict.worstYards == 2101.f, true);

    // Order in the roster must not change the answer.
    std::vector<RegroupMember> const flipped = {Near("far", 2101.f), Near("near", 700.f),
                                               Near("beside", 4.f)};
    CheckName("whatever order the roster comes back in",
              ReadFamilyRegroup(flipped, LIVE, false).waitingFor, "far");
}

// ONE STRAGGLER WORTH WAITING FOR AND ONE THAT IS NOT: the family waits, and
// still says out loud who it is carrying on without. Both halves matter -
// waiting silently for one while abandoning another is how an operator loses an
// hour to a character nothing mentions.
void AWaitAndAnAbandonmentCanBothBeTrue()
{
    std::vector<RegroupMember> family = {Near("walking", 900.f), Near("lost", 1500.f),
                                        Near("beside", 6.f)};
    family[1].sameMap = false;
    FamilyRegroup const verdict = ReadFamilyRegroup(family, LIVE, false);
    Check("it waits for the one that can rejoin", verdict.wait, true);
    CheckName("naming it", verdict.waitingFor, "walking");
    Check("and counts the one it cannot", verdict.notWaitedFor == 1u, true);
    CheckName("and says which and why", RegroupCarriedOnWithout(family, LIVE, false),
              "lost (on another map)");
}

void EveryReasonIsNamedInTheOneLineThatReportsThem()
{
    std::vector<RegroupMember> family = {Near("a", 1000.f), Near("b", 1000.f),
                                        Near("c", 1000.f), Near("d", 1000.f),
                                        Near("e", 1000.f)};
    family[0].seen = false;
    family[1].sameMap = false;
    family[2].alive = false;
    family[3].ownedByARun = true;
    family[4].stoodDown = true;
    FamilyRegroup const verdict = ReadFamilyRegroup(family, LIVE, false);
    Check("five stragglers and not one of them can be waited for", verdict.wait, false);
    Check("all five are counted", verdict.notWaitedFor == 5u, true);
    CheckName("and every one is named with its own reason",
              RegroupCarriedOnWithout(family, LIVE, false),
              "a (not in the world), b (on another map), c (dead), "
              "d (on a dungeon run), e (stood down)");
}

// A verdict a log line names has to have a name, and no two may share one.
void EveryClaimHasItsOwnName()
{
    RegroupClaim const all[] = {
        RegroupClaim::InFormation, RegroupClaim::Rejoining,
        RegroupClaim::NotInTheWorld, RegroupClaim::AnotherMap,
        RegroupClaim::Dead, RegroupClaim::OnARun, RegroupClaim::StoodDown};
    for (RegroupClaim a : all)
    {
        Check("a name is never empty", RegroupClaimName(a)[0] != '\0', true);
        for (RegroupClaim b : all)
        {
            if (a == b)
                continue;
            Check("two claims never share a name",
                  std::strcmp(RegroupClaimName(a), RegroupClaimName(b)) != 0, true);
        }
    }
}

// ============================================================================
// THE ACCEPTANCE CRITERION, WALKED. A member is placed 2,000 yards out and the
// poll loop is run to completion. The gap must close monotonically and the wait
// must end. This is the shape of the run the issue asks for, with the world
// replaced by arithmetic.
// ============================================================================

void ATwoThousandYardRejoinBothStartsAndFinishes()
{
    // The measured closing rate while the leader kept moving was about 160
    // yards a minute. With the leader held it is the follower's own pace, and
    // the party poll is thirty seconds - but the exact figure does not matter
    // to what is being asserted, which is that the loop terminates and the
    // verdict flips exactly once in each direction.
    float const closedPerPoll = 80.f;
    float gap = 2000.f;
    bool waiting = false;
    int polls = 0;
    int startedWaiting = 0;
    int stoppedWaiting = 0;
    float previous = gap;

    for (; polls < 200; ++polls)
    {
        std::vector<RegroupMember> const family = {Near("beside", 5.f), Near("behind", gap)};
        FamilyRegroup const verdict = ReadFamilyRegroup(family, LIVE, waiting);
        if (verdict.wait && !waiting)
            ++startedWaiting;
        if (!verdict.wait && waiting)
            ++stoppedWaiting;
        if (verdict.wait)
        {
            CheckName("while it waits it names the member it waits for",
                      verdict.waitingFor, "behind");
            Check("and the gap it reports is that member's",
                  verdict.worstYards == gap, true);
        }
        waiting = verdict.wait;
        if (!waiting && polls > 0)
            break;
        Check("the gap never grows while the family waits", gap <= previous, true);
        previous = gap;
        gap -= closedPerPoll;
        if (gap < 0.f)
            gap = 0.f;
    }

    Check("the wait started", startedWaiting == 1, true);
    Check("...on the first poll, because 2000 is past the split line",
          startedWaiting == 1 && polls > 1, true);
    Check("the wait ended", stoppedWaiting == 1, true);
    Check("it is not waiting at the end", waiting, false);
    Check("and it took a bounded number of polls to get there", polls < 200, true);
    Check("the member ended inside the hand-back distance", gap <= LIVE.rejoinYards,
          true);
}

// ...AND THE SAME WALK WITH A MEMBER THAT NEVER MOVES TERMINATES TOO, at the
// caller's backstop rather than here. This rule keeps saying "wait" for a
// stationary straggler, which is correct and is exactly why the caller must own
// a clock: the proof that the rule alone cannot end it is worth having in
// writing, so nobody ships it without one.
void AStragglerThatNeverMovesKeepsSayingWaitAndThatIsTheCallersProblem()
{
    std::vector<RegroupMember> const stuck = {Near("beside", 5.f), Near("stuck", 2000.f)};
    for (int poll = 0; poll < 50; ++poll)
        Check("a stationary straggler never stops asking to be waited for",
              ReadFamilyRegroup(stuck, LIVE, true).wait, true);

    // The two things the caller can hand back that DO end it, both of which are
    // facts about the world rather than about time: the member stood down after
    // the caller's ratchet gave up, and the member gone from the world.
    std::vector<RegroupMember> gaveUp = stuck;
    gaveUp[1].stoodDown = true;
    Check("a caller that gave up ends it by standing the member down",
          ReadFamilyRegroup(gaveUp, LIVE, true).wait, false);
    std::vector<RegroupMember> vanished = stuck;
    vanished[1].seen = false;
    Check("and a member that leaves the world ends it by itself",
          ReadFamilyRegroup(vanished, LIVE, true).wait, false);
}

}  // namespace

int main()
{
    TheFamilyWaitsForTheMemberItLeftBehind();
    AFamilyStandingTogetherWaitsForNobody();
    TheLinesAreWhereTheFollowGapPutThem();
    WaitingStartsAtOneLineAndEndsAtTheOther();

    AMemberOnAnotherMapNeverHoldsTheFamily();
    ADeadMemberNeverHoldsTheFamily();
    AMemberNothingCanFindNeverHoldsTheFamily();
    AMemberAnUnmeasuredDistanceNeverHoldsTheFamily();
    AMemberADungeonRunOwnsNeverHoldsTheFamily();
    AMemberTheFamilyAlreadyGaveUpOnNeverHoldsItAgain();
    NothingButAWalkableStragglerCanEverHoldTheFamily();
    AnEmptyFamilyWaitsForNobody();

    TheFamilyWaitsForTheOneFurthestBack();
    AWaitAndAnAbandonmentCanBothBeTrue();
    EveryReasonIsNamedInTheOneLineThatReportsThem();
    EveryClaimHasItsOwnName();

    ATwoThousandYardRejoinBothStartsAndFinishes();
    AStragglerThatNeverMovesKeepsSayingWaitAndThatIsTheCallersProblem();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the family waits for a member that can rejoin, and for nobody else\n");
    return EXIT_SUCCESS;
}
