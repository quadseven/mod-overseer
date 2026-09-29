#include "overseer_decisions.h"
#include <cassert>

int main()
{
    using OverseerDecisions::DungeonCrossingReachedMap;
    // Finder returns each member to its original continent on leaving.
    assert(DungeonCrossingReachedMap(0, 34, 0, true));
    assert(DungeonCrossingReachedMap(1, 34, 0, true));
    assert(!DungeonCrossingReachedMap(34, 34, 0, true));
    assert(DungeonCrossingReachedMap(34, 0, 34, false));
    assert(!DungeonCrossingReachedMap(1, 0, 34, false));
    assert(!DungeonCrossingReachedMap(0, 0, 34, false));
}
