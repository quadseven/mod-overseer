/*
 * kind='disenchant' joins overseer_command.kind without dropping a verb
 * (Refs #444).
 *
 * An ALTER that MODIFYs an ENUM replaces the whole list, so a migration that
 * forgets 'cast' or 'guild' deletes those verbs. This test pins the list the
 * newest migration must carry: every value that existed before this change, in
 * order, then 'disenchant'. It also walks every migration that names the
 * column and requires each list to keep every value of the one before it.
 *
 * The ticket said 20 existing values; 'quest' and 'naturalize' landed after
 * 'guild' (the 20th), so the real list before this change is 22. Dropping
 * those two would be the very bug the ticket guards against.
 *
 * Reads data/sql as text (run from the repo root).
 */

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

std::vector<std::string> const EXISTING = {
    "bot", "chat", "gm", "probe", "give", "share", "trade", "job", "sell", "bank",
    "auction", "mail", "repair", "buy", "bind", "hearth", "summon", "conjure", "cast", "guild",
    "quest", "naturalize"};

struct KindList
{
    std::string file;
    std::vector<std::string> values;
};

std::string Read(std::filesystem::path const& p)
{
    std::ifstream in(p);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<std::string> ParseValues(std::string const& inside)
{
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = inside.find('\'', pos)) != std::string::npos)
    {
        std::size_t const end = inside.find('\'', pos + 1);
        if (end == std::string::npos)
            break;
        out.push_back(inside.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return out;
}

// Every ENUM list a migration gives overseer_command.kind, oldest file first.
std::vector<KindList> KindLists()
{
    std::vector<std::filesystem::path> files;
    for (auto const& e : std::filesystem::directory_iterator("data/sql/characters/base"))
        if (e.path().extension() == ".sql")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    std::vector<KindList> out;
    for (auto const& f : files)
    {
        std::string const text = Read(f);
        std::istringstream lines(text);
        std::string line;
        std::string table;
        while (std::getline(lines, line))
        {
            if (line.rfind("--", 0) == 0)
                continue;
            std::size_t const at = line.find("ALTER TABLE `");
            if (at != std::string::npos)
            {
                std::size_t const e = line.find('`', at + 13);
                table = line.substr(at + 13, e - at - 13);
            }
            std::size_t const col = line.find("COLUMN `kind` ENUM(");
            if (col == std::string::npos || table != "overseer_command")
                continue;
            std::size_t const open = line.find('(', col);
            std::size_t const close = line.find(')', open);
            out.push_back({f.filename().string(), ParseValues(line.substr(open, close - open))});
        }
    }
    return out;
}

bool Has(std::vector<std::string> const& list, std::string const& v)
{
    return std::find(list.begin(), list.end(), v) != list.end();
}

// What would be lost going from `before` to `after`.
std::vector<std::string> Dropped(std::vector<std::string> const& before,
                                 std::vector<std::string> const& after)
{
    std::vector<std::string> gone;
    for (auto const& v : before)
        if (!Has(after, v))
            gone.push_back(v);
    return gone;
}

void ThePinnedListIsTwentyTwo()
{
    Check("the pinned list holds 22 existing values", EXISTING.size() == 22);
    Check("the pinned list ends with guild at 20, quest, naturalize",
          EXISTING[19] == "guild" && EXISTING[20] == "quest" && EXISTING[21] == "naturalize");
}

void TheNewestMigrationKeepsEveryVerbAndAddsDisenchant()
{
    std::vector<KindList> const lists = KindLists();
    Check("some migration names the kind column", !lists.empty());
    if (lists.empty())
        return;
    KindList const& newest = lists.back();
    Check("the newest kind migration is the disenchant one",
          newest.file == "2026_10_29_00_overseer_disenchant.sql");

    for (auto const& v : EXISTING)
    {
        if (!Has(newest.values, v))
            std::printf("  dropped: %s\n", v.c_str());
        Check("the newest list keeps an existing value", Has(newest.values, v));
    }
    Check("the newest list carries disenchant", Has(newest.values, "disenchant"));
    Check("the newest list is the 22 in order, then disenchant (23)",
          newest.values.size() == EXISTING.size() + 1 &&
              std::equal(EXISTING.begin(), EXISTING.end(), newest.values.begin()) &&
              newest.values.back() == "disenchant");
}

void NoMigrationInTheChainDropsAVerb()
{
    std::vector<KindList> const lists = KindLists();
    for (std::size_t i = 1; i < lists.size(); ++i)
    {
        std::vector<std::string> const gone = Dropped(lists[i - 1].values, lists[i].values);
        if (!gone.empty())
            std::printf("  %s drops %s\n", lists[i].file.c_str(), gone.front().c_str());
        Check("a migration keeps every value of the one before it", gone.empty());
    }
}

void TheDropCheckCatchesADroppedVerb()
{
    // The detector itself: drop 'cast', then 'guild', from the real list.
    for (char const* victim : {"cast", "guild"})
    {
        std::vector<std::string> mutated = EXISTING;
        mutated.erase(std::find(mutated.begin(), mutated.end(), victim));
        mutated.push_back("disenchant");
        std::vector<std::string> const gone = Dropped(EXISTING, mutated);
        Check("a list that forgets a verb is reported", gone.size() == 1 && gone[0] == victim);
    }
}

}  // namespace

int main()
{
    ThePinnedListIsTwentyTwo();
    TheNewestMigrationKeepsEveryVerbAndAddsDisenchant();
    NoMigrationInTheChainDropsAVerb();
    TheDropCheckCatchesADroppedVerb();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
