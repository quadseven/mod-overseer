/*
 * A class trainer teaches a whole visit's worth, and the stop that walked
 * there does not call a visit that taught nothing "nothing left"
 * (2026-09-29).
 *
 * Measured on the dev realm: the Alliance family stood in Stormwind with a
 * level 39 warrior offered 28 class spells. The head walked to the warrior
 * trainer and the leg ended "0 of 1 member(s) ... learned", the stop wrote
 * "nobody in the family has a learn left" and rested 30 minutes. The class
 * branch of the leg asked Player::GetNPCIfCanInteractWith, which needs the
 * member within INTERACTION_DISTANCE (5 yards), while the walk ends inside the
 * 12 yard arrival radius, so a member 6 to 12 yards off was dropped without a
 * line. Trainer::TeachSpell has no distance rule of its own.
 *
 * The trainer rows below are the acore_world trainer_spell rows for the
 * warrior trainer (TrainerId 1) and the priest trainer (TrainerId 11) with
 * the columns the core's Trainer::GetDefaultSpellState reads: MoneyCost (in
 * copper), ReqAbility1 (a spell the member must already hold) and ReqLevel.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; the last part
 * reads src/mod_overseer.cpp (run from the repo root).
 */

#include "overseer_decisions.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ClassLearnCandidate;
using OverseerDecisions::ClassLearnTier;
using OverseerDecisions::PlanClassLearns;
using OverseerDecisions::PickTrainingStopLeg;
using OverseerDecisions::TRAINING_STOP_LEARN_TRIES;
using OverseerDecisions::TRAINING_STOP_RETRY_SECONDS;
using OverseerDecisions::TRAINING_STOP_RUN_HOLD_SECONDS;
using OverseerDecisions::TrainingStopEnds;
using OverseerDecisions::TrainingStopFacts;
using OverseerDecisions::TrainingStopHoldsRunStart;
using OverseerDecisions::TrainingStopLegRetries;
using OverseerDecisions::TrainingStopLegAim;
using OverseerDecisions::TrainingStopLegStillWalking;
using OverseerDecisions::TrainingStopMember;
using OverseerDecisions::TrainingStopRestSeconds;
using OverseerDecisions::TrainingStopStep;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

struct Row
{
    uint32_t spellId;
    uint32_t cost;
    uint32_t reqAbility;
    uint32_t reqLevel;
};

// The core's Trainer::GetDefaultSpellState, for the columns a class trainer
// uses: not known, level reached, the required spell held.
bool Available(Row const& row, std::set<uint32_t> const& known, uint32_t level)
{
    return !known.count(row.spellId) && level >= row.reqLevel &&
           (!row.reqAbility || known.count(row.reqAbility));
}

// One read of the trainer's list at the door: what a single pass could buy.
std::vector<uint32_t> OnePass(std::vector<Row> const& rows, std::set<uint32_t> const& known,
                              uint32_t level, uint64_t money)
{
    std::vector<ClassLearnCandidate> candidates;
    for (Row const& row : rows)
        if (Available(row, known, level))
            candidates.push_back({row.spellId, row.reqLevel, row.cost, 1});
    return PlanClassLearns(candidates, money);
}

// A whole visit, the way BuyAffordableClassSpells makes it: read, buy, and read
// again until a pass buys nothing.
std::set<uint32_t> Visit(std::vector<Row> const& rows, uint32_t level, uint64_t money,
                         unsigned* passes = nullptr)
{
    // Heroic Strike (78) is a spell a warrior starts with; 284 needs it.
    std::set<uint32_t> known = {78};
    unsigned n = 0;
    for (uint32_t pass = 0; pass < OverseerDecisions::CLASS_LEARN_MAX_PASSES; ++pass)
    {
        std::vector<uint32_t> const plan = OnePass(rows, known, level, money);
        if (plan.empty())
            break;
        ++n;
        for (uint32_t id : plan)
        {
            for (Row const& row : rows)
                if (row.spellId == id)
                    money -= row.cost;
            known.insert(id);
        }
    }
    if (passes)
        *passes = n;
    return known;
}

