/*
 * Source contract for periodic OnUpdate synchronous DB query reductions (Refs #575).
 *
 * Verifies that:
 * 1. ReloadKeepReservations() fetches overseer_keep in a single sentinel LEFT JOIN
 *    query (no SELECT COUNT(*) pre-query), preserving failed-read vs empty-table
 *    semantics.
 * 2. ReloadRaidSpecTargets() caches INFORMATION_SCHEMA.TABLES presence in
 *    g_raidSpecTablePresent once found and fetches overseer_raid_spec in a single
 *    sentinel LEFT JOIN query (no SELECT COUNT(*) FROM overseer_raid_spec).
 * 3. LoadStandinRows() fetches overseer_family_standin in a single sentinel LEFT JOIN
 *    query (no SELECT COUNT(*) pre-query), preserving failed-read vs empty-table
 *    semantics.
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

std::size_t CountOccurrences(std::string const& haystack, std::string const& needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    // 1. ReloadKeepReservations
    {
        std::size_t const start = text.find("void ReloadKeepReservations()\n");
        if (start == std::string::npos)
            return Fail("cannot find ReloadKeepReservations");
        std::size_t const end = text.find("\n}\n", start);
        if (end == std::string::npos)
            return Fail("cannot find end of ReloadKeepReservations");
        std::string const body = text.substr(start, end - start);

        if (body.find("SELECT COUNT(*)") != std::string::npos)
            return Fail("ReloadKeepReservations must not run a SELECT COUNT(*) pre-query");
        if (CountOccurrences(body, "CharacterDatabase.Query(") != 1)
            return Fail("ReloadKeepReservations must issue exactly one CharacterDatabase.Query");
        if (body.find("LEFT JOIN overseer_keep") == std::string::npos)
            return Fail("ReloadKeepReservations must use a sentinel LEFT JOIN on overseer_keep");
        if (body.find("if (!rows)\n        return;") == std::string::npos)
            return Fail("ReloadKeepReservations must keep prior reservations on a failed read");
        if (body.find("if (f[0].IsNull())\n            continue;") == std::string::npos)
            return Fail("ReloadKeepReservations must skip the NULL sentinel row on an empty table");
    }

    // 2. ReloadRaidSpecTargets
    {
        std::size_t const start = text.find("void ReloadRaidSpecTargets()\n");
        if (start == std::string::npos)
            return Fail("cannot find ReloadRaidSpecTargets");
        std::size_t const end = text.find("\n}\n", start);
        if (end == std::string::npos)
            return Fail("cannot find end of ReloadRaidSpecTargets");
        std::string const body = text.substr(start, end - start);

        std::size_t const guard = body.find("if (!g_raidSpecTablePresent)");
        std::size_t const infoSchema = body.find("INFORMATION_SCHEMA.TABLES");
        std::size_t const setTrue = body.find("g_raidSpecTablePresent = true;");
        if (guard == std::string::npos || infoSchema == std::string::npos ||
            setTrue == std::string::npos || !(guard < infoSchema && infoSchema < setTrue))
            return Fail("ReloadRaidSpecTargets must guard INFORMATION_SCHEMA.TABLES with "
                        "g_raidSpecTablePresent and cache it once found");
        if (body.find("SELECT COUNT(*) FROM overseer_raid_spec") != std::string::npos)
            return Fail("ReloadRaidSpecTargets must not run a SELECT COUNT(*) pre-query on overseer_raid_spec");
        if (CountOccurrences(body, "CharacterDatabase.Query(") != 2)
            return Fail("ReloadRaidSpecTargets must have only the guarded schema check and one data query");
        if (body.find("LEFT JOIN overseer_raid_spec") == std::string::npos)
            return Fail("ReloadRaidSpecTargets must use a sentinel LEFT JOIN on overseer_raid_spec");
        if (body.find("if (!rows)\n        return;") == std::string::npos)
            return Fail("ReloadRaidSpecTargets must keep prior targets on a failed read");
        if (body.find("if (f[0].IsNull())\n            continue;") == std::string::npos)
            return Fail("ReloadRaidSpecTargets must skip the NULL sentinel row on an empty table");
    }

    // 3. LoadStandinRows
    {
        std::size_t const start =
            text.find("    bool LoadStandinRows(std::vector<OverseerDecisions::FamilyStandin>& rows)\n");
        if (start == std::string::npos)
            return Fail("cannot find LoadStandinRows");
        std::size_t const end = text.find("\n    }\n", start);
        if (end == std::string::npos)
            return Fail("cannot find end of LoadStandinRows");
        std::string const body = text.substr(start, end - start);

        if (body.find("SELECT COUNT(*)") != std::string::npos)
            return Fail("LoadStandinRows must not run a SELECT COUNT(*) pre-query");
        if (CountOccurrences(body, "CharacterDatabase.Query(") != 1)
            return Fail("LoadStandinRows must issue exactly one CharacterDatabase.Query");
        if (body.find("LEFT JOIN overseer_family_standin") == std::string::npos)
            return Fail("LoadStandinRows must use a sentinel LEFT JOIN on overseer_family_standin");
        if (body.find("if (!result)\n            return false;") == std::string::npos)
            return Fail("LoadStandinRows must return false on a failed read");
        if (body.find("if (f[0].IsNull())\n                continue;") == std::string::npos)
            return Fail("LoadStandinRows must skip the NULL sentinel row on an empty table");
    }

    std::cout << "onupdate sync queries source: all checks passed\n";
    return EXIT_SUCCESS;
}
