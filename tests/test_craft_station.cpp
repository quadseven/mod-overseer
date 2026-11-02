/*
 * A craft reaches the core only with its station in reach
 * (quadseven/wow-overseer#1786).
 *
 * A leader on a town run stood in a capital with a standing smelt. DriveCraft
 * cast first and read the focus off the refusal, so the core put "Requires
 * Forge" on the client every twenty seconds for as long as the errand stood.
 * The answer is decided before the cast now, and each way a craft cannot run
 * has one sentence.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
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

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheStep()
{
    Check("no focus casts", CraftStationStepFor(0, false, false, CraftFocusStep::Stand) ==
                                CraftStationStep::Cast);
    Check("a focus in reach casts", CraftStationStepFor(3, true, false, CraftFocusStep::Stand) ==
                                        CraftStationStep::Cast);
    Check("a forge out of reach is never cast",
          CraftStationStepFor(3, false, false, CraftFocusStep::Stand) == CraftStationStep::Defer);
    Check("a fire is lit by a cook who knows how",
          CraftStationStepFor(4, false, true, CraftFocusStep::Stand) == CraftStationStep::Light);
    Check("a fire nobody can light is deferred",
          CraftStationStepFor(4, false, false, CraftFocusStep::Stand) == CraftStationStep::Defer);
    Check("an anvil in a town's walk is walked to",
          CraftStationStepFor(1, false, false, CraftFocusStep::Walk) == CraftStationStep::Walk);
    Check("no anvil near is deferred",
          CraftStationStepFor(1, false, false, CraftFocusStep::NoneNear) == CraftStationStep::Defer);
}

void TheReason()
{
    Check("a forge names the town stop",
          CraftDeferReason(3, false, CraftFocusStep::Stand) ==
              "waiting for a town stop with a Forge");
    Check("an anvil on a campaign job names the town stop",
          CraftDeferReason(1, false, CraftFocusStep::Stand) ==
              "waiting for a town stop with an Anvil");
    Check("no anvil near says so",
          CraftDeferReason(1, true, CraftFocusStep::NoneNear) ==
              "no Anvil stands within a town's walk");
    Check("standing at it says the object is out of reach",
          CraftDeferReason(1, true, CraftFocusStep::Stand) ==
              "standing at an Anvil that does not reach");
    Check("a fire says what is missing",
          CraftDeferReason(4, false, CraftFocusStep::Stand) ==
              "does not know how to light a Cooking Fire");
    Check("a focus with no name is named by its id",
          CraftDeferReason(999, false, CraftFocusStep::Stand) ==
              "waiting for a town stop with spell focus 999");
}

void TheWalk()
{
    Check("a craft job walks", CraftMayWalk("craft", false, false));
    Check("a town run's leader in town walks", CraftMayWalk("town run", true, true));
    Check("a town run's follower waits", !CraftMayWalk("town run", false, true));
    Check("a town run's leader on the road waits", !CraftMayWalk("town run", true, false));
    Check("a dungeon job waits", !CraftMayWalk("dungeon", true, true));
}

void TheCastVerb()
{
    Check("a missing station is elsewhere, not later",
          CastRefusalRetry(CastRefusal::NoSpellFocus) == TownRetry::Elsewhere);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    size_t const drive = source.find("void DriveCraft()");
    size_t const step = source.find("CraftStationStepFor(", drive);
    size_t const cast = source.find("bot->CastSpell(bot, spellId, false)", drive);
    Check("DriveCraft decides the station before its cast",
          drive != std::string::npos && step != std::string::npos && step < cast);
    size_t const once = source.find("SayCraftPrepOnce(name, \"lighting a fire\")", drive);
    size_t const light = source.find("bot->CastSpell(bot, lighter, false)", drive);
    Check("a fire is asked for once", once != std::string::npos && once < light);
    size_t const verb = source.find("static char const* DoCast(");
    size_t const refused = source.find("Refusal::NoSpellFocus", verb);
    size_t const opcode = source.find("HandleCastSpellOpcode(", refused);
    Check("the cast verb refuses before the opcode",
          refused != std::string::npos && opcode != std::string::npos);
}

}  // namespace

int main()
{
    TheStep();
    TheReason();
    TheWalk();
    TheCastVerb();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("craft station: ok\n");
    return 0;
}
