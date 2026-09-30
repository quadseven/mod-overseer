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

#include <cmath>
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
using OverseerDecisions::TRAINING_STOP_RETRY_SECONDS;
using OverseerDecisions::TRAINING_STOP_SPENT_REST_SECONDS;
using OverseerDecisions::TrainingStopEnds;
using OverseerDecisions::TrainingStopHoldsRepairLeg;
using OverseerDecisions::TrainingStopMayPreempt;
using OverseerDecisions::TrainingStopRestStillApplies;
using OverseerDecisions::TrainingStopRestSeconds;
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
                          bool onHeadMap = true)
{
    TrainingStopMember member;
    member.name = name;
    member.learnSkill = skill;
    member.trainerYards = trainerYards;
    member.onHeadMap = onHeadMap;
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
    between.trainerYards = 850.f;  // Zayus from the auctioneer the head had walked to
    Check("a trainer across the city from the auctioneer is still the town",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
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

    // THE STOP OUTRANKS THE BRIDGE'S TOWN ERRANDS (2026-09-29). Measured on the
    // dev realm: the town phase between two Stockade runs is the auctioneer
    // errand's, the bridge writes a counter, a ground aim or a reagent vendor's
    // creature entry into the column every few minutes, and the stop that used
    // to wait for an empty column never had a turn. A family that cannot buff
    // is worse off than one that shops a few minutes later.
    TravelClaimFacts vendor(TravelOwner::TrainingStop);
    vendor.column = "vendor";
    vendor.target = "3363";
    Check("a counter errand in the column is written over by the stop",
          ReadTravelClaim(vendor) == TravelClaim::Preempt);
    for (char const* aim : {"auctioneer", "repair", "banker", "guild banker",
                            "at:1:1657.9,-4433.0,17.5", "11868", "flight master:64"})
    {
        TravelClaimFacts town(TravelOwner::TrainingStop);
        town.column = aim;
        town.target = "3363";
        Check((std::string("the bridge's town aim '") + aim + "' gives way to the stop").c_str(),
              ReadTravelClaim(town) == TravelClaim::Preempt);
    }
    TravelClaimFacts pending(TravelOwner::TrainingStop);
    pending.learnSkill = 186;
    pending.column = "vendor";
    pending.target = "3363";
    Check("a pending profession errand still fences the column",
          ReadTravelClaim(pending) == TravelClaim::RefusedProfession);
    TravelClaimFacts respecWalk(TravelOwner::TrainingStop);
    respecWalk.column = "class trainer";
    respecWalk.target = "3363";
    Check("a keyword that is no town errand is not preempted",
          ReadTravelClaim(respecWalk) != TravelClaim::Preempt);
    TravelClaimFacts otherOwner(TravelOwner::Respec);
    otherOwner.column = "vendor";
    otherOwner.target = "3363";
    Check("only the stop takes the column from a counter errand",
          ReadTravelClaim(otherOwner) == TravelClaim::RefusedForeign);
    Check("the preemptible aims are the town errands",
          TrainingStopMayPreempt("11868") && TrainingStopMayPreempt("vendor") &&
              TrainingStopMayPreempt("at:0:-8815.2,652.9,94.9") &&
              TrainingStopMayPreempt("flight master:64") && !TrainingStopMayPreempt("") &&
              !TrainingStopMayPreempt("profession trainer") && !TrainingStopMayPreempt("class trainer"));

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
    Check("a member on another map is not walked for",
          leg.step == TrainingStopStep::NobodyOnHeadMap);
    Check("and the open stop ends", TrainingStopEnds(leg.step, true));

    family[4].onHeadMap = true;
    leg = PickTrainingStopLeg(open, family);
    Check("once it is back on the head's map it is",
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

// THE FAMILY'S CLASS SPELLS (2026-09-29). Overseer.Train.Factory = 0 stopped
// the level-up grant of class spells, and the stop above walked only for a
// profession. Measured live on the dev realm in the Stockade: a level 35
// priest held no Power Word: Fortitude, a level 35 mage no Arcane
// Intellect, a level 37 paladin no blessing and a level 39 warrior no Battle
// Shout, so no party frame carried a buff. A class spell is a learn too.
TrainingStopMember ClassMember(char const* name, uint32_t affordable, float trainerYards,
                               bool onHeadMap = true)
{
    TrainingStopMember member = Member(name, 0, trainerYards, onHeadMap);
    member.classSpells = affordable;
    return member;
}

void AClassSpellIsALearnToo()
{
    std::vector<TrainingStopMember> family = {
        ClassMember("Grug", 4, 210.f),
        ClassMember("Ugga", 9, 180.f),
        ClassMember("Og", 0, 150.f),
    };
    TrainingStopLeg leg = PickTrainingStopLeg(HeldInTown(), family);
    Check("a family with no profession learn walks for a member's class spells",
          leg.step == TrainingStopStep::Walk && leg.member == 0);

    family[0].walkedThisStop = true;
    leg = PickTrainingStopLeg(HeldInTown(), family);
    Check("then for the next member that can afford one",
          leg.step == TrainingStopStep::Walk && leg.member == 1);

    family[1].walkedThisStop = true;
    leg = PickTrainingStopLeg(HeldInTown(), family);
    Check("a member with nothing to buy is not walked for",
          leg.step == TrainingStopStep::NothingToLearn);

    std::vector<TrainingStopMember> far = {ClassMember("Ugga", 9, 2375.f)};
    Check("a class trainer past the city is not walked to",
          PickTrainingStopLeg(HeldInTown(), far).step == TrainingStopStep::NoTrainerInTown);
    std::vector<TrainingStopMember> separated = {ClassMember("Ugga", 9, 1214.5f)};
    Check("a same-map family member's reachable Stormwind trainer can be walked to",
          PickTrainingStopLeg(HeldInTown(), separated).step == TrainingStopStep::Walk);
    std::vector<TrainingStopMember> away = {ClassMember("Ugga", 9, 180.f, false)};
    Check("nor for a member on another map",
          PickTrainingStopLeg(HeldInTown(), away).step == TrainingStopStep::NobodyOnHeadMap);
    Check("a member with neither a skill nor a spell wants nothing",
          !OverseerDecisions::TrainingStopWants(Member("Og", 0, 100.f)));
    Check("a skill is wanted", OverseerDecisions::TrainingStopWants(Member("Og", 186, 100.f)));
    Check("a class spell is wanted", OverseerDecisions::TrainingStopWants(ClassMember("Og", 1, 100.f)));
}

void ATrainingArrivalWaitKeepsLateMembersOwed()
{
    uint32_t const limit = OverseerDecisions::TRAINING_STOP_REACH_POLLS;
    Check("keep waiting before the arrival limit",
          !OverseerDecisions::TrainingStopArrivalMissed(true, limit - 1));
    Check("an away learner at the arrival limit leaves the visit unfinished",
          OverseerDecisions::TrainingStopArrivalMissed(true, limit));
    Check("a group with nobody away has no missed arrival",
          !OverseerDecisions::TrainingStopArrivalMissed(false, limit));
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

    TrainingStopFacts recovering = HeldInTown();
    recovering.head.bagBlocked = false;
    recovering.head.campaignBetweenAttempts = true;
    recovering.head.trainerYards = 148.f;
    Check("a nearby trainer is normally allowed between attempts",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, recovering.head));
    recovering.head.familyMemberInsideInstance = true;
    Check("a straggler still inside keeps travel with the run",
          !HeadErrandMayTravel(HeadErrand::TrainerTrip, recovering.head));
    Check("a recovering run with an instance straggler blocks training",
          PickTrainingStopLeg(recovering, Horde()).step == TrainingStopStep::RunOwnsTravel);

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
    taken.columnPreemptible = true;
    Check("unless the bridge's town errand holds it, and the stop takes it",
          PickTrainingStopLeg(taken, Horde()).step == TrainingStopStep::Walk);
    Check("without ending the stop", !TrainingStopEnds(TrainingStopStep::ColumnTaken, true));
    Check("a walk does not end it", !TrainingStopEnds(TrainingStopStep::Walk, true));
    Check("nothing ends a stop that is not open", !TrainingStopEnds(TrainingStopStep::SpentItsTime, false));

    Check("every step has a sentence",
          std::string(TrainingStopStepWord(TrainingStopStep::NoTrainerInTown)).find("1500") !=
              std::string::npos);
}

// A STOP THAT ENDED FOR A REASON THE WORLD WILL CHANGE DOES NOT REST HALF AN
// HOUR (2026-09-29). Measured on the dev realm: the Horde family's stop ended
// "no trainer within 700 yards of the head" because the head had walked to an
// auctioneer, with one member's leg cut short and its 12 class spells unbought,
// and the next stop was 30 minutes off, by which time the head was elsewhere
// again.
void ARestIsAsLongAsItsReason()
{
    Check("a stop that served everybody rests the full half hour",
          TrainingStopRestSeconds(TrainingStopStep::NothingToLearn) == TRAINING_STOP_REST_SECONDS);
    Check("a stop that spent its time rests ten minutes",
          TrainingStopRestSeconds(TrainingStopStep::SpentItsTime) == TRAINING_STOP_SPENT_REST_SECONDS);
    for (TrainingStopStep step : {TrainingStopStep::NoTrainerInTown, TrainingStopStep::NobodyOnHeadMap,
                                  TrainingStopStep::RunOwnsTravel})
        Check("a stop cut short by where the head and family stood tries again in minutes",
              TrainingStopRestSeconds(step) == TRAINING_STOP_RETRY_SECONDS);
    Check("the retry is shorter than the spent rest, which is shorter than the full one",
          TRAINING_STOP_RETRY_SECONDS < TRAINING_STOP_SPENT_REST_SECONDS &&
              TRAINING_STOP_SPENT_REST_SECONDS < TRAINING_STOP_REST_SECONDS);

    TrainingStopFacts retried = HeldInTown();
    retried.restSeconds = TRAINING_STOP_RETRY_SECONDS;
    retried.sinceLastStop = TRAINING_STOP_RETRY_SECONDS - 1;
    Check("the rest a stop asked for is the one it waits",
          PickTrainingStopLeg(retried, Horde()).step == TrainingStopStep::Resting);
    retried.sinceLastStop = TRAINING_STOP_RETRY_SECONDS;
    Check("and the stop may open when it is over",
          PickTrainingStopLeg(retried, Horde()).step == TrainingStopStep::Walk);
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

    Check("the stop reads whether the column holds a town errand it may take",
          source.find("facts.columnPreemptible =") != std::string::npos &&
              source.find("OverseerDecisions::TrainingStopMayPreempt(column)") != std::string::npos);
    Check("a book-owned aim (a respec walk, a run's leg) is never preempted",
          source.find("!_travelAims.ClaimedBy(headName, column, columnOwner)") != std::string::npos);
    Check("a stop that ends sets its own rest",
          source.find("stop.restSeconds = OverseerDecisions::TrainingStopRestSeconds(pick.step);") !=
              std::string::npos);
    Check("the stop's claim over a town errand is said",
          source.find("a training stop takes the travel column") != std::string::npos);

    // THE FAMILY'S CLASS SPELLS (2026-09-29).
    Check("a member with no profession learn is asked for its class trainer",
          source.find("ResolveTravelTarget(bot, \"class trainer\", trainer, where)") !=
              std::string::npos);
    std::size_t const teachBody = source.find("bool TeachAtTrainingStop(TrainingStopLegState& leg)");
    std::size_t const buys = source.find("BuyAffordableClassSpells(", teachBody);
    std::size_t const bodyEnd = source.find("// Put the tank strategies on a character", teachBody);
    Check("the leg's arrival buys the class spells its members can afford",
          teachBody != std::string::npos && buys != std::string::npos && buys < bodyEnd);
    Check("the expired away-member wait records an unfinished visit",
          teachBody != std::string::npos &&
              source.find("TrainingStopArrivalMissed(!away.empty(), leg.reachPolls)", teachBody) <
                  bodyEnd && source.find("leg.unlearned.insert(name);", teachBody) < bodyEnd);
    std::size_t const helper = source.find("static unsigned BuyAffordableClassSpells(");
    std::size_t const helperEnd = source.find("void DriveGuildTraining()", helper);
    Check("a purchase is counted from the spell book, not the purse",
          helper != std::string::npos && helperEnd != std::string::npos &&
              source.find("bot->HasSpell(spellId)", helper) < helperEnd);
    Check("the guild's trainer visit buys through the same helper",
          source.find("BuyAffordableClassSpells(", source.find("void DriveGuildTraining()")) <
              source.find("void DriveGuildWeaponFor("));
}

}  // namespace

// THE ALLIANCE FAMILY'S CLASS TRAINING (2026-09-29, "no buffs on Grug's family in
// the dungeons"). Grug's family (warrior 39, paladin 37, rogue 35, mage 35,
// priest 35) knew no class spell it could buy: its between-run town and its
// hearthstone were Ratchet, a neutral goblin town in the Barrens, and Ratchet has
// no Alliance class trainer. Theramore, the nearest, is 2,800 yards away and has
// no priest or rogue trainer. The Stockade is in Stormwind, the dungeon finder's
// exit walks the family out into it, and every class has a trainer there within
// 400 yards of the inn.
struct Spot
{
    char const* who;
    float x;
    float y;
};

float Yards(Spot const& a, float x, float y)
{
    float const dx = a.x - x;
    float const dy = a.y - y;
    return std::sqrt(dx * dx + dy * dy);
}

void TheStormwindTrainersAreInReach()
{
    // Innkeeper Allison of the Gilded Rose, creature 6740, world DB.
    constexpr float INN_X = -8867.8f;
    constexpr float INN_Y = 673.7f;
    // The Stockade's way out (areatrigger 503) lands the family here.
    constexpr float DOOR_X = -8764.83f;
    constexpr float DOOR_Y = 846.075f;
    // One real trainer per class, Stormwind, from creature/trainer rows.
    Spot const stormwind[] = {
        {"warrior Ander Germaine", -8705.4f, 329.6f},
        {"paladin Arthur the Faithful", -8574.0f, 860.9f},
        {"rogue Osborne the Night Man", -8752.3f, 377.6f},
        {"priest Brother Benjamin", -8547.7f, 814.7f},
        {"mage Jennea Cannon", -8990.0f, 862.9f},
    };
    for (Spot const& trainer : stormwind)
    {
        Check("every Stormwind class trainer is a town's walk from the inn the family binds at",
              Yards(trainer, INN_X, INN_Y) <= TRAINING_STOP_YARDS);
        Check("and from the door the finder's exit lands it at",
              Yards(trainer, DOOR_X, DOOR_Y) <= TRAINING_STOP_YARDS);
    }
    // Theramore's warrior trainer from the Ratchet inn: the stop cannot walk it.
    Spot const theramore{"warrior Captain Evencane", -3728.f, -4538.f};
    Check("Theramore is past a training stop's reach from Ratchet",
          Yards(theramore, -1050.f, -3665.f) > TRAINING_STOP_YARDS);

    // A priest with no trainer in reach is skipped, and the mage is walked for.
    TrainingStopMember priest = Member("Ugga", 0, -1.f);
    priest.classSpells = 4;
    TrainingStopMember mage = Member("Og", 0, 300.f);
    mage.classSpells = 5;
    TrainingStopFacts facts = HeldInTown();
    TrainingStopLeg const pick = PickTrainingStopLeg(facts, {priest, mage});
    Check("a class with no trainer in reach is skipped, not waited on",
          pick.step == TrainingStopStep::Walk && pick.member == 1);
    Check("and a family with none in reach ends its stop rather than starving",
          PickTrainingStopLeg(facts, {priest}).step == TrainingStopStep::NoTrainerInTown &&
              TrainingStopEnds(TrainingStopStep::NoTrainerInTown, true));
}

void ARestEarnedInRatchetIsNotServedInStormwind()
{
    Check("a stop that ended for want of a trainer stops resting once the head is on another map",
          !TrainingStopRestStillApplies(TrainingStopStep::NoTrainerInTown, 1, 0));
    Check("on the same map it still rests",
          TrainingStopRestStillApplies(TrainingStopStep::NoTrainerInTown, 1, 1));
    Check("a stop that served everybody rests wherever the head goes",
          TrainingStopRestStillApplies(TrainingStopStep::NothingToLearn, 1, 0));
    Check("and so does one that spent its time",
          TrainingStopRestStillApplies(TrainingStopStep::SpentItsTime, 1, 0));
}

void TheRepairLegWaitsForAnOpenStopAndNoLonger()
{
    Check("no open stop, no wait", !TrainingStopHoldsRepairLeg(false, 0));
    Check("an open stop holds the finished repair leg", TrainingStopHoldsRepairLeg(true, 0));
    Check("for the stop's own budget",
          TrainingStopHoldsRepairLeg(true, TRAINING_STOP_MAX_SECONDS - 1));
    Check("and not a second past it", !TrainingStopHoldsRepairLeg(true, TRAINING_STOP_MAX_SECONDS));
}

void TheStockadesCampaignLivesInStormwind()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        Check("src/mod_overseer.cpp is readable from the working directory", false);
        return;
    }
    Check("the Stockades row names the Gilded Rose as the campaign's town",
          source.find("{\"stockades\", 0, 101, 34, 503, 0.f, 0.f, 0.f, {}, -8867.8f, 673.7f, 98.0f},") !=
              std::string::npos);
    std::size_t const leg = source.find("void DriveRepairLeg(DungeonRunCoordinatorState& coord,");
    std::size_t const finished =
        source.find("case OverseerDecisions::RepairLegVerdict::Finished:", leg);
    std::size_t const hold = source.find("if (TrainingStopHoldsRepairLegFor(coord, leaderName, members))", leg);
    Check("the finished repair leg offers the training stop its turn before RESET",
          leg != std::string::npos && finished != std::string::npos && hold != std::string::npos &&
              hold > finished && hold < source.find("the repair leg for '{}'s party is done after", finished));
    Check("the offer runs the stop for that family only",
          source.find("void DriveTrainingStop(std::string const& onlyFamily = std::string())") !=
              std::string::npos &&
              source.find("DriveTrainingStop(family);") != std::string::npos);
    Check("the head's rest is read against the map its last stop ended on",
          source.find("OverseerDecisions::TrainingStopRestStillApplies(") != std::string::npos);
    Check("a class with no trainer in reach is said once and skipped",
          source.find("training stop skips '{}' (class {})") != std::string::npos);
    Check("same-map family members are included even when the party has spread out",
          source.find("member.onHeadMap = SteerableAI(bot) && bot->IsAlive() &&\n"
                      "                                   bot->GetMapId() == head->GetMapId();") !=
              std::string::npos);
}

int main()
{
    TheTownIsTheCity();
    TheStopClaimsLikeTheReset();
    TheHeadWalksTheFamilyTrainerByTrainer();
    AClassSpellIsALearnToo();
    ATrainingArrivalWaitKeepsLateMembersOwed();
    ItNeverTakesTheRunsTurn();
    ItIsBounded();
    ARestIsAsLongAsItsReason();
    TheAdapterIsWired();
    TheStormwindTrainersAreInReach();
    ARestEarnedInRatchetIsNotServedInStormwind();
    TheRepairLegWaitsForAnOpenStopAndNoLonger();
    TheStockadesCampaignLivesInStormwind();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_training_stop: all passed\n");
    return 0;
}
