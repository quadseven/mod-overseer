#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonResetMemberState;
using OverseerDecisions::DungeonResetRosterBlocker;

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

void CombatOutsideTheInstanceDoesNotBlockReset()
{
    // ResetInstances passes this member's combat state through to the map
    // reset, but its non-global reset predicate only requires the instance map
    // to be empty.
    std::vector<DungeonResetMemberState> const party = {
        {"Leader", false, false},
        {"Zork", false, true},
    };

    Check("outside combat is not a reset blocker",
          DungeonResetRosterBlocker(party, 209).empty());
}

void AMemberStillOnTheInstanceMapBlocksReset()
{
    std::vector<DungeonResetMemberState> const party = {
        {"Leader", false, false},
        {"Zork", true, true},
    };

    Check("an instance occupant blocks reset",
          DungeonResetRosterBlocker(party, 209) == "still on map 209: Zork");
}

}  // namespace

int main()
{
    CombatOutsideTheInstanceDoesNotBlockReset();
    AMemberStillOnTheInstanceMapBlocksReset();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("dungeon reset combat: all passed\n");
}
