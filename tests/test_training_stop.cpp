/*
 * A training stop in the town a campaign waits in (mod-overseer#688).
 *
 * Measured on the dev realm on 2026-09-24: the Horde family's ten profession
 * learns had waited 32 hours behind its armed Ragefire campaign, the family
 * standing in Orgrimmar. From where it waits the trainers its learns need are
 * 265 (enchanting) to 639 (blacksmithing) yards away and the warrior trainer
 * Grezz Ragefist 564; the head carries mining (186), Oz tailoring (197), Uzza
 * herbalism (182), Zork skinning (393) and Zrog mining (186), and Zrog was 1,772
 * yards behind the head.
 *
 * The last part reads src/mod_overseer.cpp (run from the repo root) to pin the
 * poll, the claim owner and the arrival order.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::HeadErrand;
using OverseerDecisions::HeadErrandMayTravel;
using OverseerDecisions::HeadTravelFacts;
using OverseerDecisions::PickTrainingStopLeg;
using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TOWN_STOP_NEAR_YARDS;
using OverseerDecisions::TRAINING_STOP_MAX_SECONDS;
using OverseerDecisions::TRAINING_STOP_REST_SECONDS;
using OverseerDecisions::TRAINING_STOP_YARDS;
using OverseerDecisions::TrainingStopEnds;
using OverseerDecisions::TrainingStopFacts;
using OverseerDecisions::TrainingStopLeg;
using OverseerDecisions::TrainingStopMember;
using OverseerDecisions::TrainingStopStep;
using OverseerDecisions::TrainingStopStepWord;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelOwner;
using OverseerDecisions::TravelOwnerIsALiveRun;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

TrainingStopMember Member(char const* name, uint32_t skill, float trainerYards,
                          bool withTheHead = true)
{
    TrainingStopMember member;
    member.name = name;
    member.learnSkill = skill;
    member.trainerYards = trainerYards;
    member.withTheHead = withTheHead;
    return member;
}

// The Horde family as measured, head first.
std::vector<TrainingStopMember> Horde()
{
    return {
        Member("Zug", 186, 522.f),
        Member("Oz", 197, 281.f),
        Member("Uzza", 182, 270.f),
        Member("Zork", 393, 300.f),
        Member("Zrog", 186, 522.f, false),  // 1,772 yards behind
    };
}

// Held in Orgrimmar for bag room, the campaign armed, the column empty.
TrainingStopFacts HeldInTown()
{
    TrainingStopFacts facts;
    facts.campaignArmed = true;
    facts.head.bagBlocked = true;
    facts.columnFree = true;
    return facts;
}

void TheTownIsTheCity()
{
    HeadTravelFacts between;
    between.campaignBetweenAttempts = true;
    between.trainerYards = 564.f;  // Grezz Ragefist from where the family waits
    Check("between attempts, the warrior trainer across Orgrimmar may take the head",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
    between.trainerYards = 639.f;  // the blacksmithing trainer
    Check("...and the farthest profession trainer in the city", HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
    between.trainerYards = TRAINING_STOP_YARDS + 1.f;
    Check("past the city it waits", !HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
    between.trainerYards = 2375.f;  // #663's measured walk
    Check("#663's 2,375-yard walk still waits", !HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
    between.trainerYards = -1.f;
    Check("unmeasured is not near", !HeadErrandMayTravel(HeadErrand::TrainerTrip, between));

    HeadTravelFacts staging;
    staging.runStaging = true;
    staging.stopYards = TOWN_STOP_NEAR_YARDS + 1.f;
    Check("a counter stop on the approach keeps its own 150 yards",
          !HeadErrandMayTravel(HeadErrand::TownStop, staging));
    staging.trainerYards = 10.f;
    Check("a trainer trip never goes while a run stages",
          !HeadErrandMayTravel(HeadErrand::TrainerTrip, staging));
}

void TheStopClaimsLikeTheReset()
{
    TravelClaimFacts empty(TravelOwner::TrainingStop);
    empty.learnSkill = 186;  // the head's own mining learn
    empty.target = "3363";
    Check("an empty column with a learn pending is written",
          ReadTravelClaim(empty) == TravelClaim::Write);

    TravelClaimFacts trainerWalk(TravelOwner::TrainingStop);
    trainerWalk.learnSkill = 186;
    trainerWalk.column = "profession trainer";
    trainerWalk.target = "3363";
    Check("a bridge trainer walk in the column is not written over",
          ReadTravelClaim(trainerWalk) == TravelClaim::RefusedProfession);

    TravelClaimFacts vendor(TravelOwner::TrainingStop);
    vendor.column = "vendor";
    vendor.target = "3363";
    Check("a counter errand in the column is not written over",
          ReadTravelClaim(vendor) == TravelClaim::RefusedForeign);

    Check("a training stop is not a live run", !TravelOwnerIsALiveRun(TravelOwner::TrainingStop));
}

void TheHeadWalksTheFamilyTrainerByTrainer()
{
    std::vector<TrainingStopMember> family = Horde();
    TrainingStopLeg leg = PickTrainingStopLeg(HeldInTown(), family);
    Check("held in town, the stop walks the head's own learn first",
          leg.step == TrainingStopStep::Walk && leg.member == 0);

    family[0].walkedThisStop = true;
    TrainingStopFacts open = HeldInTown();
    open.stopSeconds = 120;
    leg = PickTrainingStopLeg(open, family);
    Check("then the next member by the family's order",
          leg.step == TrainingStopStep::Walk && leg.member == 1);

    family[1].walkedThisStop = true;
    family[2].walkedThisStop = true;
    family[3].walkedThisStop = true;
    leg = PickTrainingStopLeg(open, family);
    Check("a member away from the family is not walked for",
          leg.step == TrainingStopStep::NobodyWithTheHead);
    Check("and the open stop ends", TrainingStopEnds(leg.step, true));

    family[4].withTheHead = true;
    leg = PickTrainingStopLeg(open, family);
    Check("once it is back with the family it is",
          leg.step == TrainingStopStep::Walk && leg.member == 4);

    family[4].walkedThisStop = true;
    leg = PickTrainingStopLeg(open, family);
    Check("everybody walked for: nothing left", leg.step == TrainingStopStep::NothingToLearn);
    Check("which ends the stop", TrainingStopEnds(leg.step, true));

    TrainingStopFacts between;
    between.campaignArmed = true;
    between.head.campaignBetweenAttempts = true;
    between.columnFree = true;
    leg = PickTrainingStopLeg(between, Horde());
    Check("between attempts it walks too", leg.step == TrainingStopStep::Walk && leg.member == 0);
}

void ItNeverTakesTheRunsTurn()
{
    TrainingStopFacts staging = HeldInTown();
    staging.head.bagBlocked = false;
    staging.head.runStaging = true;
    Check("a staging run keeps the head",
          PickTrainingStopLeg(staging, Horde()).step == TrainingStopStep::RunOwnsTravel);
    TrainingStopFacts inside = HeldInTown();
    inside.head.bagBlocked = false;
    inside.head.runInside = true;
    Check("a run inside keeps the head",
          PickTrainingStopLeg(inside, Horde()).step == TrainingStopStep::RunOwnsTravel);
    Check("and an open stop ends for it", TrainingStopEnds(TrainingStopStep::RunOwnsTravel, true));

    TrainingStopFacts questing = HeldInTown();
    questing.campaignArmed = false;
    Check("with no campaign armed the bridge's learn trips walk",
          PickTrainingStopLeg(questing, Horde()).step == TrainingStopStep::NotACampaign);
}

void ItIsBounded()
{
    TrainingStopFacts rested = HeldInTown();
    rested.sinceLastStop = TRAINING_STOP_REST_SECONDS - 1;
    Check("a new stop rests after the last one",
          PickTrainingStopLeg(rested, Horde()).step == TrainingStopStep::Resting);
    rested.sinceLastStop = TRAINING_STOP_REST_SECONDS;
    Check("and may start once the rest is over",
          PickTrainingStopLeg(rested, Horde()).step == TrainingStopStep::Walk);

    TrainingStopFacts spent = HeldInTown();
    spent.stopSeconds = TRAINING_STOP_MAX_SECONDS;
    Check("an open stop has its fifteen minutes",
          PickTrainingStopLeg(spent, Horde()).step == TrainingStopStep::SpentItsTime);
    Check("and then ends", TrainingStopEnds(TrainingStopStep::SpentItsTime, true));

    std::vector<TrainingStopMember> far = Horde();
    for (TrainingStopMember& member : far)
        member.trainerYards = 2375.f;
    Check("trainers past the city are not walked to",
          PickTrainingStopLeg(HeldInTown(), far).step == TrainingStopStep::NoTrainerInTown);
    far[1].trainerYards = -1.f;
    Check("nor a learn no trainer on the map teaches",
          PickTrainingStopLeg(HeldInTown(), far).step == TrainingStopStep::NoTrainerInTown);

    TrainingStopFacts taken = HeldInTown();
    taken.columnFree = false;
    Check("a taken column waits",
          PickTrainingStopLeg(taken, Horde()).step == TrainingStopStep::ColumnTaken);
    Check("without ending the stop", !TrainingStopEnds(TrainingStopStep::ColumnTaken, true));
    Check("a walk does not end it", !TrainingStopEnds(TrainingStopStep::Walk, true));
    Check("nothing ends a stop that is not open", !TrainingStopEnds(TrainingStopStep::SpentItsTime, false));

    Check("every step has a sentence",
          std::string(TrainingStopStepWord(TrainingStopStep::NoTrainerInTown)).find("700") !=
              std::string::npos);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::size_t const respecPoll = source.find("            DriveRespec();\n");
    std::size_t const stopPoll = source.find("            DriveTrainingStop();\n");
    Check("the stop runs on the train poll after the respec",
          respecPoll != std::string::npos && stopPoll != std::string::npos && stopPoll > respecPoll);
    Check("its claim names its owner",
          source.find("OverseerDecisions::TravelOwner::TrainingStop))") != std::string::npos);
    std::size_t const stopArrival = source.find("if (TeachAtTrainingStop(stopLeg->second))");
    std::size_t const respecArrival = source.find("if (RespecOnArrival(name, bot, entry, respec->second))");
    std::size_t const learnArrival = source.find("!TrainOnArrival(name, bot, entry, *plan))");
    Check("the stop's arrival is answered before the respec and the head's own learn",
          stopArrival != std::string::npos && respecArrival != std::string::npos &&
              learnArrival != std::string::npos && stopArrival < respecArrival &&
              stopArrival < learnArrival);
    Check("a member is taught only by a trainer that teaches it",
          source.find("!TrainerSpellForSkill(trainer, bot, plan->second.learnSkill))") !=
              std::string::npos);
    Check("the stop is said", source.find("overseer: TRAINING STOP for {} {}") != std::string::npos);
    Check("a leg cut short gives its member one more leg in the stop",
          source.find("if (stop.retried.insert(walking->second.forMember).second)\n"
                      "                    stop.walked.erase(walking->second.forMember);") !=
              std::string::npos);
}

}  // namespace

int main()
{
    TheTownIsTheCity();
    TheStopClaimsLikeTheReset();
    TheHeadWalksTheFamilyTrainerByTrainer();
    ItNeverTakesTheRunsTurn();
    ItIsBounded();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_training_stop: all passed\n");
    return 0;
}
