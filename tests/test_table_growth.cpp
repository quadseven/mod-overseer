/*
 * The overseer tables stop costing more with every day they have been alive.
 *
 * The live failure this pins. On the dev realm on 2026-10-10 overseer_command
 * held 828,287 rows in 1.2 GB, and nothing ever deleted one. 562,489 of them
 * (663 MB of `result`) were kind 'probe': read-only questions whose answer is
 * read back by id within seconds of being asked and never again. Today's site
 * outage (a decree read at 13.5 s, 2026-10-10_00's index) was the first read to
 * cross a timeout; the growth that made it is still there. And the module's own
 * five-minute death sweep reads every overseer_death row on every run, because
 * no key starts with `created_at`: 55,650 rows a sweep today, on the way to
 * about 720,000 at the 90-day retention.
 *
 * So the five-minute sweep also deletes answered probes past a day, a bounded
 * batch at a time, and a guarded migration gives the death sweep its key and
 * the windowed source readers (source, created_at).
 *
 * Reads src/mod_overseer.cpp and the migration to pin the wiring (run from the
 * repo root). Compiled against src/overseer_decisions.cpp like every test here,
 * though it calls nothing in it.
 */

#include "overseer_decisions.h"

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

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

// The SQL with its `--` comment lines taken out, so a comment that quotes a
// statement is not read as the statement.
std::string Statements(std::string const& sql)
{
    std::istringstream lines(sql);
    std::string line;
    std::string out;
    while (std::getline(lines, line))
    {
        std::size_t const first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "--") == 0)
            continue;
        out += line;
        out += '\n';
    }
    return out;
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

void TheSweepPrunesAnsweredProbes()
{
    std::string const source = Read("src/mod_overseer.cpp");
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }

    Check("probe answers are kept a day",
          Has(source, "constexpr uint32 PROBE_RETENTION_HOURS = 24;"));
    Check("a sweep deletes a bounded batch, so the backlog drains over hours",
          Has(source, "constexpr uint32 PROBE_SWEEP_BATCH = 5000;"));

    std::string const sweep = Between(source, "if (_sweepTimer >= CHAT_SWEEP_MS)",
                                      "KeepHeldCharactersStill();");
    Check("the five-minute sweep is where it runs", !sweep.empty());
    Check("only kind 'probe' is deleted, on updated_at, a batch at a time",
          Has(sweep, "\"DELETE FROM overseer_command WHERE kind = 'probe' \"") &&
              Has(sweep, "\"AND updated_at < NOW() - INTERVAL {} HOUR LIMIT {}\"") &&
              Has(sweep, "PROBE_RETENTION_HOURS, PROBE_SWEEP_BATCH);"));
    Check("no other kind of order is pruned: they are the realm's history",
          !Has(sweep, "DELETE FROM overseer_command WHERE created_at") &&
              !Has(sweep, "DELETE FROM overseer_command WHERE kind <>") &&
              !Has(sweep, "DELETE FROM overseer_command WHERE kind IN"));
}

void TheMigrationAddsBothKeysGuarded()
{
    std::string const migration =
        Statements(Read("data/sql/characters/base/2026_10_10_02_overseer_growth_indexes.sql"));
    Check("the migration exists", !migration.empty());
    Check("the death sweep gets a key on created_at",
          Has(migration, "ALTER TABLE `overseer_death` ADD KEY `idx_created_at` (`created_at`)"));
    Check("the windowed source readers get (source, created_at)",
          Has(migration,
              "ALTER TABLE `overseer_command` ADD KEY `idx_source_created` (`source`, `created_at`)"));
    Check("each key is guarded on INFORMATION_SCHEMA.STATISTICS",
          Has(migration, "AND INDEX_NAME = 'idx_created_at'") &&
              Has(migration, "AND INDEX_NAME = 'idx_source_created'") &&
              Has(migration, "INFORMATION_SCHEMA.STATISTICS"));
    Check("no column, row or reader changes",
          !Has(migration, "DROP") && !Has(migration, "DELETE") && !Has(migration, "ADD COLUMN"));
}

}  // namespace

int main()
{
    TheSweepPrunesAnsweredProbes();
    TheMigrationAddsBothKeysGuarded();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("table growth: all checks passed\n");
    return 0;
}
