/*
 * Whether a dungeon portal can be approached at all, before a run is opened.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter supplies the two map ids; this test pins the rule that
 * keeps a party out of a run whose staging point no travel errand could ever
 * be resolved for.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonApproach;
using OverseerDecisions::DungeonPortalApproach;

namespace
{

int failures = 0;

void Check(char const* what, DungeonApproach got, DungeonApproach want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// The three portals that shipped before Wailing Caverns are all approached
// from map 0, and the family lives on map 0. Those runs must be unaffected.
void SameMapIsWalkable()
{
    Check("deadmines from Elwynn", DungeonPortalApproach(0, 0),
          DungeonApproach::Walkable);
    Check("wailing from the Barrens", DungeonPortalApproach(1, 1),
          DungeonApproach::Walkable);
}

// The case the Wailing Caverns row creates: a portal on Kalimdor while the
// party is in the Eastern Kingdoms. There is no navmesh across that, so the
// run must not open.
void OtherContinentIsRefused()
{
    Check("map 1 portal, party on map 0", DungeonPortalApproach(0, 1),
          DungeonApproach::OffOutsideMap);
    Check("map 0 portal, party on map 1", DungeonPortalApproach(1, 0),
          DungeonApproach::OffOutsideMap);
}

// An instance map is not the outside map either, and the refusal must not
// special-case continents: the rule is equality, because the travel layer's
// rule is equality.
void AnInstanceMapIsAlsoNotTheOutsideMap()
{
    Check("party inside Deadmines", DungeonPortalApproach(36, 0),
          DungeonApproach::OffOutsideMap);
    Check("party inside Wailing Caverns", DungeonPortalApproach(43, 1),
          DungeonApproach::OffOutsideMap);
}

} // namespace

int main()
{
    SameMapIsWalkable();
    OtherContinentIsRefused();
    AnInstanceMapIsAlsoNotTheOutsideMap();
    return failures ? 1 : 0;
}
