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

// THE THIRD ANSWER (#241). A boat serves Menethil Harbour on map 0 and
// Theramore on map 1, so a party on map 0 aimed at the Wailing Caverns portal
// on map 1 is no longer stuck - but it is still not standing where a staging
// aim could be resolved, and the run still must not open on this poll.
void AKnownCrossingIsNotWalkableButIsNotHopelessEither()
{
    Check("map 1 portal, party on map 0, boat exists",
          DungeonPortalApproach(0, 1, true), DungeonApproach::NeedsCrossing);
    Check("map 0 portal, party on map 1, boat exists",
          DungeonPortalApproach(1, 0, true), DungeonApproach::NeedsCrossing);
}

// A crossing never makes the leader's own map walkable-by-boat when it is
// already the right map: equality still wins, so a run that could always open
// still opens without anybody going near a pier.
void ACrossingNeverOverridesBeingAlreadyThere()
{
    Check("already on the portal's map, boat irrelevant",
          DungeonPortalApproach(1, 1, true), DungeonApproach::Walkable);
    Check("deadmines, boat irrelevant",
          DungeonPortalApproach(0, 0, true), DungeonApproach::Walkable);
}

// AND THE DEFAULT IS THE OLD ANSWER. Every caller and test that predates boats
// keeps exactly the behaviour it had, which is what makes the new value
// reachable only from a caller that actually looked for a transport.
void WithoutACrossingTheAnswerIsUnchanged()
{
    Check("no boat, still off the outside map", DungeonPortalApproach(0, 1),
          DungeonApproach::OffOutsideMap);
    Check("no boat, explicit false", DungeonPortalApproach(0, 1, false),
          DungeonApproach::OffOutsideMap);
    // The pure function answers on the two map ids it is given and nothing
    // else. An instance map reaching here with `true` would be the adapter's
    // bug, not this function's: no transport's path names an instanceable map,
    // so the adapter cannot find one to report. Pinned so that if it ever
    // does, the failure is this line rather than a party staged in a cave.
    Check("an instance map, told there is a crossing",
          DungeonPortalApproach(43, 1, true), DungeonApproach::NeedsCrossing);
}

} // namespace

int main()
{
    SameMapIsWalkable();
    OtherContinentIsRefused();
    AnInstanceMapIsAlsoNotTheOutsideMap();
    AKnownCrossingIsNotWalkableButIsNotHopelessEither();
    ACrossingNeverOverridesBeingAlreadyThere();
    WithoutACrossingTheAnswerIsUnchanged();
    return failures ? 1 : 0;
}
