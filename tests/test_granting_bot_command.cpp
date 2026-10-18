/*
 * Natural only: a bridge command that would hand a member something it did not
 * earn is refused before mod-playerbots sees it.
 *
 * A row with no kind of its own reaches the bot engine as a whisper, verbatim.
 * `maintenance`, `autogear`, `cheat`, `summon`, `init` and `levelup` grant
 * gear, consumables, spells, cheats or a teleport. Every other verb, and the
 * natural ones that merely sound close, must still pass.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, apart from the
 * source read at the end (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GrantingBotCommandRefusal;

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

bool Refused(char const* command)
{
    return !GrantingBotCommandRefusal(command).empty();
}
}  // namespace

int main()
{
    Check("maintenance is refused", Refused("maintenance"));
    Check("autogear is refused", Refused("autogear"));
    Check("a cheat is refused", Refused("cheat gold"));
    Check("a summon is refused", Refused("summon"));
    Check("an init is refused", Refused("init=epic"));
    Check("a levelup is refused", Refused("levelup"));
    Check("case and spacing do not hide one", Refused("  MainTenance  "));

    Check("follow passes", !Refused("follow"));
    Check("a strategy change passes", !Refused("nc +new rpg"));
    Check("equipping an upgrade from the bags passes", !Refused("equip upgrade"));
    Check("a summoning stone ritual is its own kind, and `use` passes", !Refused("use Grug"));
    Check("a word that merely starts like one passes", !Refused("summoner"));
    Check("an empty command passes to the engine's own refusal", !Refused(""));

    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    std::string const s = text.str();
    std::size_t const refuse = s.find("OverseerDecisions::GrantingBotCommandRefusal(command)");
    std::size_t const hand = s.find("botAI->HandleCommand(CHAT_MSG_WHISPER, command, player);");
    Check("the delivery asks before it hands a command to the engine",
          refuse != std::string::npos && hand != std::string::npos && refuse < hand);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_granting_bot_command: all passed\n");
    return 0;
}
