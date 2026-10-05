/*
 * A finder group nobody is inside, and that the finder is no longer matching,
 * is a leftover, and the one-group rule may let it go (2026-10-03).
 *
 * Measured on the dev realm: both families' four followers sat in the finder
 * groups of finished runs for hours while each head stood in a group of one.
 * The one-group rule left the finder groups alone as the core's, and the only
 * thing that disbands one (LeaveTheFinderGroup) finds it through the head's
 * group, which a relog had taken him out of.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::FinderGroupStillServes;
using OverseerDecisions::LeftoverFinderGroupKept;
using OverseerDecisions::FinderState;

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

void ALeftoverIsLetGo()
{
    Check("a finished run's group with nobody inside is a leftover",
          !FinderGroupStillServes(FinderState::Dungeon, false));
    Check("a group the finder has forgotten is a leftover",
          !FinderGroupStillServes(FinderState::None, false));
    Check("a finished dungeon with nobody inside is a leftover",
          !FinderGroupStillServes(FinderState::Other, false));
}

void TheFinderKeepsWhatItIsUsing()
{
    Check("a member inside keeps it", FinderGroupStillServes(FinderState::Dungeon, true));
    Check("a member inside keeps it whatever the state",
          FinderGroupStillServes(FinderState::None, true));
    Check("a role check keeps it", FinderGroupStillServes(FinderState::RoleCheck, false));
    Check("a queue keeps it", FinderGroupStillServes(FinderState::Queued, false));
    Check("a proposal keeps it", FinderGroupStillServes(FinderState::Proposal, false));
}

}  // namespace

void AFinishedRunsGroupWithTheHeadIsLetGo()
{
    // Grug's family, Gnomeregan's finder group, the campaign done, nobody in.
    Check("the head's finished-run group goes once the family's run is over",
          !LeftoverFinderGroupKept(FinderState::Other, false, true, false));
    Check("the head's group stays while the family's run is open",
          LeftoverFinderGroupKept(FinderState::Other, false, true, true));
    Check("a member inside keeps the head's group whatever the run says",
          LeftoverFinderGroupKept(FinderState::Dungeon, true, true, false));
    Check("a queue keeps the head's group",
          LeftoverFinderGroupKept(FinderState::Queued, false, true, false));
    Check("without the head, a leftover goes as before",
          !LeftoverFinderGroupKept(FinderState::None, false, false, true));
}

int main()
{
    ALeftoverIsLetGo();
    TheFinderKeepsWhatItIsUsing();
    AFinishedRunsGroupWithTheHeadIsLetGo();
    if (failures)
        return 1;
    std::printf("ok test_finder_group_leftover\n");
    return 0;
}
