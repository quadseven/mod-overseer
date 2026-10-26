/*
 * A guild finder run older than 3 hours is read again after a worldserver
 * restart (#1138, wow-overseer#789).
 *
 * The live failure (dev realm, 2026-10-24 roll): a Blackfathom Deeps guild
 * finder run, 4 of 7 bosses down and 12263 s inside, was neither restored nor
 * adopted, because both restart reads took only rows created in the last 3
 * hours. The claim sweep then ended it as "claim expired". Its ceiling was
 * 14400 s, so the run was still legal.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
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

constexpr std::uint32_t BLACKFATHOM_MAP = 48;

// What both restart reads ask of a row's age: created_at > NOW() - window.
bool RestartReadTakes(unsigned ageSeconds)
{
    return ageSeconds < GUILD_RUN_RESTART_WINDOW_SECONDS;
}

void TheLiveBlackfathomRowIsRead()
{
    // The 2026-10-24 row: 12263 s since it was created, inside its ceiling.
    unsigned const ceiling = GuildRunCeilingForMap(BLACKFATHOM_MAP, true, 7);
    Check("the Blackfathom ceiling is 14400 s", ceiling == 14400);
    Check("the 12263 s row is inside its ceiling", 12263 < ceiling);
    Check("the 12263 s row is read again at the restart", RestartReadTakes(12263));
    Check("the old 3 hour window would not have read it", !(12263 < 3 * 60 * 60));
}

void EveryLegalRunIsRead()
{
    // The longest a run lives: its ceiling plus the progress extension.
    unsigned const blackfathom = GuildRunCeilingForMap(BLACKFATHOM_MAP, true, 7);
    unsigned const blackfathomWing = GuildRunCeilingForMap(BLACKFATHOM_MAP, false, 7);
    unsigned const depthsWing = GuildRunCeilingSeconds(false, 15);
    Check("Blackfathom is no longer than the longest ceiling",
          blackfathom <= GUILD_RUN_LONGEST_CEILING_SECONDS &&
              blackfathomWing <= GUILD_RUN_LONGEST_CEILING_SECONDS);
    Check("the Blackrock Depths wing is no longer than the longest ceiling",
          depthsWing <= GUILD_RUN_LONGEST_CEILING_SECONDS);
    Check("the base ceiling is no longer than the longest ceiling",
          GUILD_RUN_CEILING_SECONDS <= GUILD_RUN_LONGEST_CEILING_SECONDS);
    Check("a Blackfathom run at the end of its progress extension is read",
          RestartReadTakes(blackfathom + GUILD_RUN_PROGRESS_EXTENSION_SECONDS));
    Check("the window leaves a margin past the longest life",
          GUILD_RUN_RESTART_WINDOW_SECONDS >=
              GUILD_RUN_LONGEST_CEILING_SECONDS + GUILD_RUN_PROGRESS_EXTENSION_SECONDS +
                  GUILD_RUN_RESTART_MARGIN_SECONDS);
}

void ARowPastEveryCeilingIsNot()
{
    unsigned const blackfathom = GuildRunCeilingForMap(BLACKFATHOM_MAP, true, 7);
    unsigned const pastIt = blackfathom + GUILD_RUN_PROGRESS_EXTENSION_SECONDS +
                            GUILD_RUN_RESTART_MARGIN_SECONDS;
    Check("a row past its ceiling, extension and margin is not read", !RestartReadTakes(pastIt));
    Check("a row a day old is not read", !RestartReadTakes(24 * 60 * 60));
}

std::string ReadSource()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const start = source.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = source.find(to, start);
    return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Has(std::string const& in, char const* what)
{
    return in.find(what) != std::string::npos;
}

void BothReadsShareTheWindow()
{
    std::string const source = ReadSource();
    Check("the source is read", !source.empty());

    std::string const restore =
        Between(source, "void RestoreGuildRunFinderDungeons()", "if (!rows)");
    Check("the restore read is found", !restore.empty());
    Check("the restore read bounds created_at by the window",
          Has(restore, "AND created_at > NOW() - INTERVAL {} SECOND\",") &&
              Has(restore, "OverseerDecisions::GUILD_RUN_RESTART_WINDOW_SECONDS);"));
    Check("the restore read has no 3 hour bound", !Has(restore, "INTERVAL 3 HOUR"));

    std::string const strays =
        Between(source, "void DriveGuildRunStrays(std::time_t now)", "Field* const fields");
    Check("the adoption read is found", !strays.empty());
    Check("the adoption read bounds created_at by the window",
          Has(strays, "AND created_at > NOW() - INTERVAL {} SECOND) ") &&
              Has(strays, "g_runToken, OverseerDecisions::GUILD_RUN_RESTART_WINDOW_SECONDS))"));
    Check("the adoption read has no 3 hour created_at bound",
          !Has(strays, "created_at > NOW() - INTERVAL 3 HOUR"));
}

}  // namespace

int main()
{
    TheLiveBlackfathomRowIsRead();
    EveryLegalRunIsRead();
    ARowPastEveryCeilingIsNot();
    BothReadsShareTheWindow();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
