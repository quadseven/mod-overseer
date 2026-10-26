/*
 * The disenchant adapter must preserve the command enum and resolve loot on
 * the server before it can call an errand complete.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(char const* label, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", label);
    ++failures;
}

std::string Read(char const* path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void MigrationKeepsEveryCommandKind()
{
    std::string const sql = Read("data/sql/characters/base/2026_10_26_00_overseer_disenchant.sql");
    std::vector<std::string> const kinds = {
        "bot", "chat", "gm", "probe", "give", "share", "trade", "job", "sell", "bank",
        "auction", "mail", "repair", "buy", "bind", "hearth", "summon", "conjure", "cast",
        "guild", "disenchant"};
    Check("migration exists", !sql.empty());
    for (std::string const& kind : kinds)
        Check("migration preserves an existing command kind", sql.find("'" + kind + "'") != std::string::npos);
}

}

int main()
{
    MigrationKeepsEveryCommandKind();
    if (failures)
        return 1;
    std::puts("ok test_disenchant_source");
}
