/*
 * One owner of a head's travel column at a time, in a stated order (#631).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * WHAT WAS MEASURED ON THE DEV REALM, 2026-09-23, AND WHAT EACH CASE PINS.
 *
 *   - The Alliance family stood in Zul'Farrak with full bags on a run nobody
 *     had adopted. The bag check set EXIT with no portal, the next poll fell
 *     back to IDLE, and the loop wrote the run timeline every five seconds for
 *     over ninety minutes. DungeonEvacuationStart is the one decision that
 *     replaces that flip.
 *   - The Horde head was held by the regroup wait for twenty minutes while his
 *     family's run was staging, so three staging attempts failed in a row.
 *     HeadErrandMayTravel says the approach outranks the regroup wait.
 *   - The Alliance family walked back into the instance six seconds after it
 *     had been walked out for bag room. DungeonDoorShut keeps it out.
 *   - A talent reset waits for a run that is staging or inside, and goes when
 *     the head is idle or in town (JudgeRespec's RunOwnsTravel).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonDoorShut;
using OverseerDecisions::DungeonEvacuationStart;
using OverseerDecisions::EvacuationStart;
using OverseerDecisions::HeadErrand;
using OverseerDecisions::HeadErrandMayTravel;
using OverseerDecisions::HeadErrandWaitReason;
using OverseerDecisions::HeadTravelFacts;
using OverseerDecisions::JudgeRespec;
using OverseerDecisions::RespecFacts;
using OverseerDecisions::RespecStep;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

HeadTravelFacts Facts(bool inside, bool staging, bool bags)
{
    HeadTravelFacts f;
    f.runInside = inside;
    f.runStaging = staging;
    f.bagBlocked = bags;
    return f;
}

void AnIdleHeadMayBeTakenByAnyone()
{
    HeadTravelFacts const idle = Facts(false, false, false);
    for (HeadErrand who : {HeadErrand::ActiveRun, HeadErrand::CampaignApproach, HeadErrand::TownStop,
                           HeadErrand::BagUpkeep, HeadErrand::TrainerTrip, HeadErrand::Other})
        Check("idle head", HeadErrandMayTravel(who, idle), true);
    Check("no reason when it may", std::string(HeadErrandWaitReason(HeadErrand::Other, idle)).empty(),
          true);
}

void InsideTheRunNothingElseMovesTheHead()
{
    HeadTravelFacts const inside = Facts(true, false, true);
    Check("the run itself", HeadErrandMayTravel(HeadErrand::ActiveRun, inside), true);
    Check("not the approach", HeadErrandMayTravel(HeadErrand::CampaignApproach, inside), false);
    // The measured case: the bank pass sent the head to 'banker' on map 209.
    Check("not a bank errand", HeadErrandMayTravel(HeadErrand::BagUpkeep, inside), false);
    Check("not a trainer", HeadErrandMayTravel(HeadErrand::TrainerTrip, inside), false);
    Check("not the regroup wait", HeadErrandMayTravel(HeadErrand::Other, inside), false);
}

void StagingIsNotInterruptedByAnythingLower()
{
    HeadTravelFacts const staging = Facts(false, true, false);
    Check("the approach walks", HeadErrandMayTravel(HeadErrand::CampaignApproach, staging), true);
    Check("upkeep waits", HeadErrandMayTravel(HeadErrand::BagUpkeep, staging), false);
    Check("a talent reset waits", HeadErrandMayTravel(HeadErrand::TrainerTrip, staging), false);
    // The measured case: the regroup wait held Zug for twenty minutes.
    Check("the regroup wait does not pin him",
          HeadErrandMayTravel(HeadErrand::Other, staging), false);
    Check("and says the approach is why",
          std::string(HeadErrandWaitReason(HeadErrand::Other, staging)).find("staging") !=
              std::string::npos,
          true);
}

void NoBagRoomHandsTheFamilyToTown()
{
    // Staging and blocked cannot both hold in the module (the bag hold returns
    // the coordinator to IDLE), and blocked must win if they ever did.
    for (bool staging : {false, true})
    {
        HeadTravelFacts const blocked = Facts(false, staging, true);
        Check("the approach yields", HeadErrandMayTravel(HeadErrand::CampaignApproach, blocked),
              false);
        Check("upkeep goes", HeadErrandMayTravel(HeadErrand::BagUpkeep, blocked), true);
        Check("a trainer trip may ride along in town",
              HeadErrandMayTravel(HeadErrand::TrainerTrip, blocked), true);
        Check("the regroup wait still brings the family",
              HeadErrandMayTravel(HeadErrand::Other, blocked), true);
    }
    Check("and says bag room is why",
          std::string(HeadErrandWaitReason(HeadErrand::CampaignApproach, Facts(false, false, true)))
                  .find("bag room") != std::string::npos,
          true);
}

void AnEvacuationFirstAsksWhoKnowsTheDoor()
{
    Check("a driven run walks out",
          DungeonEvacuationStart(true, true) == EvacuationStart::WalkOut, true);
    Check("a driven run walks out whatever the map says",
          DungeonEvacuationStart(true, false) == EvacuationStart::WalkOut, true);
    // The measured case: IDLE, standing in map 209, which has a portal row.
    Check("an unowned run is adopted before EXIT",
          DungeonEvacuationStart(false, true) == EvacuationStart::AdoptThenWalkOut, true);
    Check("no door known: hold, do not flip",
          DungeonEvacuationStart(false, false) == EvacuationStart::NoWayOut, true);
}

void TheDoorStaysShutOnlyForAFamilyHeldForBags()
{
    Check("held, alive, steered, entering: shut", DungeonDoorShut(true, true, true, true), true);
    Check("room again: open", DungeonDoorShut(false, true, true, true), false);
    Check("not a dungeon map: open", DungeonDoorShut(true, false, true, true), false);
    Check("a ghost is never refused", DungeonDoorShut(true, true, false, true), false);
    Check("a character this module does not steer is never refused",
          DungeonDoorShut(true, true, true, false), false);
}

RespecFacts ZugAtTwentyEight()
{
    RespecFacts f;
    f.specTab = 2;
    f.level = 28;
    f.pointsByTree[0] = 18;
    f.money = 20000;
    f.cost = 10000;
    f.available = true;
    f.columnFree = true;
    return f;
}

void ATalentResetWaitsForTheRun()
{
    RespecFacts idle = ZugAtTwentyEight();
    Check("idle: walks", JudgeRespec(idle) == RespecStep::Walk, true);
    RespecFacts staging = idle;
    staging.runOwnsTravel = true;
    Check("staging: waits for the run", JudgeRespec(staging) == RespecStep::RunOwnsTravel, true);
    // The purse is still asked first: a reset he cannot pay for is said as that.
    staging.money = 9914;
    Check("the price is still the first answer",
          JudgeRespec(staging) == RespecStep::CannotAfford, true);
}

} // namespace

int main()
{
    AnIdleHeadMayBeTakenByAnyone();
    InsideTheRunNothingElseMovesTheHead();
    StagingIsNotInterruptedByAnythingLower();
    NoBagRoomHandsTheFamilyToTown();
    AnEvacuationFirstAsksWhoKnowsTheDoor();
    TheDoorStaysShutOnlyForAFamilyHeldForBags();
    ATalentResetWaitsForTheRun();
    if (!failures)
        std::printf("one owner of a head's travel column at a time, in the stated order\n");
    return failures ? 1 : 0;
}
