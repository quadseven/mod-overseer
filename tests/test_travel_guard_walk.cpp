/*
 * Where an operator's guard walks the character it is written on (#163).
 *
 * The live test this pins, 2026-10-26: a guard in all five roster rows held
 * the column for fifteen minutes, and still one character never left the zone
 * it was dying in. Its guard named its homebind on map 1 while it stood on
 * map 0, so the travel drive released it as "no such spawn on map 0" every
 * time it looked, and the four followers were each refused as "nothing walks
 * it anywhere".
 *
 * So a guard is resolved to a place on the map the character stands on, and
 * a resolved guard steers its character itself.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <string>

using OverseerDecisions::GuardWalk;
using OverseerDecisions::GuardWalkFacts;
using OverseerDecisions::GuardWalkName;
using OverseerDecisions::GuardWalkSteers;
using OverseerDecisions::GuardWalkVerdict;
using OverseerDecisions::ReadGuardWalk;
using OverseerDecisions::TravelGuardMap;

namespace
{

int failures = 0;

// The measured guards, as the operator wrote them.
char const* const LEADER_GUARD = "guard:0:-8868,674,98";    // map 0
char const* const FOLLOWER_GUARD = "guard:0:-8865,669,98";  // map 0
char const* const AWAY_GUARD = "guard:1:-1048,-3665,24";    // map 1

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckVerdict(char const* what, GuardWalkVerdict const& got, GuardWalk walk,
                  std::string const& place)
{
    if (got.walk == walk && got.place == place)
        return;
    std::printf("FAIL %s: got %s '%s', wanted %s '%s'\n", what, GuardWalkName(got.walk),
                got.place.c_str(), GuardWalkName(walk), place.c_str());
    ++failures;
}

GuardWalkFacts On(char const* guard, std::uint32_t map, char const* leaderColumn = "")
{
    GuardWalkFacts facts;
    facts.guard = guard;
    facts.mapKnown = true;
    facts.currentMap = map;
    facts.leaderColumn = leaderColumn;
    return facts;
}

void TheMapAGuardNames()
{
    std::uint32_t map = 99;
    CheckBool("map 1 is read", TravelGuardMap(AWAY_GUARD, map) && map == 1, true);
    map = 99;
    CheckBool("map 0 is read", TravelGuardMap(LEADER_GUARD, map) && map == 0, true);
    map = 99;
    CheckBool("a three-digit map is read",
              TravelGuardMap("guard:530:1,2,3", map) && map == 530, true);
    CheckBool("an `at:` aim names no guard map", TravelGuardMap("at:0:1,2,3", map), false);
    CheckBool("a bare prefix names no map", TravelGuardMap("guard:", map), false);
    CheckBool("an empty map names no map", TravelGuardMap("guard::1,2,3", map), false);
    CheckBool("a word is not a map", TravelGuardMap("guard:x:1,2,3", map), false);
}

// A GUARD ON THE CHARACTER'S OWN MAP IS WALKED AS WRITTEN, leader or follower,
// and steers its character itself. The four followers that were told "nothing
// walks it anywhere" are this case.
void AGuardOnItsOwnMapWalksItself()
{
    GuardWalkVerdict const follower = ReadGuardWalk(On(FOLLOWER_GUARD, 0, LEADER_GUARD));
    CheckVerdict("a follower's guard on its own map", follower, GuardWalk::OwnPlace,
                 "at:0:-8865,669,98");
    CheckBool("a follower's own guard steers it", GuardWalkSteers(follower.walk), true);

    GuardWalkVerdict const leader = ReadGuardWalk(On(LEADER_GUARD, 0));
    CheckVerdict("the leader's guard", leader, GuardWalk::OwnPlace, "at:0:-8868,674,98");
    CheckBool("the leader's guard steers it", GuardWalkSteers(leader.walk), true);
}

// THE MEASURED FIFTH CHARACTER. Its guard is on map 1, it stands on map 0, and
// its leader is guarded on map 0: it goes where the rest of the family goes.
void AGuardOnAnotherMapFollowsTheLeadersGuard()
{
    GuardWalkVerdict const got = ReadGuardWalk(On(AWAY_GUARD, 0, LEADER_GUARD));
    CheckVerdict("a guard on map 1 read on map 0, leader guarded on map 0", got,
                 GuardWalk::LeadersPlace, "at:0:-8868,674,98");
    CheckBool("the leader's guard place steers it", GuardWalkSteers(got.walk), true);

    // Once it is on map 1 its own guard is the place again.
    CheckVerdict("the same guard read on map 1", ReadGuardWalk(On(AWAY_GUARD, 1, LEADER_GUARD)),
                 GuardWalk::OwnPlace, "at:1:-1048,-3665,24");
}

// NOTHING ON THIS MAP STANDS IN FOR IT: refused, with nothing to walk, and the
// caller says so once. Never handed to the drive as a place on another map.
void AGuardOnAnotherMapWithNothingHereIsRefused()
{
    GuardWalkVerdict const unguardedLeader =
        ReadGuardWalk(On(AWAY_GUARD, 0, "at:0:-8877,652,96"));
    CheckVerdict("leader carries an ordinary aim", unguardedLeader, GuardWalk::OtherMap, "");
    CheckBool("an other-map guard steers nobody", GuardWalkSteers(unguardedLeader.walk), false);

    CheckVerdict("no leader column", ReadGuardWalk(On(AWAY_GUARD, 0)), GuardWalk::OtherMap, "");
    CheckVerdict("leader guarded on a third map",
                 ReadGuardWalk(On(AWAY_GUARD, 0, "guard:530:1,2,3")), GuardWalk::OtherMap, "");
    CheckVerdict("leader's guard names no place", ReadGuardWalk(On(AWAY_GUARD, 0, "guard:")),
                 GuardWalk::OtherMap, "");
}

// THE ANSWERS THAT ARE UNCHANGED FROM BEFORE.
void TheOldAnswersStand()
{
    GuardWalkFacts unknown;
    unknown.guard = AWAY_GUARD;
    CheckVerdict("a character not in the world keeps the place as written",
                 ReadGuardWalk(unknown), GuardWalk::OwnPlace, "at:1:-1048,-3665,24");

    GuardWalkVerdict const none = ReadGuardWalk(On("guard:", 0, LEADER_GUARD));
    CheckVerdict("a guard naming no place", none, GuardWalk::NoPlace, "");
    CheckBool("a guard naming no place steers nobody", GuardWalkSteers(none.walk), false);

    CheckVerdict("a guard whose map is not a number is walked as written",
                 ReadGuardWalk(On("guard:x:1,2,3", 0)), GuardWalk::OwnPlace, "at:x:1,2,3");
}

}  // namespace

int main()
{
    TheMapAGuardNames();
    AGuardOnItsOwnMapWalksItself();
    AGuardOnAnotherMapFollowsTheLeadersGuard();
    AGuardOnAnotherMapWithNothingHereIsRefused();
    TheOldAnswersStand();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("travel guard walk: all checks passed\n");
    return 0;
}
