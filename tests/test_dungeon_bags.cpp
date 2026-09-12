/*
 * A dungeon run the family has no room for (#429).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * WHAT THIS PINS, AND WHY EACH CASE IS HERE RATHER THAN BEING OBVIOUS. The
 * measured defect was not the arithmetic - "is anybody at or below the floor"
 * was already right - it was that the question had only one answer and could
 * only be asked from a place where that answer was already too late. Two
 * consecutive runs into map 189 opened and were evacuated six and eight seconds
 * later with nothing credited, each spending a slot of a twenty-five run
 * campaign, because the check ran only against members already on the instance
 * map and the drive that empties a bag is stood down whenever the coordinator
 * is not IDLE. So the cases below are mostly about WHERE the family is standing
 * and what that means, not about the comparison.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonBagPressure;
using OverseerDecisions::DungeonCampaignAfterRun;
using OverseerDecisions::DungeonCampaignProgress;
using OverseerDecisions::DungeonRunBagPressure;
using OverseerDecisions::DungeonRunEnteredTheInstance;
using OverseerDecisions::DungeonRunExitOutcome;
using OverseerDecisions::DungeonRunTrailingFailures;

namespace
{

int failures = 0;

char const* Word(DungeonBagPressure pressure)
{
    switch (pressure)
    {
        case DungeonBagPressure::None:     return "None";
        case DungeonBagPressure::HoldOut:  return "HoldOut";
        case DungeonBagPressure::Evacuate: return "Evacuate";
    }
    return "?";
}

void Check(char const* what, DungeonBagPressure got, DungeonBagPressure want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, Word(got), Word(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got, want);
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckCount(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, want %u\n", what, got, want);
    ++failures;
}

// The town trip's own floor, which is the number this decision must be handed.
// Written out here rather than included, because TownTripLimits lives on the
// adapter's constant and the point of the parameter is that the two agree.
constexpr unsigned FLOOR = 3;

void AFamilyWithRoomIsNeverTouched()
{
    Check("everybody comfortable, outside",
          DungeonRunBagPressure({20, 14, 31, 9, 27}, FLOOR, false, false),
          DungeonBagPressure::None);
    Check("everybody comfortable, inside",
          DungeonRunBagPressure({20, 14, 31, 9, 27}, FLOOR, true, false),
          DungeonBagPressure::None);
    // One slot above the floor is above the floor. The comparison is "at or
    // below", and a member with four free slots can still pick things up.
    Check("one above the floor",
          DungeonRunBagPressure({4, 4, 4, 4, 4}, FLOOR, false, false),
          DungeonBagPressure::None);
}

void NobodyCommittedMeansTheRunSimplyDoesNotOpen()
{
    // THE CASE THAT DID NOT EXIST BEFORE #429. This is the whole defect: the
    // old check could only be reached once somebody was already inside, so a
    // family that could not loot had no answer except to walk in and walk out
    // again.
    Check("one member at the floor, nobody inside",
          DungeonRunBagPressure({20, 3, 31, 9, 27}, FLOOR, false, false),
          DungeonBagPressure::HoldOut);
    Check("one member at zero, nobody inside",
          DungeonRunBagPressure({20, 0, 31, 9, 27}, FLOOR, false, false),
          DungeonBagPressure::HoldOut);
    Check("everybody full, nobody inside",
          DungeonRunBagPressure({0, 0, 0, 0, 0}, FLOOR, false, false),
          DungeonBagPressure::HoldOut);
}

void SomebodyInsideMeansTheOnlyWayOutIsTheDoor()
{
    // The answer this check always gave, kept: bags genuinely do fill DURING a
    // run, and that is the case the evacuation was built for.
    Check("one member at the floor, party inside",
          DungeonRunBagPressure({20, 3, 31}, FLOOR, true, false),
          DungeonBagPressure::Evacuate);
    Check("one member at zero, party inside",
          DungeonRunBagPressure({0}, FLOOR, true, false),
          DungeonBagPressure::Evacuate);
}

void ARunAlreadyWalkingToTheDoorIsNeverReDecided()
{
    // EXIT is the only thing that closes a run row and counts it. Answering
    // anything but None while it owns the party would leave a row 'active'
    // inside a dungeon with nobody walking anybody out, which is the stranding
    // shape the whole coordinator exists to end. Asked FIRST, so it holds
    // whatever else is true.
    Check("leaving, party inside, bags full",
          DungeonRunBagPressure({0, 0}, FLOOR, true, true),
          DungeonBagPressure::None);
    Check("leaving, nobody inside yet, bags full",
          DungeonRunBagPressure({0, 0}, FLOOR, false, true),
          DungeonBagPressure::None);
    Check("leaving, plenty of room",
          DungeonRunBagPressure({40}, FLOOR, true, true),
          DungeonBagPressure::None);
}

void AMemberNOBODYCouldReadIsUnknownAndNotFull()
{
    // The caller contributes no entry for a member that does not resolve to a
    // live character, the same rule TownNeed::present already states. An empty
    // list is a poll that learned nothing, and standing a campaign down on
    // that would stop a run every time the family is mid-relog.
    Check("nobody readable, outside",
          DungeonRunBagPressure({}, FLOOR, false, false),
          DungeonBagPressure::None);
    Check("nobody readable, inside",
          DungeonRunBagPressure({}, FLOOR, true, false),
          DungeonBagPressure::None);
    // And a partial reading decides on what it CAN see: four readable members
    // with room say nothing about the fifth that is logged out.
    Check("four readable, all with room",
          DungeonRunBagPressure({20, 14, 31, 9}, FLOOR, false, false),
          DungeonBagPressure::None);
}

void AFloorOfZeroIsStillARealBound()
{
    // Unlike the zero limits elsewhere in this file - a zero skip count, a zero
    // failure limit - zero here is not "unreadable, so do not act". A character
    // with zero free slots genuinely cannot pick anything up, so the question
    // is answerable and is answered.
    Check("zero floor, a member at zero",
          DungeonRunBagPressure({12, 0}, 0, false, false),
          DungeonBagPressure::HoldOut);
    Check("zero floor, a member at one",
          DungeonRunBagPressure({12, 1}, 0, false, false),
          DungeonBagPressure::None);
}

void TheFloorIsWhateverTheCallerHandsOver()
{
    // The parameter exists so the run's floor and the town trip's floor can be
    // the SAME number. It was 2 here and 3 there when this was measured, which
    // is a coordinator that would hold out for a trip the town drive did not
    // think was owed.
    Check("floor 2 lets three slots through",
          DungeonRunBagPressure({3}, 2, false, false),
          DungeonBagPressure::None);
    Check("floor 3 does not",
          DungeonRunBagPressure({3}, 3, false, false),
          DungeonBagPressure::HoldOut);
}

void TheRowSaysEvacuatedRatherThanLeft()
{
    CheckWord("bags ended it", DungeonRunExitOutcome(false, false, true), "evacuated");
    CheckWord("nothing special", DungeonRunExitOutcome(false, false, false), "left");
    // The order is the priority, and the two older words keep theirs.
    CheckWord("stall outranks bags", DungeonRunExitOutcome(false, true, true), "stalled");
    CheckWord("proof outranks both", DungeonRunExitOutcome(true, true, true), "complete");
    CheckWord("proof outranks bags", DungeonRunExitOutcome(true, false, true), "complete");
    // Nine characters into the VARCHAR(16) the outcome column already is, so
    // this word needs no migration - the same argument 'complete' shipped on.
    CheckCount("fits the column",
               static_cast<unsigned>(std::string("evacuated").size()) <= 16u ? 1u : 0u, 1u);
}

void AnEvacuatedRunSpendsNoSlotOfTheCampaign()
{
    // THE SECOND HALF OF THE MEASURED DEFECT. `dungeon_runs_done` moved for a
    // run that killed nothing, so a campaign of twenty-five would have burned
    // itself out in about twenty minutes with an empty instance table behind
    // it.
    CheckBool("evacuated is not a run", DungeonRunEnteredTheInstance("evacuated"), false);

    DungeonCampaignProgress const p = DungeonCampaignAfterRun("evacuated", 1, 25, true);
    CheckBool("not counted", p.counted, false);
    CheckCount("no slot filled", p.runsDone, 0u);
    CheckCount("the same slot is still to be made", p.nextRunNumber, 1u);
    CheckBool("the campaign is not over", p.campaignOver, false);

    // And the older words are untouched: a run that walked out on its own two
    // feet still fills its slot.
    DungeonCampaignProgress const left = DungeonCampaignAfterRun("left", 1, 25, true);
    CheckBool("'left' still counts", left.counted, true);
    CheckCount("'left' fills slot 1", left.runsDone, 1u);
}

void EvacuationsStillCountTowardTheFailureStop()
{
    // Something has to bound a family whose bags never drain. A run that spends
    // no slot spends no campaign either, which is exactly the hole #225 warned
    // about when it took the slot away from staging failures, so 'evacuated'
    // joins the trailing-failure count for the same reason they did.
    CheckCount("three in a row",
               DungeonRunTrailingFailures({"evacuated", "evacuated", "evacuated"}), 3u);
    // ...and one run that actually happened resets the streak, counting back
    // from the newest.
    CheckCount("a real run in the middle stops the count",
               DungeonRunTrailingFailures({"evacuated", "complete", "evacuated"}), 1u);
    CheckCount("mixed with the pre-entry failures",
               DungeonRunTrailingFailures({"evacuated", "staging_failed", "left"}), 2u);
}

} // namespace

int main()
{
    AFamilyWithRoomIsNeverTouched();
    NobodyCommittedMeansTheRunSimplyDoesNotOpen();
    SomebodyInsideMeansTheOnlyWayOutIsTheDoor();
    ARunAlreadyWalkingToTheDoorIsNeverReDecided();
    AMemberNOBODYCouldReadIsUnknownAndNotFull();
    AFloorOfZeroIsStillARealBound();
    TheFloorIsWhateverTheCallerHandsOver();
    TheRowSaysEvacuatedRatherThanLeft();
    AnEvacuatedRunSpendsNoSlotOfTheCampaign();
    EvacuationsStillCountTowardTheFailureStop();
    if (!failures)
        std::printf("a run the family has no room for does not open, and an evacuated "
                    "one spends no slot\n");
    return failures ? 1 : 0;
}
