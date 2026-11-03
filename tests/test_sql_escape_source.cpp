/*
 * Every string the adapter writes into SQL goes through Esc().
 *
 * src/mod_overseer.cpp builds its statements with the core's `{}` formatting
 * and six ostringstream batches, neither of which escapes anything: Esc()
 * (truncate, then CharacterDatabase.EscapeString) is the one discipline, and
 * an audit of the file found it applied at every data-bearing string slot but
 * two. The job write in DoJob embedded the allow-listed mode and the player's
 * name raw while the two reads around it escaped the same name, and the
 * presence snapshot batch embedded every online player's name raw. Neither is
 * exploitable today, because the mode is matched against fixed lists first
 * and the core only admits letters into a character name; both are a
 * loosening away from a quote in a statement. This pins the two sites so the
 * discipline holds everywhere, and fails against main, where both are raw.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
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

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}
}

int main()
{
    std::string const source = ReadSource();
    Check("source is readable", !source.empty());

    std::string const doJob = Between(source, "static char const* DoJob(",
                                      "static char const* DoGuild(");
    Check("DoJob is found", !doJob.empty());
    std::string::size_type const jobWrite =
        doJob.find("\"UPDATE overseer_roster SET job = '{}' WHERE name = '{}'\"");
    Check("DoJob writes the job column", jobWrite != std::string::npos);
    if (jobWrite != std::string::npos)
    {
        std::string const args = doJob.substr(jobWrite, 240);
        Check("the job write escapes the mode",
              args.find("Esc(mode)") != std::string::npos);
        Check("the job write escapes the name",
              args.find("Esc(player->GetName())") != std::string::npos);
        Check("the job write embeds no raw mode",
              args.find("\n            mode,") == std::string::npos);
    }

    std::string const snapshot = Between(source, "REPLACE INTO overseer_snapshot (guid, name, level",
                                         "<< uint32(p->GetLevel())");
    Check("the snapshot batch is found", !snapshot.empty());
    Check("the snapshot batch escapes the player name",
          snapshot.find("<< Esc(p->GetName()) <<") != std::string::npos);
    Check("the snapshot batch embeds no raw player name",
          snapshot.find("<< p->GetName() <<") == std::string::npos);

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("every string the adapter writes into SQL is escaped");
    return 0;
}
