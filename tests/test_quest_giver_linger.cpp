/*
 * A spawn walk that ends at a quest giver or taker lingers there for the quest
 * row (mod-overseer#1392), and the far-giver diagnostic finds a living giver.
 *
 * On the dev realm, 84 of 90 "no giver of that quest in reach" refusals between
 * 2026-11-05 and 2026-11-07 came within 10 seconds of a spawn walk's `reached`
 * line for the same member: the walk let the member go, its own movement took
 * it out of the take's 20-yard reach, and the bridge's take row found nobody.
 * The decision is pinned here; the adapter's wiring is pinned in the source
 * because it reads a live character.
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

void TheArrivalDecision()
{
    Expect(SpawnWalkArrivalHoldFor(false, false, true) == SpawnWalkArrivalHold::LingerForQuest,
           "a walk that ends at a quest giver or taker lingers for the quest row");
    Expect(SpawnWalkArrivalHoldFor(false, false, false) == SpawnWalkArrivalHold::LetGo,
           "a walk to a node or any other spawn lets its walker go, as before");
    Expect(SpawnWalkArrivalHoldFor(true, true, true) == SpawnWalkArrivalHold::HoldForParty &&
               SpawnWalkArrivalHoldFor(true, false, false) == SpawnWalkArrivalHold::HoldForParty,
           "a quest party's leader is still held for its party");
    Expect(SpawnWalkArrivalHoldFor(false, true, true) == SpawnWalkArrivalHold::Stay &&
               SpawnWalkArrivalHoldFor(false, true, false) == SpawnWalkArrivalHold::Stay,
           "a row's own stay: wins over the quest linger (wow-overseer#888)");
    Expect(SPAWN_WALK_QUEST_LINGER_SECONDS > 0 && SPAWN_WALK_QUEST_LINGER_SECONDS <= 120,
           "the linger is bounded, and no longer than a vendor walk's");
}

void TheAdapterLingersAndTheRowLiftsIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    std::size_t const arrival =
        src.find("state == D::MailWalkState::Arrived && ev.goal == D::WalkGoal::Spawn");
    Expect(arrival != std::string::npos, "the spawn walk's arrival branch exists");
    std::size_t const decide = src.find(
        "D::SpawnWalkArrivalHoldFor(\n                    party != nullptr, "
        "ev.spawnStaySeconds > 0,\n"
        "                    SpawnIsQuestHolder(ev.spawnIsObject, ev.mailboxEntry));",
        arrival);
    std::size_t const linger = src.find("LingerAtTheQuestHolder(bot, check.targetName);", arrival);
    std::size_t const release =
        src.find("ReleaseHold(check.targetName, bot, \"the walk to its work is over\"", arrival);
    Expect(decide != std::string::npos && linger != std::string::npos && decide < linger &&
               linger < release,
           "the arrival asks the decision and lingers at a quest holder before the release");

    std::size_t const holder = src.find("static bool SpawnIsQuestHolder(bool object, uint32 entry)");
    Expect(holder != std::string::npos &&
               src.find("sObjectMgr->GetCreatureQuestInvolvedRelationMap()", holder) <
                   src.find("static std::map<std::string, time_t>& QuestHolderLingers()") &&
               src.find("sObjectMgr->GetGOQuestInvolvedRelationMap()", holder) <
                   src.find("static std::map<std::string, time_t>& QuestHolderLingers()"),
           "a quest holder is a starter or ender, creature or gameobject, from the world's relations");

    std::size_t const lingerFn = src.find("static void LingerAtTheQuestHolder(");
    Expect(lingerFn != std::string::npos &&
               src.find("HoldAtTheObjective(who, name, "
                        "OverseerDecisions::SPAWN_WALK_QUEST_LINGER_SECONDS);",
                        lingerFn) != std::string::npos,
           "the linger is the walk's own hold, bounded by SPAWN_WALK_QUEST_LINGER_SECONDS");

    std::size_t const endFn = src.find("static void EndQuestHolderLinger(Player* who)");
    Expect(endFn != std::string::npos &&
               src.find("hold->second.until != until", endFn) != std::string::npos &&
               src.find("ReleaseHold(name, who, \"the quest row at its giver ran\", "
                        "MAIL_WALK_HOLD_VERB);",
                        endFn) != std::string::npos,
           "the quest row lifts only the hold its linger placed");

    std::size_t const doQuest = src.find("static char const* DoQuest(");
    std::size_t const refusal =
        src.find("OverseerDecisions::QuestErrandRefusal(errand, facts);", doQuest);
    std::size_t const lift = src.find("EndQuestHolderLinger(player);", doQuest);
    std::size_t const take = src.find("player->AddQuestAndCheckCompletion(quest, giver);", doQuest);
    Expect(doQuest != std::string::npos && refusal < lift && lift < take,
           "DoQuest ends the linger once the giver was read, before any answer");
}

void TheDiagnosticFindsTheLiving()
{
    std::string const src = Read("src/mod_overseer.cpp");
    std::size_t const fn = src.find("static Creature* QuestCreatureInReach(");
    std::size_t const end = src.find("static bool QuestRelationNames(", fn);
    Expect(fn != std::string::npos && end != std::string::npos, "the giver finder exists");
    std::size_t const living = src.find("player->FindNearestCreature(entry, 120.0f, true);", fn);
    std::size_t const dead = src.find("player->FindNearestCreature(entry, 120.0f, false);", fn);
    // FindNearestCreature's third argument is the live state wanted: `false`
    // finds only dead creatures, so the old line said "no creature of that
    // entry within 120 yards" beside a living giver.
    Expect(living != std::string::npos && living < end,
           "the nearest-is diagnostic searches living creatures");
    Expect(dead == std::string::npos || (living < dead && dead < end),
           "and only falls back to a dead one after the living search");
}

}  // namespace

int main()
{
    TheArrivalDecision();
    TheAdapterLingersAndTheRowLiftsIt();
    TheDiagnosticFindsTheLiving();
    if (failures)
    {
        std::fprintf(stderr, "%d quest giver linger check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_quest_giver_linger: ok\n");
    return 0;
}
