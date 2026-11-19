// A stranded death knight on map 609, with its class step and its crossing both
// walled, is sent to the nearest reachable spawn of the death knight gryphon
// (entry 29501) that passes its spellclick conditions, and clicks it there.
// Coordinates are the dev realm's own world data read on 2026-11-19: the two
// 29501 spawns (guids 128509 and 128510, both at z 154.0) and the three
// stranded knights' standing places. Nothing here adds a spawn.
//
// Compiled against src/overseer_decisions.cpp and nothing else; the adapter's
// wiring is pinned by reading src/mod_overseer.cpp, as the other source tests do.
#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void expect(bool ok, char const* what)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

MailWalkPoint at(float x, float y, float z)
{
    MailWalkPoint p;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

SpellClickFacts stranded()
{
    SpellClickFacts f;
    f.mapId = 609;
    f.deathKnight = true;
    f.alive = true;
    f.classStepWalled = true;
    f.crossingWalled = true;
    return f;
}

SpellClickSpawn spawn(uint32_t id, float x, float y, float z, bool conditions, bool reachable)
{
    SpellClickSpawn s;
    s.spawnId = id;
    s.at = at(x, y, z);
    s.conditionsMet = conditions;
    s.reachable = reachable;
    return s;
}

std::vector<SpellClickSpawn> gryphons(bool conditions, bool reachable)
{
    return {spawn(128509, 2402.9f, -5727.0f, 154.0f, conditions, reachable),
            spawn(128510, 2409.1f, -5722.4f, 154.0f, conditions, reachable)};
}

bool has(std::string const& text, char const* what)
{
    return text.find(what) != std::string::npos;
}

std::string readFile(char const* path)
{
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
}  // namespace

int main()
{
    MailWalkPoint const saevor = at(2364.4f, -5771.3f, 151.4f);
    MailWalkPoint const brug = at(1650.9f, -5883.5f, 116.9f);

    // The nearest reachable spawn wins, with its yards.
    {
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), gryphons(true, true), saevor);
        expect(c.applies, "a stranded knight is asked");
        expect(c.index == 0, "the nearer spawn (128509) is chosen");
        expect(c.yards > 40.f && c.yards < 60.f, "its yards are the 3D distance");
        expect(std::strcmp(c.why, "") == 0, "no reason is given when one is chosen");
    }
    // The nearest is skipped when its path does not arrive; the next one is taken.
    {
        std::vector<SpellClickSpawn> s = gryphons(true, true);
        s[0].reachable = false;
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), s, saevor);
        expect(c.index == 1, "an unreachable spawn is passed over");
    }
    // The nearest is skipped when its conditions fail.
    {
        std::vector<SpellClickSpawn> s = gryphons(true, true);
        s[0].conditionsMet = false;
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), s, saevor);
        expect(c.index == 1, "a spawn failing its spellclick conditions is passed over");
    }
    // The list order does not decide.
    {
        std::vector<SpellClickSpawn> s = gryphons(true, true);
        std::swap(s[0], s[1]);
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), s, saevor);
        expect(c.index == 1 && s[1].spawnId == 128509, "map order does not decide");
    }
    // Brug and Corvane: none reachable, and the reason says so.
    {
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), gryphons(true, false), brug);
        expect(c.applies && c.index < 0, "no reachable spawn gives no choice");
        expect(std::strcmp(c.why, SpellClickWhy::Unreachable) == 0, "the reason is unreachable");
        expect(c.nearestYards > 700.f, "the nearest spawn's yards are kept for the log");
    }
    {
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), gryphons(false, true), saevor);
        expect(c.index < 0 && std::strcmp(c.why, SpellClickWhy::ConditionsFail) == 0,
               "conditions failing everywhere says so");
    }
    {
        SpellClickChoice const c = ChooseSpellClickSpawn(stranded(), {}, saevor);
        expect(c.applies && c.index < 0 && std::strcmp(c.why, SpellClickWhy::NoSpawn) == 0,
               "no spawn on the map says so");
    }

    // It is asked of nobody else.
    {
        SpellClickFacts f = stranded();
        f.classStepWalled = false;
        expect(!ChooseSpellClickSpawn(f, gryphons(true, true), saevor).applies,
               "a knight whose class step is open is not asked");
        f = stranded();
        f.crossingWalled = false;
        expect(!ChooseSpellClickSpawn(f, gryphons(true, true), saevor).applies,
               "a knight whose crossing is open is not asked");
        f = stranded();
        f.deathKnight = false;
        expect(!ChooseSpellClickSpawn(f, gryphons(true, true), saevor).applies,
               "another class is not asked");
        f = stranded();
        f.mapId = 0;
        expect(!ChooseSpellClickSpawn(f, gryphons(true, true), saevor).applies,
               "another map is not asked");
        f = stranded();
        f.alive = false;
        expect(!ChooseSpellClickSpawn(f, gryphons(true, true), saevor).applies,
               "a corpse is not asked");
    }

    // The outcome words.
    expect(SpellClickOutcomeFor(false, false, 151.f, 151.f) == SpellClickOutcome::NotInReach,
           "out of reach is not in reach");
    expect(SpellClickOutcomeFor(true, true, 151.f, 151.f) == SpellClickOutcome::Aboard,
           "a vehicle seat is aboard");
    expect(SpellClickOutcomeFor(true, false, 151.f, 200.f) == SpellClickOutcome::Aboard,
           "a lift is aboard");
    expect(SpellClickOutcomeFor(true, false, 151.f, 152.f) == SpellClickOutcome::NoEffect,
           "a click that moved nobody has no effect");
    expect(std::strcmp(SpellClickOutcomeWord(SpellClickOutcome::NoEffect), "no effect") == 0,
           "the word for no effect");
    expect(DEATH_KNIGHT_GRYPHON_ENTRY == 29501 && DEATH_KNIGHT_GRYPHON_SPELL == 54575,
           "the entry and spell are the death knight gryphon's");

    // The adapter is wired the way the decision says.
    std::string const src = readFile("src/mod_overseer.cpp");
    expect(!src.empty(), "src/mod_overseer.cpp reads (run from the repo root)");
    expect(has(src, "if (ev.spellClick)\n            return SpellClickServes(who, entry);"),
           "CreatureServesWalk accepts a gryphon spawn for a spellclick walk");
    expect(has(src, "sConditionMgr->GetConditionsForSpellClickEvent("),
           "the spellclick conditions are the core's own");
    expect(has(src, "who->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_SPELLCLICK)"),
           "the click is gated by GetNPCIfCanInteractWith with SPELLCLICK");
    expect(has(src, "npc->HandleSpellClick(bot)"), "the click is HandleSpellClick");
    expect(has(src, "D::ChooseSpellClickSpawn("), "the adapter asks the decision");
    expect(has(src, "overseer: spellclick walk"), "an attempt logs a line");
    expect(!has(src, "AddCreature") && !has(src, "SummonCreature(D::DEATH_KNIGHT_GRYPHON"),
           "no creature is added");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
