/*
 * A retire asks whether the character is seated in a live guild finder run
 * (#1334).
 *
 * The bridge's retire loop deletes factory random bots at level 42 and above,
 * and the guild finder seats exactly those bots as pugs. The retire verdict
 * never asked about the run, so a pug healer fighting inside Zul'Farrak was
 * logged out and deleted, and five minutes later its run was abandoned with
 * the healer "not in world": eight runs in one day on the dev realm, every one
 * of them a pug a retire had just deleted.
 *
 * The verdict itself is pinned in test_retire.cpp. This pins the adapter: the
 * one function that reads a retire's facts reads the seat from the module's
 * live guild runs, so the first check and the delete-time check both ask it.
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
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}

int Count(std::string const& haystack, std::string const& needle)
{
    int n = 0;
    for (std::string::size_type at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        ++n;
    return n;
}
}  // namespace

int main()
{
    std::string const source = ReadSource();
    Check("source is readable", !source.empty());

    std::string const facts =
        Between(source, "OverseerDecisions::RetireFacts ReadRetireFacts(", "return facts;\n    }");
    Check("the retire facts reader is found", !facts.empty());
    Check("it sets the guild run seat", facts.find("facts.inGuildRun") != std::string::npos);
    Check("it reads the seat from the live guild runs",
          facts.find("SeatedInGuildRun(") != std::string::npos);

    std::string const seat = Between(source, "bool SeatedInGuildRun(", "\n    }\n");
    Check("the seat reader is found", !seat.empty());
    Check("the seat reader walks the live guild runs", seat.find("_guildRuns") != std::string::npos);
    Check("the seat reader matches the run's seated names", seat.find(".names") != std::string::npos);

    Check("the facts reader is not static, so it can read the runs",
          source.find("static OverseerDecisions::RetireFacts ReadRetireFacts(") == std::string::npos);
    Check("both retire checks read the facts through it",
          Count(source, "ReadRetireFacts(") == 3);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
