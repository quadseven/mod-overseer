/*
 * A guild bot leaves a group it was talked into by somebody else's bot.
 *
 * Random bots of the realm invite guild members they pass and lead them off,
 * and a follower's own errands are held for as long as it follows. The control
 * plane sends `leave-group` on kind='guild' to the follower; the module takes
 * the follower out of the group the way a player's /leave does, and refuses
 * what that would break. These are the row and the gate.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

namespace D = OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Is(char const* got, char const* want)
{
    return std::strcmp(got, want) == 0;
}

D::LeaveGroupFacts Follower()
{
    D::LeaveGroupFacts f;
    f.isBot = true;
    f.inGroup = true;
    return f;
}

void TheRowIsItsFirstWord()
{
    Check("leave-group is the row", D::IsLeaveGroupRow("leave-group"));
    Check("spaces around it", D::IsLeaveGroupRow("  leave-group "));
    Check("not party-disband", !D::IsLeaveGroupRow("party-disband"));
    Check("not the bot chat word", !D::IsLeaveGroupRow("leave"));
    Check("not empty", !D::IsLeaveGroupRow(""));
    Check("the verb is spelled once", Is(D::LEAVE_GROUP_VERB, "leave-group"));
}

void AFollowerMayLeave()
{
    Check("a bot following somebody may leave", Is(D::LeaveGroupGate(Follower()), ""));
}

void TheGateRefusesWhatLeavingWouldBreak()
{
    D::LeaveGroupFacts f = Follower();
    f.inGroup = false;
    Check("no group", Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::NotInGroup));

    f = Follower();
    f.isBot = false;
    Check("a player is never made to leave",
          Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::NotABot));

    f = Follower();
    f.leadsGroup = true;
    Check("a leader is not made to leave its own group",
          Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::LeadsGroup));

    f = Follower();
    f.battleground = true;
    Check("a battleground or finder group is left alone",
          Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::Battleground));

    f = Follower();
    f.inInstance = true;
    Check("not from inside an instance",
          Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::InInstance));

    f = Follower();
    f.inCombat = true;
    Check("not in a fight", Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::InCombat));
}

void APlayerIsAskedFirst()
{
    // Every wall is up: the player wall is named before the rest, so a row
    // aimed at a person always says that.
    D::LeaveGroupFacts f;
    f.inGroup = true;
    f.leadsGroup = true;
    f.battleground = true;
    f.inInstance = true;
    f.inCombat = true;
    Check("a person is named first", Is(D::LeaveGroupGate(f), D::LeaveGroupRefusal::NotABot));
}

}  // namespace

int main()
{
    TheRowIsItsFirstWord();
    AFollowerMayLeave();
    TheGateRefusesWhatLeavingWouldBreak();
    APlayerIsAskedFirst();
    if (failures)
        return 1;
    std::printf("leave-group holds\n");
    return 0;
}
