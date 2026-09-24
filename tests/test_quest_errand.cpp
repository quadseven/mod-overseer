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
    Expect(ParseQuestErrand("abandon quest:7848").verb == QuestErrandVerb::None,
           "only take and turnin");
    Expect(ParseQuestErrand("take quest:12345678901").verb == QuestErrandVerb::None,
           "an id too long to be one");
    Expect(ParseQuestErrand("").verb == QuestErrandVerb::None, "empty");
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

int main()
{
    TheGrammar();
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
