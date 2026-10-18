/*
 * Cooking needs one cast, not a trainer (quadseven/mod-overseer#534).
 *
 * Every Cooking recipe a family below 75 can reach carries
 * RequiresSpellFocus 4, "Cooking Fire", and DriveCraft cast in place, so
 * CheckCast refused every poll with SPELL_FAILED_REQUIRES_SPELL_FOCUS. A
 * Cooking Fire is conjured where the caster stands by Basic Campfire (818);
 * a Forge (3) or an Anvil (1) is a world spawn and is not.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::FocusLighterSpell;

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

void TheDecision()
{
    Check("a Cooking Fire is lit with Basic Campfire", FocusLighterSpell(4) == 818);
    Check("no focus needs nothing lit", FocusLighterSpell(0) == 0);
    Check("an Anvil is a world spawn, not conjured", FocusLighterSpell(1) == 0);
    Check("a Forge is a world spawn, not conjured", FocusLighterSpell(3) == 0);
    Check("an unknown focus is not guessed at", FocusLighterSpell(999) == 0);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());

    std::size_t const drive = source.find("void DriveCraft()");
    Check("DriveCraft exists", drive != std::string::npos);
    std::size_t const end = source.find("// ----", drive);
    std::string const body =
        drive == std::string::npos ? std::string() : source.substr(drive, end - drive);
    std::size_t const refusal = body.find("if (result == SPELL_FAILED_REQUIRES_SPELL_FOCUS)");
    std::size_t const asks =
        body.find("OverseerDecisions::FocusLighterSpell(info->RequiresSpellFocus)");
    std::size_t const known = body.find("bot->HasSpell(lighter)");
    std::size_t const cast = body.find("bot->CastSpell(bot, lighter, false)");
    Check("DriveCraft asks which spell provides the focus on a focus refusal",
          refusal != std::string::npos && asks != std::string::npos && asks > refusal);
    Check("only a spell the character knows is cast (natural-only)",
          known != std::string::npos && known > asks);
    Check("and it is cast untriggered", cast != std::string::npos && cast > known);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_craft_campfire\n");
    return 0;
}
