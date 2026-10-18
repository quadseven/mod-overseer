/*
 * kind='quest': take a quest from its giver or hand it in to its taker. The
 * grammar, the refusals in the order a person would check them, and the seams
 * the adapter needs, pinned in the source.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
}

void TheGrammar()
{
    QuestErrand const take = ParseQuestErrand("take quest:7848");
    Expect(take.verb == QuestErrandVerb::Take && take.questId == 7848, "take quest:7848");
    QuestErrand const turnin = ParseQuestErrand("turnin quest:7487");
    Expect(turnin.verb == QuestErrandVerb::TurnIn && turnin.questId == 7487,
           "turnin quest:7487");
    Expect(ParseQuestErrand("take 7848").verb == QuestErrandVerb::None, "no quest: prefix");
    Expect(ParseQuestErrand("take quest:").verb == QuestErrandVerb::None, "no id");
    Expect(ParseQuestErrand("take quest:78a").verb == QuestErrandVerb::None, "not a number");
    Expect(ParseQuestErrand("take quest:0").verb == QuestErrandVerb::None, "zero is no quest");
    QuestErrand const abandon = ParseQuestErrand("abandon quest:12619");
    Expect(abandon.verb == QuestErrandVerb::Abandon && abandon.questId == 12619,
           "abandon quest:12619");
    Expect(ParseQuestErrand("abandon 12619").verb == QuestErrandVerb::None, "abandon needs quest:");
    Expect(ParseQuestErrand("forget quest:7848").verb == QuestErrandVerb::None,
           "only take, turnin and abandon");
    Expect(ParseQuestErrand("take quest:12345678901").verb == QuestErrandVerb::None,
           "an id too long to be one");
    Expect(ParseQuestErrand("").verb == QuestErrandVerb::None, "empty");
}

void TheAbandon()
{
    QuestErrand const abandon{QuestErrandVerb::Abandon, 12619};
    QuestErrandFacts f;
    f.questKnown = true;
    f.status = 3;  // incomplete
    Expect(QuestErrandRefusal(abandon, f).empty(), "an incomplete quest may be abandoned, no giver needed");
    f.status = 5;  // failed
    Expect(QuestErrandRefusal(abandon, f).empty(), "a failed quest may be abandoned");
    f.status = 0;
    Expect(QuestErrandRefusal(abandon, f) == "not in the quest log", "not in the log");
    f.status = 1;
    Expect(QuestErrandRefusal(abandon, f) == "it is complete; hand it in", "complete is handed in");
    f.status = 3;
    f.rewarded = true;
    Expect(QuestErrandRefusal(abandon, f) == "already turned in", "a rewarded quest is kept");
    f.rewarded = false;
    f.questKnown = false;
    Expect(QuestErrandRefusal(abandon, f) == "no such quest", "an unknown quest");
}

QuestErrandFacts Ready()
{
    QuestErrandFacts f;
    f.questKnown = true;
    f.giverInReach = true;
    f.eligible = true;
    f.logHasRoom = true;
    f.rewardable = true;
    return f;
}

void TakingAtLothos()
{
    QuestErrand const take = ParseQuestErrand("take quest:7848");
    Expect(QuestErrandRefusal(take, Ready()).empty(), "a member in reach, eligible, takes it");

    QuestErrandFacts f = Ready();
    f.giverInReach = false;
    Expect(QuestErrandRefusal(take, f) == "no giver of that quest in reach",
           "nobody in reach gives it");
    f = Ready();
    f.status = 3;
    Expect(QuestErrandRefusal(take, f) == "already in the quest log", "held already");
    f = Ready();
    f.rewarded = true;
    Expect(QuestErrandRefusal(take, f) == "already turned in", "attuned already");
    f = Ready();
    f.logHasRoom = false;
    Expect(QuestErrandRefusal(take, f) == "the quest log is full", "a full log says so");
    f = Ready();
    f.eligible = false;
    Expect(QuestErrandRefusal(take, f).find("not eligible") == 0,
           "below level 55, or the other faction's row");
    f = Ready();
    f.questKnown = false;
    Expect(QuestErrandRefusal(take, f) == "no such quest", "an unknown quest");
    Expect(QuestErrandRefusal(QuestErrand{}, Ready()).find("malformed") == 0,
           "a malformed row is refused first");
}

void HandingInTheFragment()
{
    QuestErrand const turnin = ParseQuestErrand("turnin quest:7848");
    QuestErrandFacts f = Ready();
    f.status = 1;
    Expect(QuestErrandRefusal(turnin, f).empty(), "a complete quest in reach is handed in");
    f.status = 3;
    Expect(QuestErrandRefusal(turnin, f) == "not complete", "no fragment yet");
    f.status = 1;
    f.rewardChoice = true;
    Expect(QuestErrandRefusal(turnin, f).find("choice of reward") != std::string::npos,
           "a choice of reward is not made for anybody");
    f.rewardChoice = false;
    f.rewardable = false;
    Expect(QuestErrandRefusal(turnin, f) == "the core will not reward it now",
           "the core's own refusal stands");
    f = Ready();
    f.status = 1;
    f.giverInReach = false;
    Expect(QuestErrandRefusal(turnin, f) == "no taker of that quest in reach",
           "nobody in reach takes it back");
}

// #19: QUEST 45, DISCOVER ROLF'S FATE, IS HANDED IN AT AN OBJECT. It starts at
// gameobject 55 (A half-eaten body) and ends at gameobject 56 (Rolf's corpse),
// with no creature_questender row, and used to be refused in exactly the words
// a character too far from a real creature gets. It is now handed in at the
// object when one is in reach, said to be an object when none is, and a quest
// the world names nothing at all for is a stop, never a wait.
void AQuestHandedInAtAnObject()
{
    QuestErrand const turnin = ParseQuestErrand("turnin quest:45");
    QuestErrandFacts f = Ready();
    f.status = 1;
    f.holder = QuestRelationHolder::GameObject;
    Expect(QuestErrandRefusal(turnin, f).empty(),
           "a complete quest whose ender is an object in reach is handed in");
    f.giverInReach = false;
    std::string const far = QuestErrandRefusal(turnin, f);
    Expect(far == "no object that takes that quest in reach",
           "an object ender out of reach says object, not creature");
    Expect(far != QuestErrandRefusal(turnin, [] {
               QuestErrandFacts c = Ready();
               c.status = 1;
               c.giverInReach = false;
               return c;
           }()),
           "and is not worded like a creature out of reach");
    Expect(QuestErrandRefusalRetryable(far), "an object out of reach is a wait");

    QuestErrand const take = ParseQuestErrand("take quest:45");
    QuestErrandFacts t = Ready();
    t.holder = QuestRelationHolder::GameObject;
    Expect(QuestErrandRefusal(take, t).empty(), "a quest an object gives is taken there");
    t.giverInReach = false;
    Expect(QuestErrandRefusal(take, t) == "no object that gives that quest in reach",
           "an object giver out of reach says object");
}

void AQuestNothingTakesIn()
{
    QuestErrand const turnin = ParseQuestErrand("turnin quest:45");
    QuestErrandFacts f = Ready();
    f.status = 1;
    f.holder = QuestRelationHolder::None;
    std::string const none = QuestErrandRefusal(turnin, f);
    Expect(none == "the world names no creature and no object that takes that quest in",
           "a quest nothing takes in is said to be one");
    Expect(!QuestErrandRefusalRetryable(none),
           "and is could-not, not did-not-yet: the sender must not re-ask");
    f.giverInReach = false;
    Expect(QuestErrandRefusal(turnin, f) == none, "reach does not change it");

    QuestErrand const take = ParseQuestErrand("take quest:45");
    QuestErrandFacts t = Ready();
    t.holder = QuestRelationHolder::None;
    Expect(!QuestErrandRefusalRetryable(QuestErrandRefusal(take, t)),
           "a quest nothing gives is a stop too");
    Expect(QuestErrandRefusal(take, t).find("gives") != std::string::npos,
           "and says gives, not takes in");

    Expect(std::string(QuestRelationHolderName(QuestRelationHolder::Creature)) == "creature" &&
               std::string(QuestRelationHolderName(QuestRelationHolder::GameObject)) ==
                   "gameobject" &&
               std::string(QuestRelationHolderName(QuestRelationHolder::None)) == "none",
           "the holder names the row carries");
}

void WhichRefusalsAreAWait()
{
    Expect(QuestErrandRefusalRetryable("no taker of that quest in reach") &&
               QuestErrandRefusalRetryable("no giver of that quest in reach") &&
               QuestErrandRefusalRetryable("not complete") &&
               QuestErrandRefusalRetryable("the quest log is full") &&
               QuestErrandRefusalRetryable("the core will not reward it now"),
           "the walls that move are a wait");
    Expect(!QuestErrandRefusalRetryable("no such quest") &&
               !QuestErrandRefusalRetryable("already turned in") &&
               !QuestErrandRefusalRetryable("malformed request: want take, turnin or abandon quest:<id>") &&
               !QuestErrandRefusalRetryable(
                   "it offers a choice of reward, and nobody here would choose"),
           "the walls the same row hits for ever are a stop");
    // Every refusal the decision can return for a turn-in at a creature that
    // is merely too far is still the same string as before #19, so a sender
    // reading the old words keeps working.
    QuestErrandFacts f = Ready();
    f.status = 1;
    f.giverInReach = false;
    Expect(QuestErrandRefusal(ParseQuestErrand("turnin quest:7848"), f) ==
               "no taker of that quest in reach",
           "a creature ender out of reach keeps its words");
}

void TheAdapterSeams()
{
    std::string const module = Read("src/mod_overseer.cpp");
    Expect(module.find("else if (kind == \"quest\")\n                detail = DoQuest(") !=
               std::string::npos,
           "kind='quest' is dispatched");
    Expect(module.find("kind != \"quest\"") != std::string::npos,
           "a quest row never shares a chat trigger");
    Expect(module.find("GetCreatureQuestRelationMap()") != std::string::npos &&
               module.find("GetCreatureQuestInvolvedRelationMap()") != std::string::npos,
           "the giver and taker come from the world's own quest relations");
    Expect(module.find("player->AddQuestAndCheckCompletion(quest, giver);") != std::string::npos &&
               module.find("player->RewardQuest(quest, 0, giver);") != std::string::npos,
           "the core's own calls take and hand in");
    Expect(module.find("GetGOQuestRelationMap()") != std::string::npos &&
               module.find("GetGOQuestInvolvedRelationMap()") != std::string::npos,
           "an object giver and taker come from the world's own relations too (#19)");
    std::size_t const doQuest = module.find("static char const* DoQuest(");
    Expect(doQuest != std::string::npos &&
               module.find("QuestObjectInReach(player, errand.questId, take)", doQuest) !=
                   std::string::npos &&
               module.find("QuestHolderKind(errand.questId, take)", doQuest) != std::string::npos,
           "the errand looks for an object when no creature is in reach (#19)");
    Expect(module.find("<< \",\\\"holder\\\":\"\n", doQuest) != std::string::npos &&
               module.find("QuestErrandRefusalRetryable(reason)", doQuest) != std::string::npos,
           "the row says which kind holds the quest and whether to ask again (#19)");
    Expect(module.find("if (player->GetQuestStatus(errand.questId) == QUEST_STATUS_NONE)") !=
               std::string::npos &&
               module.find("if (!player->GetQuestRewardStatus(errand.questId))") !=
                   std::string::npos,
           "the log is read back before 'delivered'");

    std::string const sql = Read("data/sql/characters/base/2026_09_24_04_overseer_quest.sql");
    Expect(sql.find("'guild','quest')") != std::string::npos,
           "the kind enum adds quest after every value before it");
}

}  // namespace

// THE TAKE'S REACH IS THE WALK'S ARRIVAL RADIUS (2026-10-08). The spawn walk that
// brings a character to a quest giver counts it arrived within
// SPAWN_WALK_ARRIVE_YARDS; a take that asks for less refuses a character the
// walk delivered ("no giver of that quest in reach"). Pinned in the source
// because the executor reads a live character.
void TheTakeReachIsTheWalksArrivalRadius()
{
    std::string const src = Read("src/mod_overseer.cpp");
    std::size_t const fn = src.find("static Creature* QuestCreatureInReach(");
    Expect(fn != std::string::npos, "the giver finder exists");
    std::size_t const body = src.find("static char const* DoQuest(", fn);
    std::size_t const reach = src.find("OverseerDecisions::SPAWN_WALK_ARRIVE_YARDS", fn);
    Expect(reach != std::string::npos && reach < body,
           "the giver is looked for within the spawn walk's arrival radius");
    std::size_t const old = src.find("FindNearestCreature(entry, TRAVEL_ARRIVED_YARDS)", fn);
    Expect(old == std::string::npos || old > body,
           "and not within the travel errand's smaller one");
}

int main()
{
    TheGrammar();
    TheAbandon();
    TheTakeReachIsTheWalksArrivalRadius();
    
    TakingAtLothos();
    HandingInTheFragment();
    AQuestHandedInAtAnObject();
    AQuestNothingTakesIn();
    WhichRefusalsAreAWait();
    TheAdapterSeams();
    if (failures)
    {
        std::fprintf(stderr, "%d quest errand check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_quest_errand: ok\n");
    return 0;
}
