/*
 * Source contract for the realm-wide tally (mod-overseer#1552): the hooks count
 * every character into the aggregate, and a guildless bot still gets no
 * per-death row.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

std::string Between(std::string const& text, std::string const& from, std::string const& to)
{
    std::size_t const a = text.find(from);
    if (a == std::string::npos)
        return "";
    std::size_t const b = text.find(to, a + from.size());
    return b == std::string::npos ? "" : text.substr(a, b - a);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const death = Between(text, "void RecordDeath(Player* player)", "PendingDeath d;");
    if (death.empty())
        return Fail("cannot find RecordDeath up to the row it builds");
    std::size_t const counted = death.find("NoteRealmDeath(player);");
    std::size_t const gate = death.find("if (!onRoster && !inGuild)");
    if (counted == std::string::npos || gate == std::string::npos || counted > gate)
        return Fail("every death must be counted before the managed-only row gate");
    if (death.find("return;", gate) == std::string::npos)
        return Fail("a guildless bot must still return before any per-death row is queued");

    std::string const noteDeath =
        Between(text, "static void NoteRealmDeath(Player* player)", "static void NoteRealmKiller(");
    if (noteDeath.find("g_realmTally.NoteDeath(") == std::string::npos ||
        noteDeath.find("g_deathQueue") != std::string::npos ||
        noteDeath.find("PendingDeath") != std::string::npos)
        return Fail("the realm death hook must write the aggregate and never a death row");

    std::string const level = Between(text, "static void RecordLevel(Player* player, uint8 oldLevel)",
                                      "void RecordDeath(Player* player)");
    std::size_t const levelCounted = level.find("NoteRealmLevel(player);");
    std::size_t const levelGate = level.find("OverseerDecisions::RecordsLevel(");
    if (levelCounted == std::string::npos || levelGate == std::string::npos ||
        levelCounted > levelGate)
        return Fail("every level change must be counted before the managed-only row gate");

    if (text.find("NoteRealmKiller(killed, killer);") == std::string::npos)
        return Fail("the creature kill hook must remember the killer for every victim");

    std::string const flush = Between(text, "void FlushRealmTally(", "void FlushDeaths()");
    if (flush.find("INSERT INTO overseer_realm_hour") == std::string::npos ||
        flush.find("ON DUPLICATE KEY UPDATE deaths = deaths + VALUES(deaths)") ==
            std::string::npos)
        return Fail("the flush must add the aggregate to overseer_realm_hour");
    if (text.find("FlushRealmTally(true);") == std::string::npos ||
        text.find("            FlushRealmTally();") == std::string::npos)
        return Fail("the tally must be flushed on the timer and at shutdown");
    if (text.find("DELETE FROM overseer_realm_hour WHERE hour_start") == std::string::npos)
        return Fail("the aggregate table must be pruned");

    std::cout << "realm tally source: all checks passed\n";
    return EXIT_SUCCESS;
}
