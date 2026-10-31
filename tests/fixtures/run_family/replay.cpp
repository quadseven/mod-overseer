// Actual adapter SQL runs on SQLite. Only database syntax and fixture roster
// lookup are substituted. The separate MySQL migration needs its own replay.
#include <sqlite3.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int64 = std::int64_t;
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)

struct Field
{
    std::string value;
    template<class T> T Get() const { return static_cast<T>(std::stoull(value)); }
};
struct Result
{
    std::vector<Field> fields;
    Field* Fetch() { return fields.data(); }
};
using QueryResult = std::shared_ptr<Result>;
sqlite3* database = nullptr;
bool familyColumns = true;

void replace(std::string& text, std::string const& before, std::string const& after)
{
    auto pos = text.find(before);
    if (pos != std::string::npos)
        text.replace(pos, before.size(), after);
}

void check(bool condition, char const* reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}

struct Database
{
    template<class T> static void format(std::string& sql, T const& arg)
    {
        std::ostringstream value;
        value << arg;
        replace(sql, "{}", value.str());
    }
    template<class... Args> QueryResult Query(std::string sql, Args const&... args)
    {
        (format(sql, args), ...);
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
            throw std::runtime_error(sql + ": " + sqlite3_errmsg(database));
        QueryResult result;
        int status = sqlite3_step(statement);
        if (status == SQLITE_ROW)
        {
            result = std::make_shared<Result>();
            for (int i = 0; i < sqlite3_column_count(statement); ++i)
            {
                auto value = sqlite3_column_text(statement, i);
                result->fields.push_back(Field{value ? reinterpret_cast<char const*>(value) : "0"});
            }
        }
        else if (status != SQLITE_DONE)
        {
            std::string error = sqlite3_errmsg(database);
            sqlite3_finalize(statement);
            throw std::runtime_error(sql + ": " + error);
        }
        sqlite3_finalize(statement);
        return result;
    }
    template<class... Args> void Execute(std::string sql, Args const&... args)
    {
        replace(sql, "ON DUPLICATE KEY UPDATE", "ON CONFLICT DO UPDATE SET");
        replace(sql, "NOW()", "CURRENT_TIMESTAMP");
        Query(sql, args...);
    }
} CharacterDatabase;

std::string Esc(std::string const& value) { return value; } // fixture names are alphanumeric

struct Runtime
{
    static std::string FamilyOfCharacter(std::string const& member)
    {
        return member == "LeaderA" || member == "MemberA" ? "FamilyA" : "FamilyB";
    }
    static bool SchemaHasColumns(char const*, char const*, uint64) { return familyColumns; }
    // RUNTIME_BODIES
    static uint32 lookup(uint32 map, std::string const& family) { return FAMILY_LOOKUP; }
    static bool owns(uint32 map, std::string const& member) { return MEMBER_LOOKUP; }
};

int main(int argc, char** argv)
{
    try
    {
        bool const legacy = argc > 1 && std::string(argv[1]) == "--legacy";
        familyColumns = !legacy;
        check(sqlite3_open(":memory:", &database) == SQLITE_OK, "SQLite open failed");
        CharacterDatabase.Execute(
            "CREATE TABLE overseer_roster(name TEXT PRIMARY KEY,family TEXT,enabled INTEGER)");
        CharacterDatabase.Execute(
            "INSERT INTO overseer_roster VALUES ('LeaderA','FamilyA',1),('MemberA','FamilyA',1),"
            "('LeaderB','FamilyB',1)");
        CharacterDatabase.Execute(
            "CREATE TABLE overseer_dungeon_run(id INTEGER PRIMARY KEY,"
            "family TEXT NOT NULL DEFAULT '',leader_name TEXT,map_id INTEGER,state TEXT,"
            "started_at TEXT DEFAULT CURRENT_TIMESTAMP,last_progress_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "active_map INTEGER GENERATED ALWAYS AS (CASE WHEN state='active' THEN map_id ELSE NULL END) STORED)");
        CharacterDatabase.Execute(SCOPED_SCHEMA && familyColumns
            ? "CREATE UNIQUE INDEX active_run ON overseer_dungeon_run(family,active_map)"
            : "CREATE UNIQUE INDEX active_run ON overseer_dungeon_run(active_map)");
        auto first = Runtime::OpenOrTouchRun("LeaderA", 129);
        if (legacy)
        {
            check(first && Runtime::OpenOrTouchRun("MemberA", 129) == first,
                  "old schema no longer preserves the single-family run");
            check(Runtime::lookup(129, "FamilyA") == first && Runtime::owns(129, "LeaderA"),
                  "old schema lookup names the missing family column");
            sqlite3_close(database);
            std::cout << "Legacy run schema replay passed\n";
            return 0;
        }
        auto second = Runtime::OpenOrTouchRun("LeaderB", 129);
        check(first && second && first != second,
              "independent families on one dungeon map shared a run row");
        check(Runtime::OpenOrTouchRun("MemberA", 129) == first,
              "another member of the same family created a rival run");
        check(Runtime::lookup(129, "FamilyA") == first && Runtime::lookup(129, "FamilyB") == second,
              "adoption selected a different family's run");
        check(!Runtime::owns(129, "Unknown"), "a nonmember acquired a family's run");
        auto otherMap = Runtime::OpenOrTouchRun("LeaderB", 70);
        check(otherMap != second && Runtime::lookup(70, "FamilyA") == 0,
              "lookup ignored the selected dungeon map");
        CharacterDatabase.Execute("UPDATE overseer_dungeon_run SET state='ended' WHERE id={}", first);
        check(!Runtime::owns(129, "LeaderA") && Runtime::owns(129, "LeaderB"),
              "one family's run kept ownership of another family's members");
        auto next = Runtime::OpenOrTouchRun("MemberA", 129);
        check(next != first && next != second && Runtime::lookup(129, "FamilyB") == second,
              "repeating a run replaced another family's active run");
        sqlite3_close(database);
        std::cout << "Family run isolation replay passed\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        if (database)
            sqlite3_close(database);
        return 1;
    }
}
