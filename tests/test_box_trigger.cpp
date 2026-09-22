/*
 * A door that is a box. mod-overseer#577.
 *
 * WHAT WAS WRONG. The areatrigger table carries two shapes. A row with a
 * radius is a sphere; a row with a radius of 0 is a box with a length, a width,
 * a height and an orientation. Every decision this module makes about a door
 * took a radius, and ArrivalReachesTrigger answered "no" for a radius of 0, so
 * the exit walk and the walk back in refused every box door. Ten classic doors
 * have a box on the way out (Ragefire Chasm, both Maraudon wings, six Dire Maul
 * doors, Scholomance), so none of them could have a portal row: a run through
 * one would have entered and never walked out.
 *
 * WHAT THIS FILE PINS. The core's own box test, mirrored and checked against
 * the real rows (2226 and 3196, plus 2230 for an orientation that is not 0);
 * the arrival question for a box; the tolerance a walk at a small box is
 * handed; grounding an aim under a box; and the staging standoff a box gets.
 *
 * Every number is a row read out of the world database on the pinned core,
 * quoted in the adapter's portal table beside the door it belongs to.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::AreaTriggerForm;
using OverseerDecisions::AreaTriggerFormOf;
using OverseerDecisions::AreaTriggerInscribedYards;
using OverseerDecisions::AreaTriggerReachYards;
using OverseerDecisions::AreaTriggerShape;
using OverseerDecisions::ArrivalReachesTrigger;
using OverseerDecisions::DoorAimHeight;
using OverseerDecisions::DoorAimOnTheFloor;
using OverseerDecisions::DoorArrivalYards;
using OverseerDecisions::DungeonStagingStandoffYards;
using OverseerDecisions::InsideAreaTrigger;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const diff = got - want > 0.f ? got - want : want - got;
    if (diff <= 0.002f)
        return;
    std::printf("FAIL %s: got %.4f, wanted %.4f\n", what, static_cast<double>(got),
                static_cast<double>(want));
    ++failures;
}

// The adapter's numbers, so a reader can check them against the source.
constexpr float ARRIVAL = 5.f;      // TRAVEL_ARRIVED_POSITION_YARDS
constexpr float STANDOFF = 20.f;    // DUNGEON_STAGING_STANDOFF_YARDS
constexpr float BARRIER = 10.f;     // DUNGEON_BARRIER_RADIUS_YARDS
constexpr float PLAYER_SIZE = 1.5f; // scale * DEFAULT_COMBAT_REACH at scale 1

AreaTriggerShape Row(float x, float y, float z, float radius, float length, float width,
                     float height, float orientation)
{
    AreaTriggerShape shape;
    shape.x = x;
    shape.y = y;
    shape.z = z;
    shape.radius = radius;
    shape.length = length;
    shape.width = width;
    shape.height = height;
    shape.orientation = orientation;
    return shape;
}

// areatrigger: (2226,389,2.58019,-0.013587,-13.3668,0,30.69,12.19,25.56,0)
// Ragefire Chasm's way out.
AreaTriggerShape const RAGEFIRE_EXIT =
    Row(2.58019f, -0.013587f, -13.3668f, 0.f, 30.69f, 12.19f, 25.56f, 0.f);
// areatrigger: (3196,429,4.31119,-837.085,-33.0405,0,6.167,11.44,22.94,0)
// Dire Maul East's east door, from the inside.
AreaTriggerShape const DIRE_MAUL_EAST_EXIT =
    Row(4.31119f, -837.085f, -33.0405f, 0.f, 6.167f, 11.44f, 22.94f, 0.f);
// areatrigger: (2230,1,1818.4,-4427.26,-10.4478,0,21.69,11.83,21.22,0.576)
// Ragefire Chasm's way in, turned 0.576 radians.
AreaTriggerShape const RAGEFIRE_ENTRY =
    Row(1818.4f, -4427.26f, -10.4478f, 0.f, 21.69f, 11.83f, 21.22f, 0.576f);
// areatrigger: (2221,329,3584.78,-3632.05,142.118,10,9.778,17.94,27.92,0)
// Stratholme's way out: a radius AND box extents, which the core reads as a
// sphere because it asks the radius first.
AreaTriggerShape const STRATHOLME_EXIT =
    Row(3584.78f, -3632.05f, 142.118f, 10.f, 9.778f, 17.94f, 27.92f, 0.f);
// areatrigger: (228,1,-753.596,-2212.78,21.5403,13,0,0,0,0)
AreaTriggerShape const WAILING_ENTRY = Row(-753.596f, -2212.78f, 21.5403f, 13.f, 0.f, 0.f, 0.f, 0.f);
// areatrigger: (194,33,-230.953,2105.06,79.7533,5,0,0,0,0)
AreaTriggerShape const SHADOWFANG_EXIT = Row(-230.953f, 2105.06f, 79.7533f, 5.f, 0.f, 0.f, 0.f, 0.f);

// ---------------------------------------------------------------------------

void TheShapeIsReadTheWayTheCoreReadsIt()
{
    Check("2226 is a box", AreaTriggerFormOf(RAGEFIRE_EXIT) == AreaTriggerForm::Box, true);
    Check("2221 carries a radius, so it is a sphere",
          AreaTriggerFormOf(STRATHOLME_EXIT) == AreaTriggerForm::Sphere, true);
    Check("a row with neither is nothing",
          AreaTriggerFormOf(Row(1.f, 2.f, 3.f, 0.f, 0.f, 5.f, 5.f, 0.f)) ==
              AreaTriggerForm::Nothing,
          true);
}

// Position::IsWithinBox against the real 2226: half-extents 15.345 along x,
// 6.095 along y and 12.78 up and down, orientation 0.
void TheRagefireExitBoxIsTheSizeItsRowSays()
{
    AreaTriggerShape const& box = RAGEFIRE_EXIT;
    Check("the centre", InsideAreaTrigger(box, box.x, box.y, box.z, PLAYER_SIZE), true);
    Check("15 along the length", InsideAreaTrigger(box, box.x + 15.f, box.y, box.z, PLAYER_SIZE), true);
    Check("16 along the length", InsideAreaTrigger(box, box.x + 16.f, box.y, box.z, PLAYER_SIZE), false);
    Check("6 across", InsideAreaTrigger(box, box.x, box.y - 6.f, box.z, PLAYER_SIZE), true);
    // The sphere would take 1.5 yards of combat reach off first. The box does
    // not: 6.2 across is outside a 6.095 half-width whatever size the
    // character is.
    Check("6.2 across, even for a large character",
          InsideAreaTrigger(box, box.x, box.y - 6.2f, box.z, 5.f), false);
    Check("12 down", InsideAreaTrigger(box, box.x, box.y, box.z - 12.f, PLAYER_SIZE), true);
    Check("13 down", InsideAreaTrigger(box, box.x, box.y, box.z - 13.f, PLAYER_SIZE), false);
}

// The orientation turns the box. 2230 is turned 0.576 radians, so ten yards
// along that bearing is along its 21.69 length and inside, and ten yards at
// right angles to it is across its 11.83 width and outside. With the rotation
// dropped, both would be the same answer.
void AnOrientationTurnsTheBox()
{
    AreaTriggerShape const& box = RAGEFIRE_ENTRY;
    float const c = std::cos(box.orientation);
    float const s = std::sin(box.orientation);
    Check("ten yards along the turned length",
          InsideAreaTrigger(box, box.x + 10.f * c, box.y + 10.f * s, box.z, PLAYER_SIZE), true);
    Check("ten yards across the turned width",
          InsideAreaTrigger(box, box.x - 10.f * s, box.y + 10.f * c, box.z, PLAYER_SIZE), false);
    // Ten along and four across the turned box is inside it; the same point
    // against the box with its turn dropped is 8.8 yards across a 5.915 half
    // width and outside. Only the rotation tells the two apart.
    float const px = box.x + 10.f * c - 4.f * s;
    float const py = box.y + 10.f * s + 4.f * c;
    AreaTriggerShape unturned = box;
    unturned.orientation = 0.f;
    Check("ten along and four across the turned box",
          InsideAreaTrigger(box, px, py, box.z, PLAYER_SIZE), true);
    Check("is outside the same box unturned",
          InsideAreaTrigger(unturned, px, py, box.z, PLAYER_SIZE), false);
}

// The sphere branch keeps the combat reach, exactly as the radius overloads
// and the Wailing Caverns arithmetic already say.
void ASphereStillSubtractsTheCharactersSize()
{
    AreaTriggerShape const& door = STRATHOLME_EXIT;
    Check("11 yards from a radius 10 sphere, less 1.5 of reach",
          InsideAreaTrigger(door, door.x + 11.f, door.y, door.z, PLAYER_SIZE), true);
    Check("12 yards is not", InsideAreaTrigger(door, door.x + 12.f, door.y, door.z, PLAYER_SIZE),
          false);
}

// The acceptance criterion's two real boxes: one the default tolerance fits
// inside and one it does not.
void ArrivalAtABoxIsAskedOfItsShorterSide()
{
    CheckNear("2226's shorter half-side", AreaTriggerInscribedYards(RAGEFIRE_EXIT), 6.095f);
    Check("5 yards fits inside 2226", ArrivalReachesTrigger(ARRIVAL, RAGEFIRE_EXIT), true);

    CheckNear("3196's shorter half-side", AreaTriggerInscribedYards(DIRE_MAUL_EAST_EXIT), 3.0835f);
    Check("5 yards does not fit inside 3196",
          ArrivalReachesTrigger(ARRIVAL, DIRE_MAUL_EAST_EXIT), false);

    // The radius overload is unchanged: a radius of 0 is still refused, so a
    // caller that has not been given the shape cannot get a box wrong.
    Check("the radius overload still refuses a box", ArrivalReachesTrigger(ARRIVAL, 0.f), false);
    Check("and the shape overload of a sphere is the radius overload",
          ArrivalReachesTrigger(ARRIVAL, WAILING_ENTRY), ArrivalReachesTrigger(ARRIVAL, 13.f));
}

// What a walk at the door is handed.
void ASmallBoxIsWalkedAtOnATighterTolerance()
{
    CheckNear("2226 takes the default", DoorArrivalYards(ARRIVAL, RAGEFIRE_EXIT), 5.f);
    float const tight = DoorArrivalYards(ARRIVAL, DIRE_MAUL_EAST_EXIT);
    CheckNear("3196 takes its half-side less a yard", tight, 2.0835f);
    Check("which the arrival question then accepts",
          ArrivalReachesTrigger(tight, DIRE_MAUL_EAST_EXIT), true);
    Check("and a character stopped that far out on any bearing is inside",
          InsideAreaTrigger(DIRE_MAUL_EAST_EXIT, DIRE_MAUL_EAST_EXIT.x + tight,
                            DIRE_MAUL_EAST_EXIT.y, DIRE_MAUL_EAST_EXIT.z, 0.f) &&
              InsideAreaTrigger(DIRE_MAUL_EAST_EXIT, DIRE_MAUL_EAST_EXIT.x,
                                DIRE_MAUL_EAST_EXIT.y + tight, DIRE_MAUL_EAST_EXIT.z, 0.f),
          true);

    CheckNear("a sphere the default fits keeps it", DoorArrivalYards(ARRIVAL, WAILING_ENTRY), 5.f);
    CheckNear("Shadowfang's radius 5 way out is walked on 4",
              DoorArrivalYards(ARRIVAL, SHADOWFANG_EXIT), 4.f);

    AreaTriggerShape const sliver = Row(0.f, 0.f, 0.f, 0.f, 3.f, 20.f, 10.f, 0.f);
    CheckNear("a box too thin for any tolerance gets none", DoorArrivalYards(ARRIVAL, sliver), 0.f);
    Check("and is refused", ArrivalReachesTrigger(DoorArrivalYards(ARRIVAL, sliver), sliver), false);
}

// The aim at a box goes on the floor when the floor is inside the box.
void ABoxAimGoesOnTheFloor()
{
    // areatrigger_teleport: (3185,...,429,9.31119,-837.085,-32.5305,0) lands a
    // character on the floor five yards from 3196, at z -32.5305.
    DoorAimHeight const aim =
        DoorAimOnTheFloor(true, -32.5305f, DoorArrivalYards(ARRIVAL, DIRE_MAUL_EAST_EXIT),
                          DIRE_MAUL_EAST_EXIT);
    Check("3196's aim grounds", aim.grounded, true);
    CheckNear("on the floor", aim.z, -32.5305f);
    CheckNear("0.51 yards from the row", aim.correctionYards, 0.51f);

    // areatrigger_teleport: (2230,...,389,3.81,-14.82,-17.84,4.39) lands in
    // front of 2226 at z -17.84, 4.47 yards under its centre.
    DoorAimHeight const ragefire = DoorAimOnTheFloor(true, -17.84f, ARRIVAL, RAGEFIRE_EXIT);
    Check("2226's aim grounds", ragefire.grounded, true);
    CheckNear("on the floor", ragefire.z, -17.84f);

    DoorAimHeight const below =
        DoorAimOnTheFloor(true, DIRE_MAUL_EAST_EXIT.z - 12.f, 2.f, DIRE_MAUL_EAST_EXIT);
    Check("a floor under the box's bottom face is not its floor", below.grounded, false);
    CheckNear("and the row's z is kept", below.z, DIRE_MAUL_EAST_EXIT.z);

    DoorAimHeight const nothing = DoorAimOnTheFloor(false, 0.f, 2.f, DIRE_MAUL_EAST_EXIT);
    Check("no surface found keeps the row's z", nothing.grounded, false);
    CheckNear("the row's z", nothing.z, DIRE_MAUL_EAST_EXIT.z);

    // A sphere through the shape overload is the radius overload.
    DoorAimHeight const viaShape = DoorAimOnTheFloor(true, 17.f, ARRIVAL, WAILING_ENTRY);
    DoorAimHeight const viaRadius = DoorAimOnTheFloor(WAILING_ENTRY.z, true, 17.f, ARRIVAL, 13.f);
    Check("a sphere grounds the same either way", viaShape.grounded, viaRadius.grounded);
    CheckNear("to the same z", viaShape.z, viaRadius.z);
}

// A box stands off by its half-diagonal plus the gather circle when that is
// more than the flat standoff, and a sphere keeps the flat standoff.
void ABoxStandsOffByItsOwnSize()
{
    CheckNear("2230's half-diagonal", AreaTriggerReachYards(RAGEFIRE_ENTRY), 12.3532f);
    CheckNear("so Ragefire stages 22.35 out",
              DungeonStagingStandoffYards(RAGEFIRE_ENTRY, STANDOFF, BARRIER), 22.3532f);
    CheckNear("a small box keeps the flat 20",
              DungeonStagingStandoffYards(DIRE_MAUL_EAST_EXIT, STANDOFF, BARRIER), 20.f);
    CheckNear("a sphere keeps the flat 20 whatever its radius",
              DungeonStagingStandoffYards(WAILING_ENTRY, STANDOFF, BARRIER), 20.f);
}

}  // namespace

int main()
{
    TheShapeIsReadTheWayTheCoreReadsIt();
    TheRagefireExitBoxIsTheSizeItsRowSays();
    AnOrientationTurnsTheBox();
    ASphereStillSubtractsTheCharactersSize();
    ArrivalAtABoxIsAskedOfItsShorterSide();
    ASmallBoxIsWalkedAtOnATighterTolerance();
    ABoxAimGoesOnTheFloor();
    ABoxStandsOffByItsOwnSize();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
