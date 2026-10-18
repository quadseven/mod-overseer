/*
 * The command-kind header in src/mod_overseer.cpp says what each verb does
 * when its handler returns, and those sentences are read as the contract
 * (Refs #976).
 *
 * A docs-only fix once pasted the hearth's sentence ("A hearthstone has a cast
 * time") into the kind='conjure' paragraph. The conjure handler casts NOTHING
 * (#325): it holds the character still and parks the row, and
 * ResolveConjureChecks sends the first cast. This test pins the paragraph to
 * that code.
 *
 * It also pins the property the original finding asked for (a guild run's
 * "last kill" mark can never land in the future): the mark is only ever moved
 * by GuildRunKillAfter, which writes `now` and nothing else, so an adopted run
 * cannot carry a mark set backward from a row.
 *
 * Compiled against src/overseer_decisions.cpp; reads src/mod_overseer.cpp as
 * text (run from the repo root).
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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string Between(std::string const& hay, std::string const& from, std::string const& to)
{
    std::size_t const a = hay.find(from);
    if (a == std::string::npos)
        return std::string();
    std::size_t const b = hay.find(to, a);
    if (b == std::string::npos)
        return std::string();
    return hay.substr(a, b - a);
}

void TheConjureParagraphDescribesConjuring(std::string const& src)
{
    std::string const para =
        Between(src, "kind='conjure' - ", "2026_09_08_01_overseer_conjure.sql");
    Check("the kind='conjure' paragraph is found", !para.empty());
    Check("the conjure paragraph does not talk about a hearthstone",
          !Has(para, "hearthstone"));
    Check("the conjure paragraph says nothing is cast when the handler returns",
          Has(para, "NOTHING IS CAST WHEN THE HANDLER RETURNS"));
    Check("the conjure paragraph names ResolveConjureChecks as the caster",
          Has(para, "ResolveConjureChecks sends the first cast"));
    // The code the paragraph describes.
    Check("the conjure handler still casts nothing itself",
          Has(src, "ask it to stand, and cast NOTHING yet"));
}

void TheKillMarkIsOnlyMovedByTheKillClock(std::string const& src)
{
    std::size_t pos = 0;
    int assignments = 0;
    bool onlyFromClock = true;
    std::string const needle = "run.lastKillAt = ";
    while ((pos = src.find(needle, pos)) != std::string::npos)
    {
        ++assignments;
        if (src.compare(pos + needle.size(), 17, "kills.lastKillAt;") != 0)
            onlyFromClock = false;
        pos += needle.size();
    }
    Check("a guild run's kill mark is assigned somewhere", assignments > 0);
    Check("a guild run's kill mark is only ever the kill clock's", onlyFromClock);

    std::time_t const now = 1000000;
    OverseerDecisions::GuildRunKillClock const fresh =
        OverseerDecisions::GuildRunKillAfter({0, 0}, 2, now);
    Check("a new high marks the kill at now", fresh.lastKillAt == now && fresh.best == 2);
    OverseerDecisions::GuildRunKillClock const same =
        OverseerDecisions::GuildRunKillAfter(fresh, 2, now + 600);
    Check("no new high leaves the mark where it was", same.lastKillAt == now);
}

} // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    TheConjureParagraphDescribesConjuring(src);
    TheKillMarkIsOnlyMovedByTheKillClock(src);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_command_kind_doc\n");
    return 0;
}
