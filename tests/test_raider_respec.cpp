/*
 * A guild raider's talent reset at a class trainer (mod-overseer#692).
 *
 * Both guilds were four healers short of a Molten Core lineup on the dev realm
 * on 2026-09-24. A Shadow priest there holds 51 points in Shadow (tree 2) and
 * about 1,100 gold; its healing tree is Holy (tree 1). The walk reuses the
 * roster reset's JudgeRespec and RespecTook, and this file pins the grammar,
 * the refusals before the walk and the verdict after it.
 *
 * The last part reads src/mod_overseer.cpp (run from the repo root) to pin the
 * shared reset door and the premade spend.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::ErrandWalkRefusalRetryable;
using OverseerDecisions::JudgeTalentVisit;
using OverseerDecisions::ParseTrainerWalkRequest;
using OverseerDecisions::RespecFacts;
using OverseerDecisions::TalentWalkRefusal;
using OverseerDecisions::TrainerVisitOutcome;
using OverseerDecisions::TrainerWalkRequest;

namespace E = OverseerDecisions::ErrandWalkRefusal;

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

bool Same(char const* a, char const* b)
{
    return std::string(a) == b;
}

void TheGrammar()
{
    TrainerWalkRequest holy = ParseTrainerWalkRequest("walk-to-trainer talents:1 max:20000");
    Check("talents:1 parses", Same(holy.error, ""));
    Check("to tree 1", holy.talentTab == 1);
    Check("with its cap", holy.maxYards == 20000.f);
    Check("and no trade", holy.skill == 0 && holy.learn.empty());

    Check("tree 0 parses", ParseTrainerWalkRequest("walk-to-trainer talents:0").talentTab == 0);
    Check("tree 2 parses", ParseTrainerWalkRequest("walk-to-trainer talents:2").talentTab == 2);
    Check("a trade walk has no tree",
          ParseTrainerWalkRequest("walk-to-trainer skill:197").talentTab == -1);

    char const* const bad[] = {
        "walk-to-trainer talents:3",
        "walk-to-trainer talents:-1",
        "walk-to-trainer talents:12",
        "walk-to-trainer talents:",
        "walk-to-trainer talents:1 talents:2",
        "walk-to-trainer talents:1 skill:197",
        "walk-to-trainer talents:1 learn:3908",
        "walk-to-trainer",
        "walk-to-trainer max:600",
    };
    for (char const* row : bad)
    {
        TrainerWalkRequest const request = ParseTrainerWalkRequest(row);
        if (!Same(request.error, E::MalformedTrainer))
        {
            std::printf("FAIL malformed: %s\n", row);
            ++failures;
        }
        Check("a malformed row carries no tree", request.talentTab == -1);
    }
}

RespecFacts ShadowPriest()
{
    RespecFacts facts;
    facts.specTab = 1;           // Holy
    facts.level = 60;
    facts.pointsByTree[2] = 51;  // Shadow
    facts.money = 11046421;      // copper
    facts.cost = 10000;          // the first reset
    return facts;
}

void TheRefusalsBeforeTheWalk()
{
    Check("a level 60 Shadow priest with the price walks", Same(TalentWalkRefusal(ShadowPriest()), ""));

    RespecFacts low = ShadowPriest();
    low.level = 9;
    Check("below level 10 is refused", Same(TalentWalkRefusal(low), E::TalentsTooLow));

    RespecFacts holy = ShadowPriest();
    holy.pointsByTree[2] = 0;
    holy.pointsByTree[1] = 51;
    Check("a priest already in Holy is refused", Same(TalentWalkRefusal(holy), E::TalentsInTree));

    RespecFacts poor = ShadowPriest();
    poor.money = 9999;
    Check("a purse short of the price is refused", Same(TalentWalkRefusal(poor), E::TalentsCannotAfford));
    poor.costWaived = true;
    Check("unless the realm waives the price", Same(TalentWalkRefusal(poor), ""));

    Check("the purse is worth asking again", ErrandWalkRefusalRetryable(E::TalentsCannotAfford));
    Check("the tree is not", !ErrandWalkRefusalRetryable(E::TalentsInTree));
    Check("nor the level", !ErrandWalkRefusalRetryable(E::TalentsTooLow));
    Check("nor a map with no class trainer", !ErrandWalkRefusalRetryable(E::NoClassTrainerOnMap));
    Check("a refused reset at the trainer is", ErrandWalkRefusalRetryable(E::TaughtNothing));
}

void TheVerdictAfterTheVisit()
{
    uint32_t holy[3] = {0, 51, 0};
    Check("reset and spent in Holy: learned",
          JudgeTalentVisit(true, holy, 1) == TrainerVisitOutcome::Learned);
    uint32_t mixed[3] = {5, 46, 0};
    Check("a point left outside the tree: not learned",
          JudgeTalentVisit(true, mixed, 1) == TrainerVisitOutcome::TaughtNothing);
    uint32_t shadow[3] = {0, 0, 51};
    Check("no reset: not learned", JudgeTalentVisit(false, shadow, 1) == TrainerVisitOutcome::TaughtNothing);
    Check("even if the points read right",
          JudgeTalentVisit(false, holy, 1) == TrainerVisitOutcome::TaughtNothing);
    uint32_t unspent[3] = {0, 0, 0};
    Check("reset with nothing spent: not learned",
          JudgeTalentVisit(true, unspent, 1) == TrainerVisitOutcome::TaughtNothing);
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
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    std::size_t n = 0;
    for (std::size_t at = source.find("AskForTalentWipe(bot, npc);"); at != std::string::npos;
         at = source.find("AskForTalentWipe(bot, npc);", at + 1))
        ++n;
    Check("the roster reset and the raider reset share one door", n == 2);
    has("the tree's premade build spends the points",
        "PlayerbotFactory::InitTalentsBySpecNo(bot, static_cast<int>(ev.specIndex), false);");
    has("the premade is the one the config maps the tree to",
        "sPlayerbotAIConfig.randomClassSpecIndex[bot->getClass()][tree]");
    has("the bot plays its new tree", "botAI->ResetStrategies(false);");
    has("only a trainer of the walker's class serves a talents walk",
        "if (ev.talentTab >= 0)\n            return TrainerServesClassOf(entry, who);");
    has("the walk is judged by the roster's own rules", "D::TalentWalkRefusal(facts); *wall");
}

}  // namespace

int main()
{
    TheGrammar();
    TheRefusalsBeforeTheWalk();
    TheVerdictAfterTheVisit();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_raider_respec: all passed\n");
    return 0;
}
