/*
 * Getting a member that is left inside an instance back out of it (#351).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter takes the census, writes the aim and lets the game's own
 * areatrigger decide; what is pinned here is who a run may walk out, who it
 * may only name, and the arithmetic that says an aim at a doorway can open it.
 *
 * THE READING THIS EXISTS FOR, measured on the live realm. Two members of a
 * five stood at (-163.5, 132.9, -73.7) on instance map 43 - the entrance
 * trigger's own landing point, to the decimal - for over an hour, with the
 * leader outside on map 1. An instance cannot be reset while anybody is in it,
 * so the campaign wrote twelve consecutive rows reading
 *
 *     ended_reason  the reset never became possible in 5 minutes -
 *                   still on map 43: <three names>
 *     outcome       reset_failed
 *
 * one every five minutes, with run_number never leaving 1. Nothing was aiming
 * those two at anything: the coordinator only ever walks a party out of a door
 * from inside a run it owns, and it could not own this one because adoption
 * asks about the LEADER, who was outside.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ArrivalReachesTrigger;
using OverseerDecisions::DungeonEvacuation;
using OverseerDecisions::DungeonRunEntryBlockers;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::DungeonRunEvacuation;

namespace
{

int failures = 0;

// TRAVEL_ARRIVED_POSITION_YARDS, copied from src/mod_overseer.cpp rather than
// shared with it. Not a dependency on the adapter's shape: the point of writing
// it here is that the decision below is true of the number this module actually
// walks on, and a copy that drifts is a test that stops describing the module -
// which is why the two triggers it is checked against are quoted from the world
// database in the same breath.
constexpr float ARRIVED_YARDS = 5.0f;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNames(char const* what, std::vector<std::string> const& got,
                std::vector<std::string> const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got [", what);
    for (std::string const& name : got)
        std::printf("%s ", name.c_str());
    std::printf("], want [");
    for (std::string const& name : want)
        std::printf("%s ", name.c_str());
    std::printf("]\n");
    ++failures;
}

// A member of the census, in the shape the adapter hands over. The distance is
// measured from the EXIT door, which stands on the inside map, so a member that
// carries one is a member standing in the instance.
DungeonRunEntryState Outside(char const* name)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.through = true;
    return state;
}

DungeonRunEntryState Inside(char const* name, float fromDoor)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.distanceFromDoor = fromDoor;
    return state;
}

DungeonRunEntryState DeadInside(char const* name, float fromDoor)
{
    DungeonRunEntryState state = Inside(name, fromDoor);
    state.alive = false;
    return state;
}

DungeonRunEntryState Elsewhere(char const* name)
{
    // Seen, alive, not through this door and not on its map either: the
    // negative sentinel the crossing predicates already read as "not measured".
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    return state;
}

DungeonRunEntryState NotSeen(char const* name)
{
    DungeonRunEntryState state;
    state.name = name;
    return state;
}

// ---------------------------------------------------------------------------

// THE MEASURED SHAPE. Three of the five out on the map outside, two standing at
// the landing point inside. The two inside are the whole of what holds the
// reset, and they are exactly who gets walked.
void TheTwoLeftInsideAreTheOnesWalkedOut()
{
    std::vector<DungeonRunEntryState> const census = {
        Outside("Leader"), Outside("Second"), Outside("Third"),
        Inside("Straggler", 12.7f), Inside("OtherStraggler", 12.7f),
    };

    DungeonEvacuation const evacuation = DungeonRunEvacuation(census);
    CheckNames("who is walked out", evacuation.walk, {"Straggler", "OtherStraggler"});
    CheckNames("who is only named", evacuation.wait, {});
}

// A MEMBER ALREADY OUT IS NEVER AIMED AT A DOOR IT HAS LEFT. This is not
// tidiness: an aim naming a map the character is not on is one
// ResolveTravelTarget refuses outright, so writing it would spend an errand on
// a refusal and log it as a fault.
void NobodyThatIsAlreadyOutIsWalkedAnywhere()
{
    std::vector<DungeonRunEntryState> const census = {
        Outside("Leader"), Outside("Second"), Outside("Third"),
        Outside("Fourth"), Outside("Fifth"),
    };

    DungeonEvacuation const evacuation = DungeonRunEvacuation(census);
    CheckNames("nobody to walk", evacuation.walk, {});
    CheckNames("nobody to name", evacuation.wait, {});
}

// A CORPSE WALKS NOWHERE, AND IT HOLDS THE INSTANCE OPEN JUST AS HARD. Naming
// it separately is the point: an aim would do nothing for it, and leaving it
// silently out of the answer would make the reset's own hold line read as if
// the map were empty.
void ADeadMemberIsNamedRatherThanWalked()
{
    std::vector<DungeonRunEntryState> const census = {
        Outside("Leader"),
        Inside("Straggler", 40.f),
        DeadInside("Corpse", 300.f),
    };

    DungeonEvacuation const evacuation = DungeonRunEvacuation(census);
    CheckNames("the living one is walked", evacuation.walk, {"Straggler"});
    CheckNames("the dead one is named", evacuation.wait, {"Corpse"});
}

// TWO WAYS OF NOT BEING IN THERE, AND NEITHER IS WALKED. A member on some third
// map is not what holds this reset; a member this poll could not find is not in
// the world and therefore not on the map, which is the same reading the
// adapter's own reset blockers already take.
void AMemberSomewhereElseIsNotAStraggler()
{
    std::vector<DungeonRunEntryState> const census = {
        Outside("Leader"), Elsewhere("Wanderer"), NotSeen("LoggedOut"),
        Inside("Straggler", 12.7f),
    };

    DungeonEvacuation const evacuation = DungeonRunEvacuation(census);
    CheckNames("only the one inside", evacuation.walk, {"Straggler"});
    CheckNames("and nobody is waited for", evacuation.wait, {});
}

// AN EMPTY CENSUS ANSWERS "NOBODY", AND THAT IS NOT THE FAIL-CLOSED THE ENTRY
// PREDICATES USE. DungeonRunEntryReady refuses an empty roster because knocking
// for a party that is not all there is the tank entering alone. This question is
// the other way round: the answer feeds a walk, and walking nobody is the right
// thing to do when there is nobody to walk.
void AnEmptyCensusWalksNobody()
{
    DungeonEvacuation const evacuation = DungeonRunEvacuation({});
    CheckNames("no walkers", evacuation.walk, {});
    CheckNames("no waiters", evacuation.wait, {});
}

// EVERYBODY THE EVACUATION NAMES IS SOMEBODY THE CROSSING ALREADY CALLS A
// BLOCKER. The invariant rather than the assertion: these two read the same
// census for different purposes, and a member walked out of a door that the
// crossing does not think is holding anything would mean one of them is wrong
// about who is where.
void EverybodyWalkedIsSomebodyTheCrossingIsWaitingOn()
{
    std::vector<DungeonRunEntryState> const census = {
        Outside("Leader"), Outside("Second"),
        Inside("Straggler", 12.7f), DeadInside("Corpse", 300.f),
        Elsewhere("Wanderer"),
    };

    std::string const blockers = DungeonRunEntryBlockers(census, 5.f);
    DungeonEvacuation const evacuation = DungeonRunEvacuation(census);
    for (std::string const& name : evacuation.walk)
        CheckBool("walked member is a named blocker",
                  blockers.find(name) != std::string::npos, true);
    for (std::string const& name : evacuation.wait)
        CheckBool("waited member is a named blocker",
                  blockers.find(name) != std::string::npos, true);
    CheckBool("and somebody out is not", blockers.find("Leader") == std::string::npos,
              true);
}

// ---------------------------------------------------------------------------

// THE HALF THAT IS NOT THE AIM, pinned here because writing the aim by hand
// looks like the whole fix and is not one at all: set on the measured pair by
// hand, the exit trigger's own coordinates moved neither of them in an hour.
//
// A cut-off follower may drive itself only on an errand that needs nobody else,
// and every aim shape this coordinator writes for a door is a coordinate, which
// SplitErrand deliberately calls one the family owns. So the exit aim walks
// nobody on its own: what makes it legal is the ESCORT, which is the other
// exemption. Widening this refusal instead would let any follower that has lost
// its family walk to any point anybody wrote, which is the scatter the refusal
// exists to prevent.
void TheExitAimAloneWouldNotWalkACutOffFollower()
{
    CheckBool("a trigger aim is one the family owns",
              OverseerDecisions::SplitFollowerDrivesItself("trigger:226"), false);
    CheckBool("and so is a point aim",
              OverseerDecisions::SplitFollowerDrivesItself("at:43:-172.2,139,-66.6"),
              false);
    CheckBool("while an errand that needs nobody is not",
              OverseerDecisions::SplitFollowerDrivesItself("innkeeper"), true);
}

// ---------------------------------------------------------------------------

// THE RULE THE WHOLE FIX TURNS ON. An arrival tolerance is where a walk STOPS,
// and an areatrigger fires on the server's own radius and nothing else, so a
// tolerance that is not strictly tighter than the radius is a character parked
// outside its own door with the errand reported as a success.
void AnAimOnlyOpensADoorItStopsInside()
{
    // The two doors this module aims at, quoted from the pinned core's own base
    // world DB (data/sql/base/db_world/areatrigger.sql):
    //   (78,0,-11208.5,1685.34,25.7612,7,0,0,0,0)     Deadmines, the way in
    //   (119,36,-14.3628,-393.38,64.5605,6,0,0,0,0)   Deadmines, the way out
    //   (226,43,-172.181,138.98,-66.6471,12,0,0,0,0)  Wailing Caverns, the way out
    CheckBool("5y inside a radius of 7", ArrivalReachesTrigger(ARRIVED_YARDS, 7.f), true);
    CheckBool("5y inside a radius of 6", ArrivalReachesTrigger(ARRIVED_YARDS, 6.f), true);
    CheckBool("5y inside a radius of 12", ArrivalReachesTrigger(ARRIVED_YARDS, 12.f),
              true);
}

// THE DEFECT THE TOLERANCE CONSTANT WAS WRITTEN FOR, pinned as a rule rather
// than left in a comment: twelve yards was once the tolerance for every aim,
// and arriving within twelve yards of a seven yard trigger is arriving OUTSIDE
// it - a clean-looking success at a door that never opens.
void ATooLooseToleranceArrivesOutsideTheDoor()
{
    CheckBool("12y against a radius of 7", ArrivalReachesTrigger(12.f, 7.f), false);
    CheckBool("12y against a radius of 6", ArrivalReachesTrigger(12.f, 6.f), false);
    // Equal is refused too, and deliberately: standing exactly on the edge of a
    // radius is a coin toss decided by the last step's pathing noise.
    CheckBool("exactly the radius", ArrivalReachesTrigger(12.f, 12.f), false);
}

// A TRIGGER WITH NO RADIUS IS A BOX, NOT A CIRCLE - the areatrigger table
// carries both shapes, and the Stockades entrance (101) is one of the boxes. The
// question has no answer for it, so it is refused rather than guessed at, and
// the caller's remedy is to aim nobody and say why.
void ADoorWithNoRadiusIsRefusedRatherThanGuessed()
{
    CheckBool("a box trigger", ArrivalReachesTrigger(ARRIVED_YARDS, 0.f), false);
    CheckBool("a negative radius", ArrivalReachesTrigger(ARRIVED_YARDS, -1.f), false);
    CheckBool("no tolerance at all", ArrivalReachesTrigger(0.f, 12.f), false);
}

// AND THE MEASUREMENT ITSELF, AS ARITHMETIC. This is the sharpest fact in the
// whole defect, it is not what it first looks like, and it deserves to be a
// number a test checks rather than a sentence in a comment.
//
// Quoted from the pinned core's own base world DB:
//   areatrigger.sql
//     (226,43,-172.181,138.98,-66.6471,12,0,0,0,0)
//   areatrigger_teleport.sql
//     (228,'The Barrens - Wailing Caverns',43,-163.49,132.9,-73.66,5.83)
//
// So a member that walks in through trigger 228 lands at
// (-163.49, 132.9, -73.66) - which is where the two measured members stood -
// and trigger 226 with its radius of 12 is the way back out.
void TheLandingPointIsAlreadyInsideTheWayOutOnceTheServersCheckIsUsed()
{
    float const landingX = -163.49f, landingY = 132.9f, landingZ = -73.66f;
    float const doorX = -172.181f, doorY = 138.98f, doorZ = -66.6471f;
    float const radius = 12.f;

    float const dx = landingX - doorX;
    float const dy = landingY - doorY;
    float const dz = landingZ - doorZ;
    float const stood = std::sqrt(dx * dx + dy * dy + dz * dz);

    // STRAIGHT LINE: outside, by seven tenths of a yard. This is the reading
    // that made the defect look like a walking problem, and it is the wrong
    // reading.
    CheckBool("the straight line is outside the radius", stood > radius, true);
    CheckBool("and only just", stood - radius < 1.f, true);

    // WHAT THE SERVER ACTUALLY COMPARES. Player::IsInAreaTriggerRadius
    // (Player.cpp:2216) measures with WorldObject::GetDistance
    // (Object.cpp:1311), which is the exact distance MINUS the character's own
    // size - GetObjectSize, Object.cpp:2892, which for a player is
    // scale * DEFAULT_COMBAT_REACH (Player.h:1104, ObjectDefines.h:45) and so
    // 1.5 at scale 1. Under that reading the pair were standing INSIDE their
    // own exit for the whole hour. Nothing had ever asked the door.
    float const combatReach = 1.5f;
    CheckBool("the server's own reading is inside the radius",
              stood - combatReach <= radius, true);

    // WHICH IS WHY THE FIX KNOCKS AT POLL RATE AND NOT ONLY ON ARRIVAL. An
    // errand only reports arriving at ARRIVED_YARDS, so a walk would not think
    // to ask for another eight yards. The door was open the whole time.
    CheckBool("the walk would not have asked yet", stood > ARRIVED_YARDS, true);

    // And when a straggler genuinely is far away, arriving at the aim still
    // puts it inside the door with room to spare, so the walk's own knock
    // lands too. Both halves, not one or the other.
    CheckBool("arriving at the aim is inside the door",
              ArrivalReachesTrigger(ARRIVED_YARDS, radius), true);
}

} // namespace

int main()
{
    TheTwoLeftInsideAreTheOnesWalkedOut();
    NobodyThatIsAlreadyOutIsWalkedAnywhere();
    ADeadMemberIsNamedRatherThanWalked();
    AMemberSomewhereElseIsNotAStraggler();
    AnEmptyCensusWalksNobody();
    EverybodyWalkedIsSomebodyTheCrossingIsWaitingOn();
    TheExitAimAloneWouldNotWalkACutOffFollower();
    AnAimOnlyOpensADoorItStopsInside();
    ATooLooseToleranceArrivesOutsideTheDoor();
    ADoorWithNoRadiusIsRefusedRatherThanGuessed();
    TheLandingPointIsAlreadyInsideTheWayOutOnceTheServersCheckIsUsed();
    if (!failures)
        std::printf("a member left inside is walked out, and an aim only opens a door it "
                    "stops inside\n");
    return failures ? 1 : 0;
}
