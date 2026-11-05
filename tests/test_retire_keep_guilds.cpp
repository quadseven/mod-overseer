/*
 * The shipped defaults name no one deployment's guilds or characters (#1344).
 *
 * A stranger's worldserver must not inherit another realm's names. The retire
 * keep list used to default to two guilds of one realm, so a fresh install kept
 * the members of two guilds it did not have and protected none of its own. The
 * chat watch list was seeded by a shipped migration with one realm's
 * character. Both are now empty unless the operator's config or an outside
 * writer fills them.
 *
 * The decision half is pinned against overseer_decisions.h: the default is
 * empty, an empty list parses to nothing, and nothing matches it, so the retire
 * path keeps no guild beyond Overseer.Natural.Guilds. The wiring half reads the
 * adapter, the shipped config and the migration as text.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::NameListHas;
using OverseerDecisions::ParseNameList;
using OverseerDecisions::RETIRE_KEEP_GUILDS_DEFAULT;

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

std::string Read(char const* path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}

bool Has(std::string const& haystack, std::string const& needle)
{
    return haystack.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    // THE DEFAULT IS EMPTY, AND EMPTY KEEPS NO GUILD.
    Check("the retire keep list defaults to empty", std::string(RETIRE_KEEP_GUILDS_DEFAULT).empty());
    std::vector<std::string> const kept = ParseNameList(RETIRE_KEEP_GUILDS_DEFAULT);
    Check("the default parses to no names", kept.empty());
    Check("an empty csv parses to no names", ParseNameList("").empty());
    Check("a csv of separators and spaces parses to no names", ParseNameList(" , ,").empty());
    Check("an empty list keeps a named guild", !NameListHas(kept, "Any Guild"));
    Check("an empty list keeps a guildless character", !NameListHas(kept, ""));

    // A configured list still keeps its own guilds, case aside.
    std::vector<std::string> const mine = ParseNameList("My Guild, Other Guild");
    Check("a configured guild is kept", NameListHas(mine, "my guild"));
    Check("an unlisted guild is not kept", !NameListHas(mine, "Third Guild"));

    // THE ADAPTER READS THE KEY WITH THE NAMED DEFAULT, NOT A LITERAL.
    std::string const source = Read("src/mod_overseer.cpp");
    Check("the adapter source was read", !source.empty());
    std::string const keepGuilds =
        Between(source, "static std::vector<std::string> RetireKeepGuilds()", "\n    }");
    Check("RetireKeepGuilds is found", !keepGuilds.empty());
    Check("RetireKeepGuilds reads Overseer.Retire.KeepGuilds",
          Has(keepGuilds, "\"Overseer.Retire.KeepGuilds\""));
    Check("RetireKeepGuilds defaults to the named constant",
          Has(keepGuilds, "OverseerDecisions::RETIRE_KEEP_GUILDS_DEFAULT"));
    Check("RetireKeepGuilds carries no guild literal of its own",
          !Has(keepGuilds, "\"Overseer.Retire.KeepGuilds\", \""));

    // THE SHIPPED CONFIG SAYS SO.
    std::string const conf = Read("conf/mod_overseer.conf.dist");
    std::string const block = Between(conf, "#    Overseer.Retire.KeepGuilds\n", "\n\n");
    Check("the conf block is found", !block.empty());
    Check("the conf default is empty", Has(block, "#        Default:     \"\""));
    Check("the conf says an empty list keeps no guild", Has(block, "keeps no guild"));
    Check("the conf line is empty", Has(block, "#Overseer.Retire.KeepGuilds = \"\""));

    // THE CHAT WATCH LIST IS AN OUTSIDE WRITE, NOT A SHIPPED SEED.
    std::string const chat = Read("data/sql/characters/base/2026_08_22_00_overseer_chat.sql");
    Check("the chat migration was read", !chat.empty());
    Check("the chat migration still creates the watch table",
          Has(chat, "CREATE TABLE IF NOT EXISTS `overseer_chat_watch`"));
    Check("the chat migration seeds no watched character",
          !Has(chat, "INTO `overseer_chat_watch`") && !Has(chat, "INTO overseer_chat_watch"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("retire keep guilds and chat watch defaults: ok\n");
    return 0;
}
