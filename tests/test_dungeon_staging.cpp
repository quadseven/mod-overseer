/*
 * Dungeon staging decisions, without AzerothCore.
 *
 * An early entrant is ahead of the staging point, not a missing member. The
 * barrier may therefore open for the remaining party, while the crossing
 * predicate still requires every roster member to be inside before CLEARING.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::ApproachLimits;
using OverseerDecisions::DungeonRunBarrierMet;
using OverseerDecisions::DungeonRunEntryState;
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

}  // namespace

int main()
{
    AnEarlyEntrantDoesNotDeadlockTheBarrier();
    MissingMembersStillFailClosed();
    AnInsideDeadMemberStillBlocksTheBarrier();
    OneMemberCannotPretendTheWholePartyCrossed();
    return failures ? 1 : 0;
}
