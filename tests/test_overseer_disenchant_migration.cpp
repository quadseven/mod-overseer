/*
 * The `kind` column of `overseer_command` is a 21-value ENUM after this
 * migration (mod-overseer#444). `ALTER ... MODIFY` replaces the whole list,
 * so a migration that forgets any of the 20 pre-existing values deletes
 * those verbs from the schema silently, after which every row of that kind
 * fails to insert. This test pins the full list against the migration file.
 *
 * Compiled against nothing; reads data/sql/characters/base/ as text
 * (run from the repo root).
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    // The migration that adds 'disenchant' to the kind ENUM.
    std::ifstream in("data/sql/characters/base/2026_10_26_00_overseer_disenchant.sql");
    if (!in)
    {
        std::printf("FAIL could not read the disenchant migration (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    // All 20 pre-existing values must still be present (the migration replaces
    // the whole list rather than appending).
    Check("the migration keeps 'bot'",          Has(src, "'bot'"));
    Check("the migration keeps 'chat'",          Has(src, "'chat'"));
    Check("the migration keeps 'gm'",            Has(src, "'gm'"));
    Check("the migration keeps 'probe'",         Has(src, "'probe'"));
    Check("the migration keeps 'give'",          Has(src, "'give'"));
    Check("the migration keeps 'share'",         Has(src, "'share'"));
    Check("the migration keeps 'trade'",         Has(src, "'trade'"));
    Check("the migration keeps 'job'",           Has(src, "'job'"));
    Check("the migration keeps 'sell'",          Has(src, "'sell'"));
    Check("the migration keeps 'bank'",          Has(src, "'bank'"));
    Check("the migration keeps 'auction'",       Has(src, "'auction'"));
    Check("the migration keeps 'mail'",          Has(src, "'mail'"));
    Check("the migration keeps 'repair'",        Has(src, "'repair'"));
    Check("the migration keeps 'buy'",           Has(src, "'buy'"));
    Check("the migration keeps 'bind'",          Has(src, "'bind'"));
    Check("the migration keeps 'hearth'",        Has(src, "'hearth'"));
    Check("the migration keeps 'summon'",        Has(src, "'summon'"));
    Check("the migration keeps 'conjure'",       Has(src, "'conjure'"));
    Check("the migration keeps 'cast'",          Has(src, "'cast'"));
    Check("the migration keeps 'guild'",         Has(src, "'guild'"));

    // The new value is added.
    Check("the migration adds 'disenchant'",     Has(src, "'disenchant'"));

    // The ENUM is a single MODIFY COLUMN statement listing all 21 values.
    Check("the migration MODIFYs the kind column", Has(src, "MODIFY COLUMN `kind` ENUM("));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_overseer_disenchant_migration\n");
    return 0;
}
