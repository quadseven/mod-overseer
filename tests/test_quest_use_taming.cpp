/*
 * A hunter's Taming Rod fires: the reach is the rod's own spell range, the read
 * back waits out the channel, and a fight with the beast itself is no wall.
 *
 * Measured on the dev realm, 2026-10-06 to 2026-10-10: of 172 `use-item-on`
 * rows at a Taming Rod, 164 were refused before anything was cast (58 `the
 * target is too far`, 53 `character is moving`, 21 `character is in combat`,
 * 20 `no such target`, 12 `the creature is dead`). The 8 that fired all read
 * back `nothing` after 6 seconds, and 7 of them had tamed the beast: the quest
 * completed after the read. Every rod spell (19548 to 19700, 30099 to 30105)
 * is instant, channeled for 20 seconds, with a 30 yard range (Spell.dbc,
 * SpellRange.dbc, SpellDuration.dbc). No hunter in the two guilds has a pet.
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
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

// The rods' spell range (SpellRange.dbc index of 19694 and its siblings).
constexpr float ROD_RANGE_YARDS = 30.0f;
// The rods' channel (SpellDuration.dbc), and their cast time (instant).
constexpr uint32_t ROD_CHANNEL_MS = 20000;

// A hunter beside its beast with the rod in its bags.
QuestUseGateFacts Rod(float yards)
{
    QuestUseGateFacts f;
    f.hasBotAI = true;
    f.inWorld = true;
    f.alive = true;
    f.targetSeen = true;
    f.targetAlive = true;
    f.targetYards = yards;
    f.reachYards = QuestUseCreatureReach(ROD_RANGE_YARDS);
    return f;
}

void TheReachIsTheItemsOwnSpellRange()
{
    float const rod = QuestUseCreatureReach(ROD_RANGE_YARDS);
    Expect(rod > 25.0f && rod <= ROD_RANGE_YARDS, "a 30 yard rod reaches past 25 yards");
    Expect(QuestUseCreatureReach(5.0f) == QUEST_USE_CREATURE_YARDS,
           "a short spell keeps the near edge every quest item allows");
    Expect(QuestUseCreatureReach(0.0f) == QUEST_USE_CREATURE_YARDS,
           "an unread range keeps the near edge");
    Expect(QuestUseCreatureReach(std::nanf("")) == QUEST_USE_CREATURE_YARDS,
           "a nonsense range keeps the near edge");
    Expect(QuestUseCreatureReach(100.0f) <= QUEST_USE_SEARCH_YARDS,
           "never past the radius the target is looked for in");

    // Rows the realm refused `the target is too far`: Dazzlebow at 22.8 yards
    // from an Elder Springpaw, Twinkletoes at 15.6, Cowpoke at 14.3.
    ExpectText("22.8 yards from the beast", QuestUseGate(Rod(22.8f)), "");
    ExpectText("15.6 yards from the beast", QuestUseGate(Rod(15.6f)), "");
    // Mellowmon at 36.0 yards from a Dire Mottled Boar: past the rod.
    ExpectText("36.0 yards is past the rod", QuestUseGate(Rod(36.0f)), R::TooFar);
}

void TheReadBackWaitsOutTheChannel()
{
    Expect(QuestUseSpellMs(0, ROD_CHANNEL_MS) == ROD_CHANNEL_MS,
           "an instant channeled spell lasts its channel");
    Expect(QuestUseSpellMs(1500, 0) == 1500, "a cast with no channel lasts its cast");
    Expect(QuestUseSpellMs(0xFFFFFFF0u, 0x100u) == 0xFFFFFFFFu, "saturating, never wrapping");

    // The quest use's own margin, floor and ceiling (3, 6 and 30 seconds).
    uint32_t const window = CastVerifyWindowMs(QuestUseSpellMs(0, ROD_CHANNEL_MS), 3000, 6000, 30000);
    Expect(window > ROD_CHANNEL_MS, "the tame is read after the channel ends, not 6 seconds in");
    Expect(window <= 30000, "and inside the hold that covers it");
}

void AFightWithTheBeastItselfIsNoWall()
{
    QuestUseGateFacts f = Rod(4.0f);
    f.inCombat = true;
    ExpectText("a fight with something else", QuestUseGate(f), R::InCombat);
    f.fightingOnlyTarget = true;
    ExpectText("a fight only with the beast to tame", QuestUseGate(f), "");

    // A dead beast is still dead, fight or not.
    f.targetAlive = false;
    ExpectText("the beast died in the fight", QuestUseGate(f), R::TargetDead);

    // A gameobject is never fought: combat stays a wall there.
    QuestUseGateFacts o = Rod(2.0f);
    o.gameObject = true;
    o.reachYards = 5.0f;
    o.inCombat = true;
    o.fightingOnlyTarget = true;
    ExpectText("an object use in a fight", QuestUseGate(o), R::InCombat);
    QuestUseGateFacts h = Rod(0.0f);
    h.inCombat = true;
    h.fightingOnlyTarget = true;
    h.here = true;
    ExpectText("a here use in a fight", QuestUseGate(h), R::InCombat);
}

}  // namespace

int main()
{
    TheReachIsTheItemsOwnSpellRange();
    TheReadBackWaitsOutTheChannel();
    AFightWithTheBeastItselfIsNoWall();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_quest_use_taming: ok");
    return 0;
}
