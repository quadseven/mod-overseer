/*
 * kind='quest' `use-item-on` and `use-gameobject`: the grammar, the gate one
 * row at a time in the order the gate asks, the retry table, and the judge.
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;
namespace R = OverseerDecisions::QuestUseRefusal;

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

void ExpectText(char const* what, std::string const& got, char const* want)
{
    if (got != want)
    {
        std::fprintf(stderr, "FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
        ++failures;
    }
}

// A creature use that may fire: bot, in the world, alive, free, a living
// creature in reach, a carried and usable item.
QuestUseGateFacts Ready()
{
    QuestUseGateFacts f;
    f.hasBotAI = true;
    f.inWorld = true;
    f.alive = true;
    f.targetSeen = true;
    f.targetAlive = true;
    f.targetYards = 4.0f;
    f.reachYards = QUEST_USE_CREATURE_YARDS;
    return f;
}

void TheHereVerbParses()
{
    QuestUseRequest r = ParseQuestUseRequest("use-item-here item:38607");
    Expect(*r.error == '\0' && r.here && !r.gameObject && r.item == 38607 && r.target == 0,
           "use-item-here item:N parses with no target");
    ExpectText("a missing item", ParseQuestUseRequest("use-item-here").error, R::MalformedHere);
    ExpectText("a creature key is no here key",
               ParseQuestUseRequest("use-item-here creature:5").error, R::MalformedHere);
    ExpectText("a zero item", ParseQuestUseRequest("use-item-here item:0").error,
               R::MalformedHere);
    ExpectText("an extra word", ParseQuestUseRequest("use-item-here item:5 item:6").error,
               R::MalformedHere);
    Expect(IsQuestUseRow("use-item-here item:38607"), "use-item-here is a use row");
    Expect(QuestUseRefusalRetry(R::MalformedHere) == TownRetry::Never, "malformed here never retried");
    Expect(QuestUseRefusalRetry(R::NoHereSpell) == TownRetry::Never, "no here spell never retried");
}

void TheRowIsRecognisedByItsFirstWord()
{
    Expect(IsQuestUseRow("use-item-on creature:1 item:2"), "use-item-on is a use row");
    Expect(IsQuestUseRow("use-gameobject 5"), "use-gameobject is a use row");
    Expect(IsQuestUseRow("use-item-on"), "a malformed one still routes to its parser");
    Expect(!IsQuestUseRow("take quest:5"), "take is not a use row");
    Expect(!IsQuestUseRow("turnin quest:5"), "turnin is not a use row");
    Expect(!IsQuestUseRow(""), "empty is not a use row");
}

void TheItemGrammar()
{
    QuestUseRequest r = ParseQuestUseRequest("use-item-on creature:3125 item:5362");
    Expect(*r.error == 0 && !r.gameObject && r.target == 3125 && r.item == 5362, "both keys");
    r = ParseQuestUseRequest("use-item-on item:5362 creature:3125");
    Expect(*r.error == 0 && r.target == 3125 && r.item == 5362, "either order");
    r = ParseQuestUseRequest("  use-item-on   creature:7 item:8 ");
    Expect(*r.error == 0 && r.target == 7 && r.item == 8, "whitespace tolerant");

    char const* const bad[] = {
        "use-item-on",
        "use-item-on creature:1",
        "use-item-on item:1",
        "use-item-on creature:1 item:",
        "use-item-on creature:0 item:2",
        "use-item-on creature:1 item:0",
        "use-item-on creature:1 creature:2",
        "use-item-on item:1 item:2",
        "use-item-on creature:x item:2",
        "use-item-on creature:1 item:2 extra",
        "use-item-on creature:1 gameobject:2",
        "use-item-on creature:1234567890 item:2",
        "use-item-on 1 2",
    };
    for (char const* command : bad)
    {
        r = ParseQuestUseRequest(command);
        ExpectText(command, r.error, R::MalformedItem);
    }
}

void TheObjectGrammar()
{
    QuestUseRequest r = ParseQuestUseRequest("use-gameobject 2086");
    Expect(*r.error == 0 && r.gameObject && r.target == 2086 && r.item == 0, "an entry");

    char const* const bad[] = {
        "use-gameobject",
        "use-gameobject 0",
        "use-gameobject abc",
        "use-gameobject 12 13",
        "use-gameobject gameobject:12",
        "use-gameobject 1234567890",
    };
    for (char const* command : bad)
    {
        r = ParseQuestUseRequest(command);
        ExpectText(command, r.error, R::MalformedObject);
    }
}

void TheGateAnswersEachWallInOrder()
{
    ExpectText("a ready use", QuestUseGate(Ready()), "");

    QuestUseGateFacts f = Ready();
    f.hasBotAI = false;
    ExpectText("no bot AI", QuestUseGate(f), R::NoBotAI);

    f = Ready();
    f.inWorld = false;
    ExpectText("not in the world", QuestUseGate(f), R::NotInWorld);

    f = Ready();
    f.loggingOut = true;
    ExpectText("logging out", QuestUseGate(f), R::LoggingOut);

    f = Ready();
    f.alive = false;
    ExpectText("dead", QuestUseGate(f), R::Dead);

    f = Ready();
    f.inFlight = true;
    ExpectText("on a flight path", QuestUseGate(f), R::InFlight);

    f = Ready();
    f.inCombat = true;
    ExpectText("in combat", QuestUseGate(f), R::InCombat);

    f = Ready();
    f.inInstance = true;
    ExpectText("in an instance", QuestUseGate(f), R::InInstance);

    f = Ready();
    f.moving = true;
    ExpectText("moving", QuestUseGate(f), R::Moving);

    f = Ready();
    f.alreadyRunning = true;
    ExpectText("already running", QuestUseGate(f), R::AlreadyRunning);

    f = Ready();
    f.heldByAnother = true;
    ExpectText("held by another verb", QuestUseGate(f), R::HeldByAnother);

    f = Ready();
    f.targetSeen = false;
    ExpectText("no target", QuestUseGate(f), R::NoTarget);

    f = Ready();
    f.targetAlive = false;
    ExpectText("dead creature", QuestUseGate(f), R::TargetDead);

    f = Ready();
    f.targetYards = QUEST_USE_CREATURE_YARDS + 0.5f;
    ExpectText("too far", QuestUseGate(f), R::TooFar);

    f = Ready();
    f.targetYards = QUEST_USE_CREATURE_YARDS;
    ExpectText("exactly at reach is in reach", QuestUseGate(f), "");

    f = Ready();
    f.targetYards = -1.0f;
    ExpectText("an unread distance is too far", QuestUseGate(f), R::TooFar);

    f = Ready();
    f.wrongTarget = true;
    ExpectText("wrong target", QuestUseGate(f), R::WrongTarget);

    f = Ready();
    f.itemCarried = false;
    ExpectText("item missing", QuestUseGate(f), R::ItemMissing);

    f = Ready();
    f.itemUsable = false;
    ExpectText("item unusable", QuestUseGate(f), R::ItemUnusable);

    f = Ready();
    f.itemOnCooldown = true;
    ExpectText("item on cooldown", QuestUseGate(f), R::ItemOnCooldown);

    // Order: the character's walls before the target's, the target's before
    // the item's.
    f = Ready();
    f.alive = false;
    f.targetSeen = false;
    f.itemCarried = false;
    ExpectText("dead outranks no target", QuestUseGate(f), R::Dead);
    f = Ready();
    f.targetYards = 99.0f;
    f.itemCarried = false;
    ExpectText("too far outranks a missing item", QuestUseGate(f), R::TooFar);
}

void AGameObjectUseAsksNothingOfAnItemOrALife()
{
    QuestUseGateFacts f = Ready();
    f.gameObject = true;
    f.targetAlive = false;
    f.itemCarried = false;
    f.wrongTarget = true;
    f.reachYards = 5.5f;
    f.targetYards = 5.0f;
    ExpectText("an object ignores item and life", QuestUseGate(f), "");
    f.targetYards = 6.0f;
    ExpectText("an object past its interaction distance", QuestUseGate(f), R::TooFar);
    f.targetSeen = false;
    ExpectText("no object of that entry", QuestUseGate(f), R::NoTarget);
}

// Perrine's Chest (37098) and the Burning Blade Stash (58595) carry lock 43: a
// click at either did nothing on the dev realm (2026-10-09), because a chest
// opens only to the spell its lock names.
void AChestIsOpenedWithTheSpellItsLockNames()
{
    // Lock type 5 asks no skill; a key case (type 1) needs an item, not a spell.
    std::vector<ChestLockCase> const lock = {{1, 6893, true}, {LOCK_CASE_SKILL, 5, true}};
    std::vector<LockOpener> const known = {
        {2575, 3},   // Mining opens a mining lock, not this one
        {6477, 10},  // an opening of another lock type
        {21651, 5},
        {3365, 5},
    };
    Expect(ChestOpeningSpell(lock, known) == 3365,
           "the lowest known spell of the skill case's lock type opens the chest");
    Expect(ChestOpeningSpell(lock, {{2575, 3}, {6477, 10}}) == 0,
           "no known spell of the lock's type: no opener");
    Expect(ChestOpeningSpell({{1, 6893, true}}, known) == 0,
           "a key lock is not opened by a spell");
    Expect(ChestOpeningSpell({}, known) == 0, "no lock row: the open-lock cast is refused, no opener");
    Expect(ChestOpeningSpell({{LOCK_CASE_SKILL, 5, false}}, known) == 0,
           "a skill case the character does not meet is skipped");
    Expect(ChestOpeningSpell({{LOCK_CASE_SKILL, 5, false}, {LOCK_CASE_SKILL, 10, true}}, known) ==
               6477,
           "the next skill case the character meets is tried");

    QuestUseGateFacts f = Ready();
    f.gameObject = true;
    f.chest = true;
    f.reachYards = 5.5f;
    f.targetYards = 0.0f;
    ExpectText("a chest no known spell opens is refused, not clicked", QuestUseGate(f),
               R::NoOpener);
    f.opener = 3365;
    ExpectText("a chest with an opener may fire", QuestUseGate(f), "");
    f.chest = false;
    f.opener = 0;
    ExpectText("an object that is no chest is still clicked", QuestUseGate(f), "");
    Expect(QuestUseRefusalRetry(R::NoOpener) == TownRetry::Never, "no opener: never");
}

void TheRetryTable()
{
    Expect(QuestUseRefusalRetry(R::MalformedItem) == TownRetry::Never, "malformed item: never");
    Expect(QuestUseRefusalRetry(R::MalformedObject) == TownRetry::Never, "malformed object: never");
    Expect(QuestUseRefusalRetry(R::WrongTarget) == TownRetry::Never, "wrong target: never");
    Expect(QuestUseRefusalRetry(R::ItemMissing) == TownRetry::Never, "item missing: never");
    Expect(QuestUseRefusalRetry(R::InInstance) == TownRetry::Elsewhere, "instance: elsewhere");
    Expect(QuestUseRefusalRetry(R::TooFar) == TownRetry::Later, "too far: later");
    Expect(QuestUseRefusalRetry(R::TargetDead) == TownRetry::Later, "dead target respawns: later");
    Expect(QuestUseRefusalRetry(R::InCombat) == TownRetry::Later, "combat: later");
}

void TheJudge()
{
    QuestUseReadBack read;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Nothing, "nothing read, nothing happened");
    read.itemConsumed = true;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Spent, "a consumed item is spent");
    read = QuestUseReadBack{};
    read.lootOpened = true;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Spent, "an opened loot window is spent");
    read = QuestUseReadBack{};
    read.onCooldown = true;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Spent, "a started cooldown is spent");
    read.counterMoved = true;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Progressed, "a moved counter outranks spent");
    read.readable = false;
    Expect(JudgeQuestUse(read) == QuestUseOutcome::Unreadable, "unreadable outranks everything");
    ExpectText("word progressed", QuestUseOutcomeWord(QuestUseOutcome::Progressed), "progressed");
    ExpectText("word spent", QuestUseOutcomeWord(QuestUseOutcome::Spent), "spent");
    ExpectText("word nothing", QuestUseOutcomeWord(QuestUseOutcome::Nothing), "nothing");
    ExpectText("word unreadable", QuestUseOutcomeWord(QuestUseOutcome::Unreadable), "unreadable");
}

}  // namespace

int main()
{
    TheHereVerbParses();
    TheRowIsRecognisedByItsFirstWord();
    TheItemGrammar();
    TheObjectGrammar();
    TheGateAnswersEachWallInOrder();
    AGameObjectUseAsksNothingOfAnItemOrALife();
    AChestIsOpenedWithTheSpellItsLockNames();
    TheRetryTable();
    TheJudge();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
