/*
 * A bought trainer rank leaves its own overseer_event row, keyed by the rank
 * spell, not by the trade.
 *
 * overseer_event de-duplicates on (character, kind, subject id, time bucket)
 * and the later write wins for `detail`. The rank purchase and the recipes it
 * opens were both recorded as kind 'learn' with the TRADE id as the subject,
 * so in the same bucket they collapsed into one row (occurrences 2) whose
 * detail read "bought the recipes this trainer teaches". The rank, the more
 * important of the two, could not be told apart from a recipe visit.
 *
 * Run from the repo root: it reads src/mod_overseer.cpp.
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

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string Between(std::string const& s, char const* head, char const* tail)
{
    size_t a = s.find(head);
    if (a == std::string::npos)
        return std::string();
    size_t b = s.find(tail, a);
    return b == std::string::npos ? std::string() : s.substr(a, b - a);
}

} // namespace

int main()
{
    std::string const src = Source();
    Check("source readable", !src.empty());

    // From the read-back that proves the purchase to the end of the errand.
    std::string const tail = Between(
        src, "if (!succeeded)", "ClearLearnAim(name);\n        return true;\n    }\n\n    // Buy the ONE");
    Check("rank purchase tail found", !tail.empty());

    Check("the rank event is keyed by the rank spell",
          tail.find("RecordEvent(bot, \"learn\", spellId,") != std::string::npos);
    Check("the rank event is not keyed by the trade",
          tail.find("RecordEvent(bot, \"learn\", skill, skillName,\n"
                    "                    alreadyHasSkill") == std::string::npos);

    // The recipes bought with the rank keep the trade as their subject, which
    // is a different key from the rank spell, so neither overwrites the other.
    Check("the recipes bought with the rank still record against the trade",
          tail.find("RecordEvent(bot, \"learn\", skill, skillName,\n"
                    "                        \"bought the recipes") != std::string::npos);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
