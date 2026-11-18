/*
 * Each death of a playerbot logs the new rpg status that was driving it.
 *
 * THE GAP (a public tracker ticket on repeated deaths in Silithus). The
 * escape from a lethal zone fires, and the members come back through the
 * playerbot's own `new rpg` status rather than a pick this module made. The
 * death line says only that a playerbot on `new rpg` moved the character; it
 * does not say which status (go grind, go camp, do quest, wander npc, ...)
 * or where it was heading, so the next read cannot tell the statuses apart.
 *
 * THE LINE. One per death, cheap, built here so its shape is pinned:
 *   death status - 'Name' (level 60) map 1 zone 1377: new rpg status 'go grind'
 *   heading to map 1 (-6968.0, 1175.0, 20.0)
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The second
 * half reads src/mod_overseer.cpp as text and pins that the death hook logs
 * the line for a playerbot and does not query the database to do it.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::NewRpgDeathLine;
using OverseerDecisions::NewRpgDeathTarget;

namespace
{

int failures = 0;

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    '%s'\n  wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

}  // namespace

int main()
{
    // A position target prints its map and one decimal per axis.
    std::string const where = NewRpgDeathTarget("position", 1, -6968.f, 1175.f, 20.f, 0);
    CheckText("a position target", where, "map 1 (-6968.0, 1175.0, 20.0)");

    // A creature or object target prints its entry.
    CheckText("an entry target", NewRpgDeathTarget("entry", 0, 0.f, 0.f, 0.f, 8888), "entry 8888");

    // A quest target prints the quest id.
    CheckText("a quest target", NewRpgDeathTarget("quest", 0, 0.f, 0.f, 0.f, 3801), "quest 3801");

    // Nothing to name.
    CheckText("no target", NewRpgDeathTarget("none", 0, 0.f, 0.f, 0.f, 0), "");
    CheckText("a misspelt kind is named, not dropped",
              NewRpgDeathTarget("positon", 1, 0.f, 0.f, 0.f, 0), "unknown target kind 'positon'");

    // The whole line.
    CheckText("a line with a target",
              NewRpgDeathLine("Workwork", 60, 1, 1377, true, "go grind", where),
              "death status - 'Workwork' (level 60) map 1 zone 1377: new rpg status 'go grind' "
              "heading to map 1 (-6968.0, 1175.0, 20.0)");
    CheckText("a line without a target",
              NewRpgDeathLine("Erg", 60, 1, 1377, true, "rest", ""),
              "death status - 'Erg' (level 60) map 1 zone 1377: new rpg status 'rest'");
    CheckText("a character with no playerbot AI",
              NewRpgDeathLine("Zug", 60, 0, 46, false, "", ""),
              "death status - 'Zug' (level 60) map 0 zone 46: no playerbot AI");
    CheckText("an empty status is named, not left blank",
              NewRpgDeathLine("Erg", 60, 1, 1377, true, "", ""),
              "death status - 'Erg' (level 60) map 1 zone 1377: new rpg status 'unknown'");

    // The adapter logs it from the death hook, for a playerbot, with no query.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::size_t const hook = text.find("void OnPlayerJustDied(Player* player) override");
    std::size_t const hookEnd = hook == std::string::npos ? hook : text.find("\n    }\n", hook);
    std::string const body =
        hook == std::string::npos ? std::string() : text.substr(hook, hookEnd - hook);
    Check("the death hook logs the status line",
          body.find("LogNewRpgDeath(player)") != std::string::npos, true);
    std::size_t const guard = body.find("if (OnRoster(player->GetName()) || InNaturalGuild(player))");
    Check("and only for a roster or guild member",
          guard != std::string::npos && guard < body.find("LogNewRpgDeath(player)"), true);
    std::size_t const helper = text.find("static void LogNewRpgDeath(Player* player)");
    std::size_t const helperEnd = helper == std::string::npos ? helper : text.find("\n}\n", helper);
    std::string const helperBody =
        helper == std::string::npos ? std::string() : text.substr(helper, helperEnd - helper);
    Check("the status is read off the playerbot's own rpgInfo",
          helperBody.find("botAI->rpgInfo.GetStatus()") != std::string::npos, true);
    Check("the line is built by the shared decision",
          helperBody.find("OverseerDecisions::NewRpgDeathLine(") != std::string::npos, true);
    Check("the helper does not query a database",
          !helperBody.empty() && helperBody.find("Query(") == std::string::npos, true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("new rpg death line: all checks passed\n");
    return EXIT_SUCCESS;
}