// acore_world.trainer_spell, TrainerId 1 (warrior), ReqLevel <= 24.
std::vector<Row> const WARRIOR = {
    {6673, 10, 0, 1},     {100, 100, 0, 4},     {772, 100, 0, 4},     {3127, 100, 0, 6},
    {6343, 100, 0, 6},    {34428, 100, 0, 6},   {284, 200, 78, 8},    {1715, 200, 0, 8},
    {2687, 600, 0, 10},   {6546, 600, 772, 10}, {72, 1000, 0, 12},    {5242, 1000, 6673, 12},
    {7384, 1000, 0, 12},  {1160, 1500, 0, 14},  {6572, 1500, 0, 14},  {285, 2000, 284, 16},
    {694, 2000, 0, 16},   {2565, 2000, 0, 16},  {676, 3000, 0, 18},   {8198, 3000, 6343, 18},
    {6192, 6000, 5242, 22}, {1608, 8000, 285, 24},
};

// TrainerId 11 (priest), the spells the family needs: Fortitude 1243, Renew
// 139 (rank 1), Flash Heal's line starts at 2061, Power Word: Shield 17.
std::vector<Row> const PRIEST = {
    {1243, 10, 0, 1},  {589, 100, 0, 4},  {17, 100, 0, 6},    {591, 100, 585, 6},
    {139, 200, 0, 8},  {586, 200, 0, 8},  {1244, 800, 1243, 12}, {592, 800, 17, 12},
    {2061, 3000, 0, 20},
};

void ARankIsBoughtAfterTheRankItNeeds()
{
    std::set<uint32_t> none = {78};
    std::vector<uint32_t> const door = OnePass(WARRIOR, none, 24, 100000);
    Check("at the door Battle Shout rank 2 (5242) is not yet offered",
          std::find(door.begin(), door.end(), 5242u) == door.end());
    Check("nor Rend rank 2 (6546), which needs Rend (772)",
          std::find(door.begin(), door.end(), 6546u) == door.end());
    unsigned passes = 0;
    std::set<uint32_t> const bought = Visit(WARRIOR, 24, 100000, &passes);
    Check("one visit buys Battle Shout rank 2 once rank 1 is known", bought.count(5242) != 0);
    Check("and Rend rank 2", bought.count(6546) != 0);
    Check("and the rank after it (6192 needs 5242)", bought.count(6192) != 0);
    Check("the visit took more than one pass", passes >= 3);
    Check("and bought every spell the level and purse allow", bought.size() == WARRIOR.size() + 1);
}

void ALimitedPurseBuysBuffsAndHealsFirst()
{
    Check("Power Word: Fortitude is a first-tier spell", ClassLearnTier("Power Word: Fortitude") == 0);
    Check("Battle Shout is", ClassLearnTier("Battle Shout") == 0);
    Check("Renew is", ClassLearnTier("Renew") == 0);
    Check("Flash Heal is", ClassLearnTier("Flash Heal") == 0);
    Check("Blessing of Might is", ClassLearnTier("Blessing of Might") == 0);
    Check("Arcane Intellect is", ClassLearnTier("Arcane Intellect") == 0);
    Check("Conjure Water is", ClassLearnTier("Conjure Water") == 0);
    Check("Healing Touch is not this list's Heal", ClassLearnTier("Healing Touch") == 1);
    Check("Smite is not", ClassLearnTier("Smite") == 1);
    Check("a null name is not", ClassLearnTier(nullptr) == 1);

    // 900 copper cannot buy a level 12 rank and the level 4 attack together:
    // the buff (tier 0) goes first even though the attack is required earlier.
    std::vector<ClassLearnCandidate> const c = {
        {589, 4, 100, 1}, {1243, 1, 10, 0}, {2052, 4, 100, 1}, {1244, 12, 800, 0}};
    std::vector<uint32_t> const plan = PlanClassLearns(c, 810);
    Check("the buffs are bought first", plan.size() >= 2 && plan[0] == 1243 && plan[1] == 1244);
    Check("and what the purse covers behind them",
          plan.size() == 2);
    Check("a spell the purse cannot cover is skipped and a cheaper one behind it buys",
          PlanClassLearns({{1, 1, 500, 1}, {2, 2, 50, 1}}, 100) == std::vector<uint32_t>{2});
    Check("priest with 900 copper: Fortitude first", Visit(PRIEST, 12, 900).count(1243) != 0);
}

