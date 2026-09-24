/*
 * A short town stop on the campaign's approach, ordered under it (#639).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * WHAT WAS MEASURED ON THE DEV REALM, 2026-09-24, AND WHAT EACH CASE PINS.
 *
 *   - Grug held 22 letters with 2,609 gold in them and Zug 6 with 1,042, and
 *     the ten members had one item in their personal banks between them. The
 *     Horde family's campaign owned its head in the town it stages from, so
 *     nothing ever checked the post. HeadErrand::TownStop is the stop, and it
 *     sits under the approach: a mailbox in the town the head is passing may
 *     go while the run stages, a mailbox across the zone may not.
 *   - A bridge stop in the column refused the run's next leg claim, which the
 *     coordinator read as an errand written over: a WARN and a re-arm every
 *     poll, and GATHERING's twelve-minute backstop still running.
 *     StagingWaitsForTownStop says when the run waits for the stop instead,
 *     and for how long at most.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::HeadErrand;
using OverseerDecisions::HeadErrandMayTravel;
using OverseerDecisions::HeadErrandWaitReason;
using OverseerDecisions::HeadTravelFacts;
using OverseerDecisions::StagingWaitsForTownStop;
using OverseerDecisions::TOWN_STOP_MAX_SECONDS;
using OverseerDecisions::TOWN_STOP_NEAR_YARDS;

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

HeadTravelFacts Facts(bool inside, bool staging, bool bags, float yards)
{
    HeadTravelFacts f;
    f.runInside = inside;
    f.runStaging = staging;
    f.bagBlocked = bags;
    f.stopYards = yards;
    return f;
}

void AnIdleHeadMayStopAnywhere()
{
    Check("idle, near", HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, false, false, 40.f)),
          true);
    // Far is only far during staging; idle, the bridge's own queue decides.
    Check("idle, far", HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, false, false, 900.f)),
          true);
    Check("idle, unmeasured",
          HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, false, false, -1.f)), true);
}

void InsideTheRunThereIsNoStop()
{
    // Every counter is on another map, and the measured Zul'Farrak case sent
    // the head to 'banker' on map 209.
    Check("inside, near", HeadErrandMayTravel(HeadErrand::TownStop, Facts(true, false, false, 5.f)),
          false);
    Check("inside, bags", HeadErrandMayTravel(HeadErrand::TownStop, Facts(true, false, true, 5.f)),
          false);
}

void NoBagRoomLetsTheStopGo()
{
    // The family is in town for upkeep, and the post and the bank are upkeep.
    Check("bags, far", HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, true, true, 900.f)),
          true);
}

void StagingLetsOnlyANearStopThrough()
{
    Check("staging, in town",
          HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, true, false, 40.f)), true);
    Check("staging, at the edge",
          HeadErrandMayTravel(HeadErrand::TownStop,
                              Facts(false, true, false, TOWN_STOP_NEAR_YARDS)),
          true);
    Check("staging, just past it",
          HeadErrandMayTravel(HeadErrand::TownStop,
                              Facts(false, true, false, TOWN_STOP_NEAR_YARDS + 1.f)),
          false);
    Check("staging, unmeasured",
          HeadErrandMayTravel(HeadErrand::TownStop, Facts(false, true, false, -1.f)), false);
    Check("staging says why",
          std::string(HeadErrandWaitReason(HeadErrand::TownStop,
                                           Facts(false, true, false, 900.f)))
                  .find("town stop") != std::string::npos,
          true);
    // The stop does not lift anything else over the approach.
    Check("staging, bag upkeep still waits",
          HeadErrandMayTravel(HeadErrand::BagUpkeep, Facts(false, true, false, 40.f)), false);
    Check("staging, a trainer still waits",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, Facts(false, true, false, 40.f)), false);
    Check("staging, the regroup wait still waits",
          HeadErrandMayTravel(HeadErrand::Other, Facts(false, true, false, 40.f)), false);
}

void TheRunWaitsForAShortStopAndNoLonger()
{
    Check("a near foreign stop, just seen", StagingWaitsForTownStop(true, 40.f, 0), true);
    Check("a near foreign stop, inside its window",
          StagingWaitsForTownStop(true, 40.f, TOWN_STOP_MAX_SECONDS - 1), true);
    Check("a near foreign stop, window over",
          StagingWaitsForTownStop(true, 40.f, TOWN_STOP_MAX_SECONDS), false);
    Check("a far foreign errand", StagingWaitsForTownStop(true, 900.f, 0), false);
    Check("not resolved yet", StagingWaitsForTownStop(true, -1.f, 0), false);
    Check("the run's own aim", StagingWaitsForTownStop(false, 40.f, 0), false);
}

} // namespace

int main()
{
    AnIdleHeadMayStopAnywhere();
    InsideTheRunThereIsNoStop();
    NoBagRoomLetsTheStopGo();
    StagingLetsOnlyANearStopThrough();
    TheRunWaitsForAShortStopAndNoLonger();
    if (!failures)
        std::printf("a short town stop goes on the approach, under it, and no longer\n");
    return failures ? 1 : 0;
}
