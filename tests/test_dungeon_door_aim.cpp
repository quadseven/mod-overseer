/*
 * A door aim goes on the floor, not in the middle of the box. mod-overseer#376.
 *
 * WHAT WAS MEASURED. A dungeon run on the dev realm cleared completely for the
 * first time - every encounter on map 43 credited, mask 255 of 255 - and the
 * party could not walk back out. EXIT got 0 of 5 through areatrigger 226 in
 * five minutes, and the travel drive underneath it refused every bearing:
 *
 *   'A' was sent to 'at:43:-172.181,138.98,-66.6471' and has been refused every
 *   bearing out of one spot for 8 polls running without moving a yard -
 *   releasing the errand as unreachable
 *
 * Those three floats are areatrigger 226's own row, which is the middle of the
 * trigger's BOX. The floor in that same chamber is z -73.66: it is where
 * areatrigger_teleport lands a character who walks IN through trigger 228, ten
 * yards away, and both rows are quoted in the adapter's portal table. The aim
 * was 7.01 yards in the air.
 *
 * WHY SEVEN YARDS IS FATAL RATHER THAN SLOPPY. PathGenerator calls an endpoint
 * more than seven yards from any polygon FARFROMPOLY_END, and
 * RoutedPathGoesWhereAsked refuses a route whose actual end misses the height
 * asked for by more than five. So no route to that point could be accepted from
 * anywhere on the map, at any distance, and every poll fell through to the
 * greedy five-bearing fan - which inside a cave is a straight line drawn
 * through rock. The party sat 125 yards from its own door until the crossing's
 * backstop fired, and the campaign counter stayed at zero because a run only
 * counts when the map empties.
 *
 * WHAT THIS FILE PINS. DoorAimOnTheFloor is ArrivalReachesTrigger in three
 * dimensions and nothing more: it moves an aim's z onto the ground under the
 * door, and only while a character that has ARRIVED at the moved aim would
 * still be inside the trigger. Every refusal keeps the trigger's own z, so a
 * door this cannot improve is left exactly as it was.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ArrivalReachesTrigger;
using OverseerDecisions::DoorAimHeight;
using OverseerDecisions::DoorAimOnTheFloor;

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
    if (diff <= 0.005f)
        return;
    std::printf("FAIL %s: got %.4f, wanted %.4f\n", what,
                static_cast<double>(got), static_cast<double>(want));
    ++failures;
}

// The adapter's own numbers, so a reader can check these against the source
// rather than against this file's memory of it.
constexpr float ARRIVAL = 5.f;        // TRAVEL_ARRIVED_POSITION_YARDS
constexpr bool FOUND = true;          // the surface probe answered
constexpr bool NOTHING_FOUND = false; // it did not

// areatrigger.sql: (226,43,-172.181,138.98,-66.6471,12,0,0,0,0)
constexpr float WAILING_EXIT_Z = -66.6471f;
constexpr float WAILING_EXIT_RADIUS = 12.f;
// areatrigger_teleport.sql: (228,'...',43,-163.49,132.9,-73.66,5.83)
constexpr float WAILING_FLOOR_Z = -73.66f;

// areatrigger.sql: (228,1,-753.596,-2212.78,21.5403,13,0,0,0,0)
constexpr float WAILING_ENTRANCE_Z = 21.5403f;
constexpr float WAILING_ENTRANCE_RADIUS = 13.f;

// ---------------------------------------------------------------------------

// The run that could not end, and the one number that ended it.
void TheWailingCavernsExitIsGrounded()
{
    DoorAimHeight const aim = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_FLOOR_Z, ARRIVAL, WAILING_EXIT_RADIUS);

    Check("the exit aim grounds", aim.grounded, true);
    CheckNear("it carries the floor, not the middle of the box", aim.z,
              WAILING_FLOOR_Z);
    // -66.6471 - -73.66
    CheckNear("and it says how far it moved", aim.correctionYards, 7.0129f);

    // The arithmetic the header quotes: sqrt(5^2 + 7.0129^2) = 8.61, against a
    // radius of 12. Room to spare, and the server subtracts a further 1.5 yards
    // of combat reach before it makes the same comparison.
    Check("a character arriving at the grounded aim is well inside the door",
          ARRIVAL * ARRIVAL + 7.0129f * 7.0129f <
              WAILING_EXIT_RADIUS * WAILING_EXIT_RADIUS,
          true);
}

// The ungrounded aim is what the drive was actually handed, and it is the thing
// the core could not route to. Nothing here fixes that except moving it.
void TheAimThatWasWalkedAtWasSevenYardsUp()
{
    float const gap = WAILING_EXIT_Z - WAILING_FLOOR_Z;
    CheckNear("the box's middle stands this far over the floor", gap, 7.0129f);
    Check("which is past the seven yards PathGenerator calls off the mesh",
          gap > 7.f, true);
    Check("and past the five TRAVEL_ROUTE_ENDPOINT_YARDS allows a route to miss",
          gap > 5.f, true);
    Check("and past the five TRAVEL_GROUND_SNAP_YARDS would have corrected",
          gap > 5.f, true);
}

// ---------------------------------------------------------------------------

void ADoorWithNoFloorUnderItKeepsItsOwnZ()
{
    DoorAimHeight const aim = DoorAimOnTheFloor(
        WAILING_EXIT_Z, NOTHING_FOUND, 0.f, ARRIVAL, WAILING_EXIT_RADIUS);

    Check("nothing was probed, so nothing is claimed", aim.grounded, false);
    CheckNear("the aim is exactly what it was before this existed", aim.z,
              WAILING_EXIT_Z);
    CheckNear("and it moved nothing", aim.correctionYards, 0.f);
}

// A radius of zero is a BOX trigger; the areatrigger table carries both shapes
// and this question has an answer for only one of them. Same refusal
// ArrivalReachesTrigger makes, for the same reason.
void ABoxTriggerIsRefusedRatherThanGuessedAt()
{
    DoorAimHeight const aim =
        DoorAimOnTheFloor(WAILING_EXIT_Z, FOUND, WAILING_FLOOR_Z, ARRIVAL, 0.f);

    Check("a box trigger does not ground", aim.grounded, false);
    CheckNear("and keeps its own z", aim.z, WAILING_EXIT_Z);

    DoorAimHeight const negative =
        DoorAimOnTheFloor(WAILING_EXIT_Z, FOUND, WAILING_FLOOR_Z, ARRIVAL, -1.f);
    Check("a negative radius is not a radius", negative.grounded, false);
    CheckNear("and keeps its own z too", negative.z, WAILING_EXIT_Z);
}

void AnArrivalToleranceOfZeroIsNotATolerance()
{
    DoorAimHeight const zero = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_FLOOR_Z, 0.f, WAILING_EXIT_RADIUS);
    Check("zero is refused", zero.grounded, false);
    CheckNear("keeping the trigger's z", zero.z, WAILING_EXIT_Z);

    DoorAimHeight const negative = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_FLOOR_Z, -3.f, WAILING_EXIT_RADIUS);
    Check("and so is a negative one", negative.grounded, false);
    CheckNear("keeping the trigger's z", negative.z, WAILING_EXIT_Z);
}

// #121's clifftop, arriving at this decision from the other side. A probe that
// answers with a surface tens of yards away has found ANOTHER surface, and
// moving the aim onto it would relocate the door rather than ground it.
void ACorrectionLargerThanTheDoorIsRefused()
{
    DoorAimHeight const aim = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_EXIT_Z - 27.f, ARRIVAL,
        WAILING_EXIT_RADIUS);

    Check("twenty-seven yards down is a different floor", aim.grounded, false);
    CheckNear("so the trigger's own z is kept", aim.z, WAILING_EXIT_Z);
    CheckNear("and nothing is reported as moved", aim.correctionYards, 0.f);
}

// The correction is a magnitude. A door at the foot of a ramp, whose floor
// reads ABOVE the middle of its box, is the same question.
void TheRuleIsSymmetricAboutTheBoxsMiddle()
{
    DoorAimHeight const below = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_EXIT_Z - 7.0129f, ARRIVAL,
        WAILING_EXIT_RADIUS);
    DoorAimHeight const above = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_EXIT_Z + 7.0129f, ARRIVAL,
        WAILING_EXIT_RADIUS);

    Check("a floor below grounds", below.grounded, true);
    Check("a floor above grounds on the same terms", above.grounded, true);
    CheckNear("and both report the same distance", below.correctionYards,
              above.correctionYards);

    DoorAimHeight const farBelow = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_EXIT_Z - 30.f, ARRIVAL,
        WAILING_EXIT_RADIUS);
    DoorAimHeight const farAbove = DoorAimOnTheFloor(
        WAILING_EXIT_Z, FOUND, WAILING_EXIT_Z + 30.f, ARRIVAL,
        WAILING_EXIT_RADIUS);
    Check("and both are refused at the same distance", farBelow.grounded,
          farAbove.grounded);
}

// ---------------------------------------------------------------------------

// THE BOUND IS THE HYPOTENUSE, AND IT IS ArrivalReachesTrigger'S OWN
// STRICTNESS. A right-angled triangle with legs 5 and 12 has hypotenuse 13, so
// against a radius of exactly 13 the corner is ON the circle and not inside it.
void TheBoundaryIsTheHypotenuseAndItIsStrict()
{
    DoorAimHeight const on =
        DoorAimOnTheFloor(0.f, FOUND, -12.f, 5.f, 13.f);
    Check("exactly on the circle is not inside it", on.grounded, false);
    CheckNear("so the aim does not move", on.z, 0.f);

    DoorAimHeight const inside =
        DoorAimOnTheFloor(0.f, FOUND, -11.9f, 5.f, 13.f);
    Check("a tenth of a yard inside it is", inside.grounded, true);
    CheckNear("and the aim takes the floor", inside.z, -11.9f);

    DoorAimHeight const outside =
        DoorAimOnTheFloor(0.f, FOUND, -12.1f, 5.f, 13.f);
    Check("a tenth of a yard outside it is not", outside.grounded, false);
}

// AT A CORRECTION OF ZERO IT IS THE RULE IT EXTENDS. This is what makes it an
// extension of ArrivalReachesTrigger rather than a second opinion about doors,
// so it is asserted rather than described.
void WithNothingToCorrectItIsArrivalReachesTrigger()
{
    static constexpr float ARRIVALS[] = {-1.f, 0.f, 1.f, 5.f, 6.9f, 7.f, 7.1f, 12.f, 40.f};
    static constexpr float RADII[] = {0.f, 6.f, 7.f, 12.f, 13.f};

    for (float arrival : ARRIVALS)
    {
        for (float radius : RADII)
        {
            // The floor is exactly the middle of the box: nothing to move.
            DoorAimHeight const aim =
                DoorAimOnTheFloor(-66.6471f, FOUND, -66.6471f, arrival, radius);
            Check("the two rules agree wherever there is no correction",
                  aim.grounded, ArrivalReachesTrigger(arrival, radius));
        }
    }
}

// ---------------------------------------------------------------------------

// THE DOOR THAT ALREADY WORKED MUST NOT STOP WORKING. ENTER crosses areatrigger
// 228 on open ground outside, and it has crossed it on every run this module
// has ever staged. A probe finding the terrace under it grounds the aim by a
// yard or two; a probe finding the hillside above the tunnel is refused, and
// the aim is the one ENTER has always had.
void TheEntranceDoorIsImprovedOrLeftAlone()
{
    DoorAimHeight const terrace = DoorAimOnTheFloor(
        WAILING_ENTRANCE_Z, FOUND, WAILING_ENTRANCE_Z - 1.4f, ARRIVAL,
        WAILING_ENTRANCE_RADIUS);
    Check("a yard and a half of ground grounds", terrace.grounded, true);
    CheckNear("onto the terrace", terrace.z, WAILING_ENTRANCE_Z - 1.4f);

    DoorAimHeight const hillside = DoorAimOnTheFloor(
        WAILING_ENTRANCE_Z, FOUND, WAILING_ENTRANCE_Z + 27.f, ARRIVAL,
        WAILING_ENTRANCE_RADIUS);
    Check("the clifftop over the shaft does not", hillside.grounded, false);
    CheckNear("so ENTER keeps the aim it has always crossed on", hillside.z,
              WAILING_ENTRANCE_Z);
}

// EVERY REFUSAL RETURNS THE OLD AIM, which is the property that makes this
// change safe to ship against four doors when only one of them was measured.
// Swept rather than spot-checked, because "it kept the z" is the whole of the
// no-regression claim.
void EveryRefusalKeepsTheTriggersOwnZ()
{
    static constexpr float GROUNDS[] = {-200000.f, -100.f, -20.f, -12.f, 0.f, 12.f, 100.f};
    static constexpr float RADII[] = {0.f, -4.f, 6.f, 12.f, 13.f};
    static constexpr float ARRIVALS[] = {-1.f, 0.f, 5.f, 40.f};
    static constexpr float TRIGGER_Z = -66.6471f;

    for (float ground : GROUNDS)
    {
        for (float radius : RADII)
        {
            for (float arrival : ARRIVALS)
            {
                DoorAimHeight const found =
                    DoorAimOnTheFloor(TRIGGER_Z, FOUND, ground, arrival, radius);
                if (!found.grounded)
                {
                    CheckNear("a refusal keeps the trigger's own z", found.z,
                              TRIGGER_Z);
                    CheckNear("and reports no correction", found.correctionYards,
                              0.f);
                }
                else
                {
                    CheckNear("and a grant carries the floor it was given",
                              found.z, ground);
                }

                // A probe that found nothing can never ground, whatever else
                // it was handed.
                DoorAimHeight const missing = DoorAimOnTheFloor(
                    TRIGGER_Z, NOTHING_FOUND, ground, arrival, radius);
                Check("no surface never grounds", missing.grounded, false);
                CheckNear("and never moves the aim", missing.z, TRIGGER_Z);
            }
        }
    }
}

}  // namespace

int main()
{
    TheWailingCavernsExitIsGrounded();
    TheAimThatWasWalkedAtWasSevenYardsUp();
    ADoorWithNoFloorUnderItKeepsItsOwnZ();
    ABoxTriggerIsRefusedRatherThanGuessedAt();
    AnArrivalToleranceOfZeroIsNotATolerance();
    ACorrectionLargerThanTheDoorIsRefused();
    TheRuleIsSymmetricAboutTheBoxsMiddle();
    TheBoundaryIsTheHypotenuseAndItIsStrict();
    WithNothingToCorrectItIsArrivalReachesTrigger();
    TheEntranceDoorIsImprovedOrLeftAlone();
    EveryRefusalKeepsTheTriggersOwnZ();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a door aim goes on the floor, not in the middle of the box\n");
    return EXIT_SUCCESS;
}
