/*
 * Dungeon staging decisions, without AzerothCore.
 *
 * An early entrant is ahead of the staging point, not a missing member. The
 * barrier may therefore open for the remaining party, while the crossing
 * predicate still requires every roster member to be inside before CLEARING.
 *
 * And a member that HAS reached the staging point is held on it until the
 * barrier opens, rather than left to be re-walked onto it every five seconds
 * (#346). The two predicates are checked against each other here, not only
 * separately: the hold has to be a narrowing of the barrier's own reading, or
 * it is a second opinion about where a party is standing.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::ApproachLimits;
using OverseerDecisions::DungeonRunBarrierMet;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::DungeonRunHoldsAtStage;
using OverseerDecisions::DungeonRunMemberState;

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

// The adapter's own three numbers: the barrier radius, one step's reach and
// the height one step may bridge. See ApproachLimits.
constexpr ApproachLimits LIMITS{10.f, 60.f, 20.f};

DungeonRunMemberState ReadyMember(float distance)
{
    DungeonRunMemberState state;
    state.seen = true;
    state.alive = true;
    state.distanceFromStage = distance;
    return state;
}

void AnEarlyEntrantDoesNotDeadlockTheBarrier()
{
    std::vector<DungeonRunMemberState> members;
    DungeonRunMemberState inside = ReadyMember(0.f);
    inside.inside = true;
    members.push_back(inside);
    members.push_back(ReadyMember(8.f));

    Check("inside member counts as staged", DungeonRunBarrierMet(members, LIMITS), true);
}

void MissingMembersStillFailClosed()
{
    std::vector<DungeonRunMemberState> members;
    DungeonRunMemberState inside = ReadyMember(0.f);
    inside.inside = true;
    members.push_back(inside);
    members.push_back(DungeonRunMemberState());

    Check("unseen member blocks barrier", DungeonRunBarrierMet(members, LIMITS), false);
}

void AnInsideDeadMemberStillBlocksTheBarrier()
{
    std::vector<DungeonRunMemberState> members;
    DungeonRunMemberState inside = ReadyMember(0.f);
    inside.inside = true;
    inside.alive = false;
    members.push_back(inside);
    members.push_back(ReadyMember(8.f));

    Check("dead inside member blocks barrier", DungeonRunBarrierMet(members, LIMITS), false);
}

void OneMemberCannotPretendTheWholePartyCrossed()
{
    std::vector<DungeonRunEntryState> members;
    DungeonRunEntryState inside;
    inside.seen = true;
    inside.alive = true;
    inside.through = true;
    members.push_back(inside);

    DungeonRunEntryState outside;
    outside.seen = true;
    outside.alive = true;
    outside.distanceFromDoor = 2.f;
    members.push_back(outside);

    Check("split party is not all through",
          OverseerDecisions::DungeonRunAllThrough(members), false);
}

// THE MEASURED DEFECT (#346). Four members reached the door and then orbited
// it - 42 yards out, then 142, then 173, then 103, then 214 - for the whole of
// a twelve minute staging window, three runs in a row. A member standing on the
// staging point is the case that has to answer true here, because everything
// downstream of this is what stops it being walked around in circles.
void AMemberOnTheStagingPointIsHeld()
{
    Check("arrived member holds", DungeonRunHoldsAtStage(ReadyMember(4.f), LIMITS), true);
    Check("and one exactly on the radius holds",
          DungeonRunHoldsAtStage(ReadyMember(10.f), LIMITS), true);
}

// A MEMBER THAT IS STILL COMING IS NEVER HELD, which is the failure mode worth
// naming: a hold that fired on a walking character would stop it where it stood
// and the barrier would then wait for a member this module had itself parked.
void AMemberStillWalkingIsNotHeld()
{
    Check("closing member is not held",
          DungeonRunHoldsAtStage(ReadyMember(240.f), LIMITS), false);

    // AND NEITHER IS ONE ON THE LEDGE OVER THE DOOR (#217). Ten yards out and a
    // hundred and fifty up is the reading that used to pass for an arrival, and
    // it is the one reading where holding would be actively harmful: the member
    // needs to find a route down, and a hold is the opposite of that.
    DungeonRunMemberState overhead = ReadyMember(10.f);
    overhead.verticalFromStage = 150.f;
    Check("overhead member is not held", DungeonRunHoldsAtStage(overhead, LIMITS), false);
}

// A FIGHT OUTRANKS THE HOLD, and this is the same line #335 drew when it left
// `flee` alone: a character held still in a fight is a character killed by the
// hold. Nothing is lost by declining - the barrier will not open for a member in
// combat either - and the caller re-asks every poll, so a held member pulled
// into a fight has its movement back within one.
void AMemberInCombatIsNotHeld()
{
    DungeonRunMemberState fighting = ReadyMember(3.f);
    fighting.inCombat = true;
    Check("member in combat is not held", DungeonRunHoldsAtStage(fighting, LIMITS), false);

    DungeonRunMemberState dead = ReadyMember(3.f);
    dead.alive = false;
    Check("dead member is not held", DungeonRunHoldsAtStage(dead, LIMITS), false);
}

// NO READING IS NOT AN ARRIVAL, the same fail-closed rule the barrier keeps. A
// member on another map carries a negative distance, and a member this poll
// could not find carries nothing at all.
void AnUnmeasuredMemberIsNotHeld()
{
    Check("unseen member is not held",
          DungeonRunHoldsAtStage(DungeonRunMemberState(), LIMITS), false);

    DungeonRunMemberState wrongMap;
    wrongMap.seen = true;
    wrongMap.alive = true;   // distanceFromStage stays at its -1 sentinel
    Check("member on another map is not held",
          DungeonRunHoldsAtStage(wrongMap, LIMITS), false);
}

// THE ONE MEMBER THE BARRIER IS HAPPY WITH AND THE HOLD MUST REFUSE. Being
// inside satisfies the barrier - it is ahead of the staging point, not missing
// from it - but holding it would be this module pinning a character inside an
// instance so that a barrier outside the instance opens sooner.
void AnInsideMemberSatisfiesTheBarrierAndIsStillNotHeld()
{
    DungeonRunMemberState inside = ReadyMember(0.f);
    inside.inside = true;

    std::vector<DungeonRunMemberState> members;
    members.push_back(inside);
    members.push_back(ReadyMember(8.f));
    Check("inside member satisfies the barrier",
          DungeonRunBarrierMet(members, LIMITS), true);
    Check("and is still not held", DungeonRunHoldsAtStage(inside, LIMITS), false);
}

// THE INVARIANT, CHECKED RATHER THAN ASSERTED IN A COMMENT: every member the
// hold accepts is a member the barrier has stopped waiting on. A party made
// entirely of held members therefore opens its own barrier, which is what makes
// the hold safe - it can only ever be holding characters whose being still is
// the thing the run is waiting for.
void EverythingHeldIsAlreadySatisfyingTheBarrier()
{
    std::vector<DungeonRunMemberState> party;
    party.push_back(ReadyMember(1.f));
    party.push_back(ReadyMember(6.f));
    party.push_back(ReadyMember(9.9f));

    for (DungeonRunMemberState const& member : party)
        Check("every member of this party is held",
              DungeonRunHoldsAtStage(member, LIMITS), true);
    Check("so the barrier they are held for is met",
          DungeonRunBarrierMet(party, LIMITS), true);
}

}  // namespace

int main()
{
    AnEarlyEntrantDoesNotDeadlockTheBarrier();
    MissingMembersStillFailClosed();
    AnInsideDeadMemberStillBlocksTheBarrier();
    OneMemberCannotPretendTheWholePartyCrossed();
    AMemberOnTheStagingPointIsHeld();
    AMemberStillWalkingIsNotHeld();
    AMemberInCombatIsNotHeld();
    AnUnmeasuredMemberIsNotHeld();
    AnInsideMemberSatisfiesTheBarrierAndIsStillNotHeld();
    EverythingHeldIsAlreadySatisfyingTheBarrier();
    return failures ? 1 : 0;
}
