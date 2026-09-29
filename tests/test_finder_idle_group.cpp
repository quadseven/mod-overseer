/*
 * The finder is a way in for a family that stands anywhere, including a family
 * whose last run the finder also took in (2026-09-29).
 *
 * Measured on the dev realm: the Alliance family, five members in one party,
 * finished a Stockade run by the dungeon finder and was then asked for Razorfen
 * Kraul, a door on the other continent. A finder run leaves the party it took in
 * as a finder group until RESETTING disbands it (LeaveTheFinderGroup), so the
 * IDLE coordinator asked "can the finder take this family in?" while that group
 * still stood, read "the group is already a dungeon finder group", and fell back
 * to the walk: a hold for a home bind and a continent crossing that the finder
 * never needs, for a family that was split across two maps and could not be
 * walked anywhere.
 *
 * ReadFinderReadiness carries no position at all, so where the members stand
 * cannot make the answer no; what could was the leftover group, and that is what
 * the asker can now say will be let go first. The rung's own join still refuses
 * a finder group, which is pinned in test_fallback_ladder.cpp.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::FinderFacts;
using OverseerDecisions::FinderMember;
using OverseerDecisions::ReadFinderReadiness;

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

FinderMember Member(char const* name, unsigned classId, bool leader = false)
{
    FinderMember m;
    m.name = name;
    m.leader = leader;
    m.inWorld = true;
    m.alive = true;
    m.inHeadsGroup = true;
    m.classId = classId;
    return m;
}

// Grug's family: warrior, paladin, rogue, mage, priest. Razorfen Kraul's
// finder range is 29-38 and the family is 35-39.
FinderFacts TheFamily()
{
    FinderFacts f;
    f.enabled = true;
    f.finderOn = true;
    f.dungeonId = 8;
    f.dbcMinLevel = 29;
    f.dbcMaxLevel = 38;
    f.groupExists = true;
    f.groupSize = 5;
    f.headLeads = true;
    f.family = {Member("Grug", 1, true), Member("Grog", 2), Member("Bork", 4),
                Member("Og", 8), Member("Ugga", 5)};
    return f;
}

void AFinderGroupTheLastRunLeftIsNotAReasonToWalk()
{
    FinderFacts f = TheFamily();
    f.groupIsFinders = true;
    Check("as it stands, a finder group refuses the queue",
          !ReadFinderReadiness(f, false).ready);

    f.groupMayBeFinders = true;
    Check("but one that RESETTING will disband first does not",
          ReadFinderReadiness(f, false).ready);
}

void TheFinderMayHaveHandedTheLeadToSomebodyElse()
{
    FinderFacts f = TheFamily();
    f.groupIsFinders = true;
    f.headLeads = false;
    f.groupMayBeFinders = true;
    Check("a finder group led by another member is disbanded all the same",
          ReadFinderReadiness(f, false).ready);

    f.groupIsFinders = false;
    Check("an ordinary party the head does not lead still refuses",
          !ReadFinderReadiness(f, false).ready);
}

void TheAllowanceOpensNothingElse()
{
    FinderFacts f = TheFamily();
    f.groupIsFinders = true;
    f.groupMayBeFinders = true;

    f.groupIsRaid = true;
    Check("a raid is still not queued as a party", !ReadFinderReadiness(f, false).ready);

    f = TheFamily();
    f.groupIsFinders = true;
    f.groupMayBeFinders = true;
    f.groupSize = 4;
    Check("four in the group is still not the family", !ReadFinderReadiness(f, false).ready);

    f = TheFamily();
    f.groupIsFinders = true;
    f.groupMayBeFinders = true;
    f.family[3].lock = 3;
    Check("a locked member still refuses", !ReadFinderReadiness(f, false).ready);

    f = TheFamily();
    f.groupIsFinders = true;
    f.groupMayBeFinders = true;
    f.enabled = false;
    Check("and the switch is still honoured", !ReadFinderReadiness(f, false).ready);
}

}  // namespace

int main()
{
    AFinderGroupTheLastRunLeftIsNotAReasonToWalk();
    TheFinderMayHaveHandedTheLeadToSomebodyElse();
    TheAllowanceOpensNothingElse();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("finder idle group: all checks passed\n");
    return 0;
}
