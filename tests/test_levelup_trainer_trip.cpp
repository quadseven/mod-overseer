/*
 * A level-up sends the family to its class trainers (mod-overseer#965).
 *
 * The operator asked when the family goes to get its new spells: at a class
 * trainer, or by upgrading itself where it stands. TrainRoster used to grant
 * them in place through PlayerbotFactory::InitAvailableSpells; that grant is
 * gone (tests/test_roster_training.cpp) and the training stop walks the family
 * to a trainer and pays (tests/test_training_stop.cpp). This file pins the
 * four things the trip still lacked:
 *
 *   - a level gained since the last stop ended ends that stop's rest, so the
 *     trip follows the level-up and not the next half hour;
 *   - the trainer walked to is the nearest one that would teach the member
 *     something, so every spell InitAvailableSpells reached at any class
 *     trainer is reachable, not only the nearest trainer's list;
 *   - a questing head partway through an objective finishes it before the
 *     trip, for a bounded wait, and nothing in the trip drops the quest;
 *   - at the trainer, the quests it takes in are handed in and the class
 *     quests it gives are taken, so A Warrior's Training and the level-1
 *     letters become completable.
 *
 * The adapter parts read src/mod_overseer.cpp (run from the repo root).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ClassTrainerOption;
using OverseerDecisions::FamilyLevelledSince;
using OverseerDecisions::PickClassTrainer;
using OverseerDecisions::PickTrainingStopLeg;
using OverseerDecisions::QuestObjectiveCount;
using OverseerDecisions::QuestObjectiveInProgress;
using OverseerDecisions::TRAINING_STOP_REST_SECONDS;
using OverseerDecisions::TRAINING_TRIP_QUEST_HOLD_SECONDS;
using OverseerDecisions::TrainerVisitTakesQuest;
using OverseerDecisions::TrainingStopEnds;
using OverseerDecisions::TrainingStopFacts;
using OverseerDecisions::TrainingStopMember;
using OverseerDecisions::TrainingStopRestStillApplies;
using OverseerDecisions::TrainingStopStep;
using OverseerDecisions::TrainingStopStepWord;
using OverseerDecisions::TrainingTripWaitsForQuest;

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

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

// The text of one adapter function, from its signature to the next marker.
std::string Body(std::string const& source, char const* begin, char const* end)
{
    std::size_t const from = source.find(begin);
    if (from == std::string::npos)
        return "";
    std::size_t const to = source.find(end, from);
    if (to == std::string::npos)
        return "";
    return source.substr(from, to - from);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

// A questing family's warrior, level 10, with spells at a Stormwind trainer.
std::vector<TrainingStopMember> Warrior()
{
    TrainingStopMember member;
    member.name = "Warrior";
    member.classSpells = 3;
    member.onHeadMap = true;
    member.trainerYards = 900.f;
    return {member};
}

TrainingStopFacts Questing()
{
    TrainingStopFacts facts;
    facts.questing = true;
    facts.columnFree = true;
    return facts;
}

void ALevelEndsTheRest()
{
    std::map<std::string, uint32_t> const atEnd = {{"Warrior", 9}, {"Priest", 9}};
    Check("nobody levelled: the rest stands", !FamilyLevelledSince(atEnd, atEnd));
    Check("one member levelled: the rest is over",
          FamilyLevelledSince(atEnd, {{"Warrior", 10}, {"Priest", 9}}));
    Check("a member absent when the stop ended says nothing",
          !FamilyLevelledSince(atEnd, {{"Warrior", 9}, {"Priest", 9}, {"Hunter", 12}}));
    Check("a member absent now says nothing", !FamilyLevelledSince(atEnd, {{"Warrior", 9}}));
    Check("a level lost is not a level gained",
          !FamilyLevelledSince(atEnd, {{"Warrior", 8}, {"Priest", 9}}));
    Check("no stop has ended: nothing to compare", !FamilyLevelledSince({}, {{"Warrior", 10}}));

    for (TrainingStopStep step : {TrainingStopStep::NothingToLearn, TrainingStopStep::SpentItsTime,
                                  TrainingStopStep::LearnsOwed, TrainingStopStep::NoTrainerInTown})
        Check("a level gained since the stop ended lifts any rest",
              !TrainingStopRestStillApplies(step, 0, 0, true));
    Check("without one, a stop that served everybody still rests",
          TrainingStopRestStillApplies(TrainingStopStep::NothingToLearn, 0, 0, false));

    // The adapter clears the rest by the same rule as a change of map: the
    // pick then sees no last stop and walks.
    TrainingStopFacts lifted = Questing();
    lifted.sinceLastStop = UINT32_MAX;
    Check("a lifted rest walks on the next poll",
          PickTrainingStopLeg(lifted, Warrior()).step == TrainingStopStep::Walk);
    TrainingStopFacts resting = Questing();
    resting.sinceLastStop = TRAINING_STOP_REST_SECONDS / 2;
    Check("an unlifted one rests",
          PickTrainingStopLeg(resting, Warrior()).step == TrainingStopStep::Resting);

    std::string const source = ReadModule();
    std::string const drive =
        Body(source, "    void DriveTrainingStop(std::string const& onlyFamily",
             "    void NoteTrainingStopLeg(");
    Check("DriveTrainingStop is found in src/mod_overseer.cpp (run from the repo root)",
          !drive.empty());
    Check("a stop that ends records every member's level",
          Has(drive, "stop.endedLevels = FamilyLevels(rows);"));
    Check("the rest is read against the levels since",
          Has(drive, "OverseerDecisions::FamilyLevelledSince(stop.endedLevels, FamilyLevels(rows))"));
}

void TheTrainerThatTeaches()
{
    // A mage on the Stormwind map: the portal trainer stands nearer than the
    // mage trainer and has nothing this mage can buy.
    std::vector<ClassTrainerOption> const mage = {
        {2485, 300.f, 0},   // a portal trainer
        {331, 450.f, 6},    // a mage trainer
        {5144, 1200.f, 6},  // another mage trainer, farther
    };
    Check("the nearest trainer that would teach something",
          PickClassTrainer(mage) == 1);
    std::vector<ClassTrainerOption> const out = {
        {331, 450.f, 0},
        {2485, 300.f, 0},
    };
    Check("with nothing to buy anywhere, the nearest of all, so the skip line can measure it",
          PickClassTrainer(out) == 1);
    Check("no class trainer on the map", PickClassTrainer({}) == -1);
    Check("a tie is the first listed",
          PickClassTrainer({{1, 100.f, 2}, {2, 100.f, 4}}) == 0);

    std::string const source = ReadModule();
    std::string const drive =
        Body(source, "    void DriveTrainingStop(std::string const& onlyFamily",
             "    void NoteTrainingStopLeg(");
    Check("the stop asks for the trainer that teaches, not the nearest one",
          Has(drive, "ClassTrainerToVisit(bot, head, trainer, member.classSpells,") &&
              !Has(drive, "ResolveTravelTarget(bot, \"class trainer\", trainer, where)"));
    std::string const visit = Body(source, "    bool ClassTrainerToVisit(", "    static bool HeadObjectiveInProgress(");
    Check("ClassTrainerToVisit is found", !visit.empty());
    Check("it walks every class trainer of the member's class on the head's map",
          Has(visit, "_classTrainerSpots[bot->getClass()]") &&
              Has(visit, "spots[i].mapId != head->GetMapId()"));
    Check("it counts what each would teach with the arrival's own rule",
          Has(visit, "AffordableClassSpells(classTrainer, bot)"));
    Check("and lets the pure pick choose", Has(visit, "OverseerDecisions::PickClassTrainer(options)"));
}

// WHAT INITAVAILABLESPELLS GRANTED, AND WHAT THE TRIP BUYS. Pinned upstream
// (mod-playerbots PlayerbotFactory::InitAvailableSpells, at the build's
// MODULE_SHA): for every class trainer with IsTrainerValidForPlayer, every
// spell on its list that Trainer::CanTeachSpell allows, granted free. The trip
// reads the same two rules and buys through Trainer::TeachSpell, which takes
// the trainer's price: the same spells, bought with the member's own gold
// (natural only, the operator, 2026-10-12).
void TheSameSpells()
{
    std::string const source = ReadModule();
    std::string const affordable =
        Body(source, "    static unsigned AffordableClassSpells(", "    // WHY THE CORE REFUSED");
    Check("AffordableClassSpells is found", !affordable.empty());
    Check("a trainer's offer is every listed spell CanTeachSpell allows",
          Has(affordable, "for (Trainer::Spell const& spell : trainer->GetSpells())") &&
              Has(affordable, "if (!trainer->CanTeachSpell(bot, &spell))"));
    std::string const teach =
        Body(source, "    bool TeachAtTrainingStop(TrainingStopLegState& leg)",
             "    // Put the tank strategies on a character");
    Check("the arrival asks IsTrainerValidForPlayer, as the factory did",
          Has(teach, "if (!trainer->IsTrainerValidForPlayer(bot) || !AffordableClassSpells(trainer, bot))"));
    Check("and buys through the core's TeachSpell, never a learnSpell grant",
          Has(teach, "BuyAffordableClassSpells(trainer, npc, bot, &offered)") &&
              !Has(teach, "learnSpell("));
}

void AnObjectiveInProgressIsFinishedFirst()
{
    Check("no progress yet: not in progress",
          !QuestObjectiveInProgress({{0, 8}, {0, 6}}));
    Check("some boars killed: in progress", QuestObjectiveInProgress({{3, 8}}));
    Check("one objective done and another not begun: in progress",
          QuestObjectiveInProgress({{8, 8}, {0, 6}}));
    Check("every objective done: not in progress", !QuestObjectiveInProgress({{8, 8}, {6, 6}}));
    Check("an objective that needs nothing is not counted",
          !QuestObjectiveInProgress({{0, 0}, {0, 4}}));
    Check("a quest with no counted objective is not in progress", !QuestObjectiveInProgress({}));

    TrainingStopFacts mid = Questing();
    mid.questObjectiveOpen = true;
    Check("a questing head partway through an objective finishes it first",
          TrainingTripWaitsForQuest(mid) &&
              PickTrainingStopLeg(mid, Warrior()).step == TrainingStopStep::QuestObjectiveOpen);
    mid.questHeldSeconds = TRAINING_TRIP_QUEST_HOLD_SECONDS - 1;
    Check("for the hold's length", TrainingTripWaitsForQuest(mid));
    mid.questHeldSeconds = TRAINING_TRIP_QUEST_HOLD_SECONDS;
    Check("and not a second past it, so a quest that never finishes cannot keep the spells",
          !TrainingTripWaitsForQuest(mid) &&
              PickTrainingStopLeg(mid, Warrior()).step == TrainingStopStep::Walk);

    TrainingStopFacts open = Questing();
    open.questObjectiveOpen = true;
    open.stopSeconds = 60;
    Check("a stop already open goes on to its next leg",
          !TrainingTripWaitsForQuest(open) &&
              PickTrainingStopLeg(open, Warrior()).step == TrainingStopStep::Walk);
    TrainingStopFacts town;
    town.campaignArmed = true;
    town.head.bagBlocked = true;
    town.columnFree = true;
    town.questObjectiveOpen = true;
    Check("a campaign's family in town is not questing and does not wait",
          !TrainingTripWaitsForQuest(town));
    Check("the wait does not end a stop", !TrainingStopEnds(TrainingStopStep::QuestObjectiveOpen, false));
    Check("it has a sentence",
          Has(TrainingStopStepWord(TrainingStopStep::QuestObjectiveOpen), "objective"));

    std::string const source = ReadModule();
    std::string const drive =
        Body(source, "    void DriveTrainingStop(std::string const& onlyFamily",
             "    void NoteTrainingStopLeg(");
    Check("the stop reads the head's objective while questing",
          Has(drive, "facts.questObjectiveOpen = facts.questing && HeadObjectiveInProgress(head);"));
    Check("and counts the hold from its first poll",
          Has(drive, "stop.questHeldSince = now;") &&
              Has(drive, "facts.questHeldSeconds ="));
    std::string const progress =
        Body(source, "    static bool HeadObjectiveInProgress(", "    // The head has reached the trainer");
    Check("HeadObjectiveInProgress is found", !progress.empty());
    Check("it reads the quest the head is working from its rpg status",
          Has(progress, "RPG_DO_QUEST") && Has(progress, "std::get_if<NewRpgInfo::DoQuest>"));
    Check("and the objective counters and carried items",
          Has(progress, "GetQuestSlotCounter(slot, i)") &&
              Has(progress, "GetItemCount(quest->RequiredItemId[i], true)"));

    // NOTHING IN THE TRIP DROPS A QUEST. It is a travel errand: the quest drive
    // stands down while it holds the wheel and picks the quest back up after,
    // with the aim's backstop carried over the errand.
    std::string const stopCode =
        Body(source, "    // ------------------------------------- a training stop in town (#688) --",
             "    // Put the tank strategies on a character");
    Check("no quest is abandoned, removed or cleared by the stop",
          !Has(stopCode, "AbandonQuest(") && !Has(stopCode, "RemoveActiveQuest(") &&
              !Has(stopCode, "ClearAim("));
    Check("the quest drive's hand-back carries the aim's clock over the errand",
          Has(source, "state.since += handedBack - state.travelHeld;"));
}

void ItWalks()
{
    std::string const source = ReadModule();
    std::string const stopCode =
        Body(source, "    // ------------------------------------- a training stop in town (#688) --",
             "    // Put the tank strategies on a character");
    Check("the training stop's code is found", !stopCode.empty());
    Check("the trip is a claim on the head's travel column",
          Has(stopCode, "_travelAims.Claim(headName, OverseerDecisions::TrainingStopLegAim(entry),"));
    Check("and nothing in it teleports, summons or hearths",
          !Has(stopCode, "TeleportTo(") && !Has(stopCode, "SummonPlayer") &&
              !Has(stopCode, "HEARTHSTONE"));
}

void TheTrainersQuests()
{
    constexpr uint32_t WARRIOR = 1u << 0;
    constexpr uint32_t PALADIN = 1u << 1;
    constexpr uint32_t MAGE = 1u << 7;
    Check("A Warrior's Training is taken by a warrior", TrainerVisitTakesQuest(WARRIOR, WARRIOR));
    Check("a quest for warriors and paladins by either",
          TrainerVisitTakesQuest(WARRIOR | PALADIN, PALADIN));
    Check("not by a mage", !TrainerVisitTakesQuest(WARRIOR, MAGE));
    Check("and a quest for every class is not a class quest", !TrainerVisitTakesQuest(0, WARRIOR));

    std::string const source = ReadModule();
    std::string const teach =
        Body(source, "    bool TeachAtTrainingStop(TrainingStopLegState& leg)",
             "    // Put the tank strategies on a character");
    std::size_t const buys = teach.find("BuyAffordableClassSpells(trainer, npc, bot, &offered)");
    std::size_t const quests = teach.find("DoTrainerQuests(npc, bot);");
    Check("the visit does the trainer's quests after the spells",
          buys != std::string::npos && quests != std::string::npos && quests > buys);

    std::string const helper =
        Body(source, "    static void DoTrainerQuests(Creature* npc, Player* bot)", "    // Returns true when");
    Check("DoTrainerQuests is found", !helper.empty());
    Check("it reads the trainer's own quest relations",
          Has(helper, "sObjectMgr->GetCreatureQuestInvolvedRelationMap()") &&
              Has(helper, "sObjectMgr->GetCreatureQuestRelationMap()") &&
              Has(helper, "equal_range(npc->GetEntry())"));
    Check("hands in first, so a follow-up the trainer gives is offered",
          helper.find("HandInAtTrainer(") < helper.find("TakeClassQuestAtTrainer("));
    std::string const handIn =
        Body(source, "    static void HandInAtTrainer(", "    static void TakeClassQuestAtTrainer(");
    std::string const take =
        Body(source, "    static void TakeClassQuestAtTrainer(", "    static void DoTrainerQuests(");
    Check("a hand-in asks the quest errand's own refusal and reads the reward back",
          Has(handIn, "OverseerDecisions::QuestErrandRefusal(errand, facts)") &&
              Has(handIn, "RewardQuestChosen(bot, quest, reward, npc);") &&
              Has(handIn, "bot->GetQuestRewardStatus(questId)"));
    std::string const chosen =
        Body(source, "void RewardQuestChosen(", "// One recorded death, queued");
    Check("the chosen hand-in is the core's own reward, taking the chosen index",
          Has(chosen, "player->RewardQuest(quest, reward.index, giver);"));
    Check("a take is a class quest, asks the same refusal and reads the log back",
          Has(take, "OverseerDecisions::TrainerVisitTakesQuest(quest->GetRequiredClasses(), "
                    "bot->getClassMask())") &&
              Has(take, "OverseerDecisions::QuestErrandRefusal(errand, facts)") &&
              Has(take, "bot->AddQuestAndCheckCompletion(quest, npc);") &&
              Has(take, "bot->GetQuestStatus(questId) == QUEST_STATUS_NONE"));
}

}  // namespace

int main()
{
    ALevelEndsTheRest();
    TheTrainerThatTeaches();
    TheSameSpells();
    AnObjectiveInProgressIsFinishedFirst();
    ItWalks();
    TheTrainersQuests();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_levelup_trainer_trip: all passed\n");
    return 0;
}
