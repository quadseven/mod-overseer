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
    bool notNull = false;
    bool defaultBot = false;
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

// A file's SQL with "--" comments removed and whitespace runs collapsed to one
// space, so a statement split over several lines reads the same as one line.
std::string Flatten(std::string const& text)
{
    std::istringstream lines(text);
    std::string line;
    std::string joined;
    while (std::getline(lines, line))
    {
        std::size_t const c = line.find("--");
        if (c != std::string::npos)
            line.erase(c);
        joined += line + " ";
    }
    std::string out;
    bool space = false;
    for (char ch : joined)
    {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
        {
            space = true;
            continue;
        }
        if (space && !out.empty())
            out += ' ';
        space = false;
        out += ch;
    }
    return out;
}

// Every ENUM list a migration gives overseer_command.kind, oldest file first.
// Any statement that adds, modifies or changes the `kind` column but yields no
// parsed list is recorded in `unparsed` (file name), never skipped silently.
std::vector<KindList> KindLists(std::vector<std::string>& unparsed)
{
    std::vector<std::filesystem::path> files;
    for (auto const& e : std::filesystem::recursive_directory_iterator("data/sql"))
        if (e.path().extension() == ".sql")
            files.push_back(e.path());
    std::sort(files.begin(), files.end(),
              [](auto const& a, auto const& b) { return a.filename() < b.filename(); });

    std::vector<KindList> out;
    for (auto const& f : files)
    {
        std::string const sql = Flatten(Read(f));
        std::size_t start = 0;
        while (start < sql.size())
        {
            std::size_t end = sql.find(';', start);
            if (end == std::string::npos)
                end = sql.size();
            std::string const stmt = sql.substr(start, end - start);
            start = end + 1;
            if (stmt.find("ALTER TABLE `overseer_command`") == std::string::npos ||
                (stmt.find("COLUMN `kind`") == std::string::npos &&
                 stmt.find("CHANGE `kind`") == std::string::npos))
                continue;
            std::size_t const col = stmt.find("COLUMN `kind` ENUM(");
            if (col == std::string::npos)
            {
                unparsed.push_back(f.filename().string());
                continue;
            }
            std::size_t const open = stmt.find('(', col);
            std::size_t const close = stmt.find(')', open);
            if (close == std::string::npos)
            {
                unparsed.push_back(f.filename().string());
                continue;
            }
            // The rest of this column's clause: up to the next comma or the end.
            std::size_t comma = stmt.find(',', close);
            std::string const attrs =
                stmt.substr(close + 1, comma == std::string::npos ? std::string::npos
                                                                  : comma - close - 1);
            KindList k;
            k.file = f.filename().string();
            k.values = ParseValues(stmt.substr(open, close - open));
            k.notNull = attrs.find("NOT NULL") != std::string::npos;
            k.defaultBot = attrs.find("DEFAULT 'bot'") != std::string::npos;
            out.push_back(k);
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

// True when `earlier` appears in `later` in the same relative order.
bool KeepsOrder(std::vector<std::string> const& earlier, std::vector<std::string> const& later)
{
    std::size_t at = 0;
    for (auto const& v : earlier)
    {
        auto it = std::find(later.begin() + static_cast<std::ptrdiff_t>(at), later.end(), v);
        if (it == later.end())
            return false;
        at = static_cast<std::size_t>(it - later.begin()) + 1;
    }
    return true;
}

void ThePinnedListIsTwentyTwo()
{
    Check("the pinned list holds 22 existing values", EXISTING.size() == 22);
    Check("the pinned list ends with guild at 20, quest, naturalize",
          EXISTING[19] == "guild" && EXISTING[20] == "quest" && EXISTING[21] == "naturalize");
}

void TheNewestMigrationKeepsEveryVerbAndAddsDisenchant()
{
    std::vector<std::string> unparsed;
    std::vector<KindList> const lists = KindLists(unparsed);
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
    Check("the newest statement keeps NOT NULL", newest.notNull);
    Check("the newest statement keeps DEFAULT 'bot'", newest.defaultBot);
    Check("the newest list carries disenchant", Has(newest.values, "disenchant"));
    Check("the newest list is the 22 in order, then disenchant (23)",
          newest.values.size() == EXISTING.size() + 1 &&
              std::equal(EXISTING.begin(), EXISTING.end(), newest.values.begin()) &&
              newest.values.back() == "disenchant");
}

void NoMigrationInTheChainDropsAVerb()
{
    std::vector<std::string> unparsed;
    std::vector<KindList> const lists = KindLists(unparsed);
    for (auto const& f : unparsed)
    {
        std::printf("  %s alters overseer_command.kind but no ENUM list could be read\n",
                    f.c_str());
        Check("every statement that alters the kind column is readable", false);
    }
    for (auto const& k : lists)
    {
        if (!k.notNull || !k.defaultBot)
            std::printf("  %s: kind statement lacks NOT NULL or DEFAULT 'bot'\n", k.file.c_str());
        Check("every kind statement carries NOT NULL", k.notNull);
        Check("every kind statement carries DEFAULT 'bot'", k.defaultBot);
    }
    for (std::size_t i = 1; i < lists.size(); ++i)
    {
        std::vector<std::string> const gone = Dropped(lists[i - 1].values, lists[i].values);
        if (!gone.empty())
            std::printf("  %s drops %s\n", lists[i].file.c_str(), gone.front().c_str());
        Check("a migration keeps every value of the one before it", gone.empty());
        if (!KeepsOrder(lists[i - 1].values, lists[i].values))
            std::printf("  %s reorders values of the list before it\n", lists[i].file.c_str());
        Check("a migration keeps the earlier values in the same relative order",
              KeepsOrder(lists[i - 1].values, lists[i].values));
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
