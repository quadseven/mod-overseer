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
    Expect(module.find("player->AddQuestAndCheckCompletion(quest, npc);") != std::string::npos &&
               module.find("player->RewardQuest(quest, 0, npc);") != std::string::npos,
           "the core's own calls take and hand in");
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
    TheAdapterSeams();
    if (failures)
    {
        std::fprintf(stderr, "%d quest errand check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_quest_errand: ok\n");
    return 0;
}
