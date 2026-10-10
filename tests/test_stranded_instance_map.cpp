/*
 * A member stranded on another continent from a leader INSIDE A DUNGEON
 * crosses to the continent the dungeon's door stands on (2026-10-10).
 *
 * Measured on wow-dev: four of the Horde family stood inside Shadowfang Keep
 * (map 33) while Oz stood in Kalimdor (map 1). The stranded crossing compared
 * map 1 with map 33, found no transport joining them, and said "no transport
 * its faction may ride ... joins map 1 and map 33" every minute for over an
 * hour, while the zeppelin from Orgrimmar joins map 1 to map 0, where the
 * keep's door stands. The run closed 'split_failed' with 4 of 5 inside.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DecideDoorRejoin;
using OverseerDecisions::DecideStrandedWay;
using OverseerDecisions::DoorRejoinFacts;
using OverseerDecisions::DoorRejoinStep;
using OverseerDecisions::StrandedCrossingGoal;
using OverseerDecisions::StrandedFacts;
using OverseerDecisions::StrandedGoal;
using OverseerDecisions::StrandedWay;
using OverseerDecisions::StrandedWayExplanation;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

bool Says(std::string const& line, char const* part)
{
    return line.find(part) != std::string::npos;
}

constexpr std::uint32_t KALIMDOR = 1;
constexpr std::uint32_t EASTERN_KINGDOMS = 0;
constexpr std::uint32_t SHADOWFANG_KEEP = 33;  // door: areatrigger 145, on map 0
constexpr std::uint32_t WAILING_CAVERNS = 43;  // door: areatrigger 228, on map 1

// Oz as the party poll read it: alive, out of combat, stone bound in
// Orgrimmar (map 1) and ready, and the zeppelin priced against `goal`.
StrandedFacts Oz(StrandedGoal const& goal, bool transportServes)
{
    StrandedFacts f;
    f.readable = true;
    f.alive = true;
    f.memberMap = KALIMDOR;
    f.leaderMap = goal.map;
    f.leaderInsideMap = goal.viaDoor ? goal.insideMap : 0;
    f.boundOnLeaderMap = goal.map == KALIMDOR;
    f.boundOnMemberMap = true;
    f.carriesStone = true;
    f.stoneReady = true;
    f.transportServes = transportServes;
    return f;
}
}  // namespace

int main()
{
    // THE GOAL OF A CROSSING TO A LEADER INSIDE SHADOWFANG KEEP is map 0.
    {
        StrandedGoal const goal = StrandedCrossingGoal(SHADOWFANG_KEEP, true, EASTERN_KINGDOMS);
        Check("a leader inside map 33 is crossed to at its door's map 0",
              goal.map == EASTERN_KINGDOMS);
        Check("the goal says it is a door", goal.viaDoor);
        Check("the goal names the dungeon the leader is inside",
              goal.insideMap == SHADOWFANG_KEEP);
    }

    // AND THE WAY IS THE ZEPPELIN. Priced from map 1 to map 0 there is one;
    // priced from map 1 to map 33 there never was.
    {
        StrandedGoal const goal = StrandedCrossingGoal(SHADOWFANG_KEEP, true, EASTERN_KINGDOMS);
        StrandedFacts const f = Oz(goal, /*transportServes*/ true);
        StrandedWay const way = DecideStrandedWay(f);
        Check("Oz sails to the keep's continent", way == StrandedWay::Sail);
        std::string const why = StrandedWayExplanation(f, way);
        Check("the line names map 1 and map 0", Says(why, "joins map 1 and map 0"));
        Check("the line names the dungeon the leader is inside",
              Says(why, "dungeon its leader is inside, map 33"));
        Check("the line never prices map 33", !Says(why, "and map 33,"));
    }

    // A MEMBER ALREADY ON THE DOOR'S CONTINENT IS NOT STRANDED: the run walks
    // it to the door, and no boat takes it anywhere.
    {
        StrandedGoal const goal = StrandedCrossingGoal(SHADOWFANG_KEEP, true, EASTERN_KINGDOMS);
        StrandedFacts f = Oz(goal, false);
        f.memberMap = EASTERN_KINGDOMS;
        StrandedWay const way = DecideStrandedWay(f);
        Check("a member on map 0 under a leader in map 33 is not stranded",
              way == StrandedWay::NotStranded);
        Check("its line names the door's map and the dungeon",
              Says(StrandedWayExplanation(f, way), "on map 0, where the door"));
    }

    // A STONE BOUND ON THE DOOR'S CONTINENT IS A WAY THERE, the same as one
    // bound on the leader's own continent.
    {
        StrandedGoal const goal = StrandedCrossingGoal(SHADOWFANG_KEEP, true, EASTERN_KINGDOMS);
        StrandedFacts f = Oz(goal, false);
        f.boundOnLeaderMap = true;  // bound in Undercity, map 0
        Check("a stone bound on the door's continent hearths",
              DecideStrandedWay(f) == StrandedWay::Hearth);
    }

    // A DUNGEON WHOSE DOOR IS ON THE MEMBER'S OWN CONTINENT: nothing to cross.
    {
        StrandedGoal const goal = StrandedCrossingGoal(WAILING_CAVERNS, true, KALIMDOR);
        Check("a leader inside map 43 is crossed to at map 1", goal.map == KALIMDOR);
        StrandedFacts const f = Oz(goal, false);
        Check("a member on map 1 under a leader in map 43 is not stranded",
              DecideStrandedWay(f) == StrandedWay::NotStranded);
    }

    // UNCHANGED WHERE NO DOOR IS KNOWN, and for a leader on a continent.
    {
        StrandedGoal const unknown = StrandedCrossingGoal(SHADOWFANG_KEEP, false, 0);
        Check("an instance map with no known door keeps the leader's map",
              unknown.map == SHADOWFANG_KEEP && !unknown.viaDoor && !unknown.insideMap);
        StrandedGoal const open = StrandedCrossingGoal(EASTERN_KINGDOMS, false, 0);
        Check("a leader on a continent keeps its own map",
              open.map == EASTERN_KINGDOMS && !open.viaDoor);
        StrandedFacts const f = Oz(open, true);
        StrandedWay const way = DecideStrandedWay(f);
        Check("a leader on map 0 is still sailed to", way == StrandedWay::Sail);
        Check("and its line has no door clause",
              !Says(StrandedWayExplanation(f, way), "door of the dungeon"));
    }

    // ONCE ACROSS, OZ WALKS TO THE KEEP'S DOOR AND IN, rather than waiting in
    // `follow` on a leader inside. Live: Oz on map 0 in Silverpine, 460 yards
    // from areatrigger 145, Zug and three more inside map 33.
    {
        constexpr float KNOCK = 5.f;
        DoorRejoinFacts f;
        f.steerable = true;
        f.alive = true;
        f.leaderInsideKnownDoor = true;
        f.onDoorMap = true;
        f.partyBelongsInside = true;
        f.yardsFromDoor = 460.f;
        Check("a member 460y from its leader's door walks to it",
              DecideDoorRejoin(f, KNOCK) == DoorRejoinStep::Walk);
        f.yardsFromDoor = 3.f;
        Check("a member at its leader's door steps through",
              DecideDoorRejoin(f, KNOCK) == DoorRejoinStep::Knock);

        DoorRejoinFacts leaving = f;
        leaving.partyBelongsInside = false;
        Check("a party walking out is met at the door, not entered",
              DecideDoorRejoin(leaving, KNOCK) == DoorRejoinStep::Wait);
        leaving.yardsFromDoor = 460.f;
        Check("and the member still walks to the door to meet it",
              DecideDoorRejoin(leaving, KNOCK) == DoorRejoinStep::Walk);

        DoorRejoinFacts fighting = f;
        fighting.inCombat = true;
        Check("a fight comes first", DecideDoorRejoin(fighting, KNOCK) == DoorRejoinStep::Wait);
        DoorRejoinFacts dead = f;
        dead.alive = false;
        Check("a ghost belongs to the revival drive",
              DecideDoorRejoin(dead, KNOCK) == DoorRejoinStep::None);
        DoorRejoinFacts escorted = f;
        escorted.runEscortHoldsIt = true;
        Check("a run's escort owns its member",
              DecideDoorRejoin(escorted, KNOCK) == DoorRejoinStep::None);
        DoorRejoinFacts across = f;
        across.onDoorMap = false;
        Check("a member on another continent is the crossing's, not the door's",
              DecideDoorRejoin(across, KNOCK) == DoorRejoinStep::None);
        DoorRejoinFacts outdoors = f;
        outdoors.leaderInsideKnownDoor = false;
        Check("a leader outdoors is followed as before",
              DecideDoorRejoin(outdoors, KNOCK) == DoorRejoinStep::None);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