void ALegThatTaughtNothingIsNotNothingLeft()
{
    Check("offered spells and none learned is retried", TrainingStopLegRetries(28, 0, 0));
    Check("for the tries a stop gives it",
          TrainingStopLegRetries(28, 0, TRAINING_STOP_LEARN_TRIES - 1));
    Check("and no more", !TrainingStopLegRetries(28, 0, TRAINING_STOP_LEARN_TRIES));
    Check("a leg that learned something is settled", !TrainingStopLegRetries(28, 3, 0));
    Check("a leg that was offered nothing is not a failure", !TrainingStopLegRetries(0, 0, 0));

    Check("the leg aim is the trainer: column text", TrainingStopLegAim(7311) == "trainer:7311");
    Check("a column holding the leg's own aim is a leg still walking",
          TrainingStopLegStillWalking("trainer:7311", 7311));
    Check("a bare entry is not the aim the leg wrote", !TrainingStopLegStillWalking("7311", 7311));
    Check("another trainer's aim is not this leg", !TrainingStopLegStillWalking("trainer:7312", 7311));
    Check("a cleared column is a leg ended", !TrainingStopLegStillWalking("", 7311));

    TrainingStopFacts facts;
    facts.campaignArmed = true;
    facts.head.trainerYards = 0.f;
    facts.columnFree = true;
    facts.sinceLastStop = UINT32_MAX;
    facts.stopSeconds = 180;
    std::vector<TrainingStopMember> none;
    Check("no learn and none owed is the ordinary nothing left",
          PickTrainingStopLeg(facts, none).step == TrainingStopStep::NothingToLearn);
    facts.learnsOwed = true;
    TrainingStopStep const owed = PickTrainingStopLeg(facts, none).step;
    Check("a visit that taught nothing reads learns owed", owed == TrainingStopStep::LearnsOwed);
    Check("which ends an open stop", TrainingStopEnds(owed, true));
    Check("and rests minutes, not the half hour",
          TrainingStopRestSeconds(owed) == TRAINING_STOP_RETRY_SECONDS);
    Check("while nothing left still rests the long time",
          TrainingStopRestSeconds(TrainingStopStep::NothingToLearn) >
              TRAINING_STOP_RETRY_SECONDS);
}

void ARunWaitsForTheStopAndNoLonger()
{
    Check("an open stop holds a run that has not started", TrainingStopHoldsRunStart(true, false, 0));
    Check("so do class learns outstanding with a trainer in reach",
          TrainingStopHoldsRunStart(false, true, 0));
    Check("nothing to learn holds nothing", !TrainingStopHoldsRunStart(false, false, 0));
    Check("the hold is 20 minutes", TRAINING_STOP_RUN_HOLD_SECONDS == 20 * 60);
    Check("held for the whole of it",
          TrainingStopHoldsRunStart(true, true, TRAINING_STOP_RUN_HOLD_SECONDS - 1));
    Check("and not a second past", !TrainingStopHoldsRunStart(true, true, TRAINING_STOP_RUN_HOLD_SECONDS));
}

std::string ReadModule()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        Check("src/mod_overseer.cpp is readable from the working directory", false);
        return;
    }
    std::size_t const leg = source.find("bool TeachAtTrainingStop(TrainingStopLegState& leg)");
    std::size_t const end = source.find("Put the tank strategies on a character", leg);
    std::string const body = source.substr(leg, end - leg);
    Check("the class branch of the leg does not ask the 5 yard interact gate",
          body.find("GetNPCIfCanInteractWith") == std::string::npos);
    Check("a member the trainer cannot be used by is said",
          body.find("cannot use creature") != std::string::npos);
    Check("a member offered spells who learned none is recorded for a retry",
          body.find("leg.unlearned.insert(name)") != std::string::npos);
    Check("a finished leg is noted against the stop",
          source.find("NoteTrainingStopLeg(name, stopLeg->second)") != std::string::npos);
    Check("the purchase loops in passes",
          source.find("pass < OverseerDecisions::CLASS_LEARN_MAX_PASSES") != std::string::npos);
    Check("the core's fail reason is said per spell",
          source.find("FailReason::NotEnoughMoney") != std::string::npos &&
              source.find("FailReason::NotEnoughSkill") != std::string::npos &&
              source.find("FailReason::Unavailable") != std::string::npos);
    std::size_t const run = source.find("if (TrainingStopHoldsRunStartFor(coord, leaderName, members))");
    std::size_t const portal = source.find("DungeonPortal const* portal = FindDungeonPortal(dungeonKeyword);", run);
    Check("a run that has not started waits for the stop before it looks for its door",
          run != std::string::npos && portal != std::string::npos && run < portal);
}

}  // namespace

int main()
{
    ARankIsBoughtAfterTheRankItNeeds();
    ALimitedPurseBuysBuffsAndHealsFirst();
    ALegThatTaughtNothingIsNotNothingLeft();
    ARunWaitsForTheStopAndNoLonger();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_class_learn: all passed\n");
    return 0;
}
