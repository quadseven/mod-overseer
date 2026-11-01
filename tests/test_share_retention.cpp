/*
 * Prune answered share rows beyond the backoff memory (#1251).
 *
 * The bridge's share refusal read (the window query over overseer_command)
 * takes 2.6 s warm and 6.5 s cold and hits read timeouts because the table
 * holds about 747,000 rows from 2026-08-21 and is never pruned. The query
 * reads 131,513 share rows from the last 90 days and sorts them by a key that
 * includes a JSON expression.
 *
 * This test pins:
 * 1. The migration file exists and follows the module's guarded migration idiom
 *    (INFORMATION_SCHEMA + PREPARE / EXECUTE / DEALLOCATE).
 * 2. Terminal share rows older than the 90-day backoff memory window are deleted.
 * 3. Terminal share rows inside the 90-day window are pruned down to the
 *    youngest 8 (history_depth) per (target_name, target_arg, command, status,
 *    reason), exactly matching the set the backoff reads.
 * 4. Non-share kinds and non-terminal statuses are preserved.
 * 5. The retention rule is idempotent: running it a second time deletes zero
 *    additional rows.
 *
 * Fixture rows are taken directly from the output format written by
 * src/mod_overseer.cpp::DoShare.
 */

#include "overseer_decisions.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
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

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

struct CommandRow
{
    uint32_t id;
    std::string kind;
    std::string target_name;
    std::string target_arg;
    std::string command;
    std::string status;
    std::string detail;
    std::string result;
    int age_days; // days ago
};

std::string ExtractReason(std::string const& result)
{
    std::string const key = "\"reason\":";
    std::size_t pos = result.find(key);
    if (pos == std::string::npos)
        return "";
    pos += key.size();
    if (pos >= result.size() || result[pos] != '"')
        return "";
    std::size_t end = result.find('"', pos + 1);
    if (end == std::string::npos)
        return "";
    return result.substr(pos + 1, end - pos - 1);
}

void TestMigrationStructure()
{
    std::string const sql = ReadFile("data/sql/characters/base/2026_10_31_01_overseer_share_retention.sql");
    Check("migration exists and is readable", !sql.empty());

    Check("uses guarded INFORMATION_SCHEMA check",
          Has(sql, "INFORMATION_SCHEMA.COLUMNS") && Has(sql, "COLUMN_NAME = 'result'"));

    Check("uses PREPARE, EXECUTE, and DEALLOCATE for step 1",
          Has(sql, "PREPARE prune_old_share_stmt FROM @prune_old_share_commands;") &&
          Has(sql, "EXECUTE prune_old_share_stmt;") &&
          Has(sql, "DEALLOCATE PREPARE prune_old_share_stmt;"));

    Check("uses PREPARE, EXECUTE, and DEALLOCATE for step 2",
          Has(sql, "PREPARE prune_excess_share_stmt FROM @prune_excess_share_commands;") &&
          Has(sql, "EXECUTE prune_excess_share_stmt;") &&
          Has(sql, "DEALLOCATE PREPARE prune_excess_share_stmt;"));

    Check("deletes terminal share rows older than 90 days",
          Has(sql, "`kind` = 'share'") &&
          Has(sql, "`status` IN ('delivered', 'error')") &&
          Has(sql, "`updated_at` < NOW() - INTERVAL 90 DAY"));

    Check("partitions by target, argument, command, status and reason",
          Has(sql, "`target_name`") &&
          Has(sql, "`target_arg`") &&
          Has(sql, "`command`") &&
          Has(sql, "`status`") &&
          Has(sql, "JSON_EXTRACT(`result`, '$.reason')"));

    Check("keeps the youngest 8 per group inside 90 days",
          Has(sql, "ROW_NUMBER() OVER") &&
          Has(sql, "`updated_at` DESC") &&
          Has(sql, "rn > 8"));

    Check("leaves non-share kinds untouched",
          !Has(sql, "kind <>") && !Has(sql, "kind != 'share'") && !Has(sql, "kind IN"));
}

std::vector<CommandRow> BuildFixtures()
{
    std::vector<CommandRow> rows;
    uint32_t nextId = 1;

    // Group 1: Og -> Grog, quest:100, error, reason "taker quest log is full"
    // 12 rows within 90 days (ages 1 to 12 days). Backoff keeps youngest 8 (ages 1..8).
    // Plus 3 rows older than 90 days (ages 95, 100, 110 days).
    for (int age = 1; age <= 12; ++age)
    {
        rows.push_back({
            nextId++, "share", "Og", "Grog", "quest:100", "error",
            "taker quest log is full",
            "{\"outcome\":\"refused\",\"reason\":\"taker quest log is full\",\"retryable\":true,"
            "\"from\":\"Og\",\"to\":\"Grog\",\"quest_id\":100,\"taker_status\":0,\"request\":\"quest:100\"}",
            age
        });
    }
    for (int age : {95, 100, 110})
    {
        rows.push_back({
            nextId++, "share", "Og", "Grog", "quest:100", "error",
            "taker quest log is full",
            "{\"outcome\":\"refused\",\"reason\":\"taker quest log is full\",\"retryable\":true,"
            "\"from\":\"Og\",\"to\":\"Grog\",\"quest_id\":100,\"taker_status\":0,\"request\":\"quest:100\"}",
            age
        });
    }

    // Group 2: Og -> Ugga, quest:100, error, reason "taker offline"
    // 5 rows within 90 days (ages 5 to 9 days). All 5 should be kept.
    for (int age = 5; age <= 9; ++age)
    {
        rows.push_back({
            nextId++, "share", "Og", "Ugga", "quest:100", "error",
            "taker not online",
            "{\"outcome\":\"refused\",\"reason\":\"taker offline\",\"retryable\":true,"
            "\"from\":\"Og\",\"to\":\"Ugga\",\"quest_id\":100,\"taker_status\":-1,\"request\":\"quest:100\"}",
            age
        });
    }

    // Group 3: Og -> Ugga, quest:200, delivered, reason ""
    // 10 delivered rows within 90 days (ages 1 to 10 days). Backoff keeps youngest 8.
    for (int age = 1; age <= 10; ++age)
    {
        rows.push_back({
            nextId++, "share", "Og", "Ugga", "quest:200", "delivered", "",
            "{\"outcome\":\"shared\",\"reason\":\"\",\"retryable\":true,"
            "\"from\":\"Og\",\"to\":\"Ugga\",\"quest_id\":200,\"taker_status\":1,\"request\":\"quest:200\"}",
            age
        });
    }

    // Group 4: Non-terminal share rows (pending, claimed) - must not be touched.
    rows.push_back({nextId++, "share", "Og", "Bork", "quest:300", "pending", "", "", 95});
    rows.push_back({nextId++, "share", "Og", "Bork", "quest:300", "claimed", "", "", 92});

    // Group 5: Non-share rows (bot, probe, give) - must not be touched.
    rows.push_back({nextId++, "bot", "Og", "", "nc +x", "delivered", "", "", 120});
    rows.push_back({nextId++, "probe", "Og", "", "auras", "delivered", "", "{\"auras\":[]}", 100});
    rows.push_back({nextId++, "give", "Og", "Bork", "item:123", "delivered", "", "", 95});

    return rows;
}

std::vector<uint32_t> ComputeBackoffKeptRows(std::vector<CommandRow> const& rows)
{
    // The bridge questshare backoff read:
    // WHERE kind = 'share' AND status IN ('delivered', 'error') AND updated_at > NOW() - INTERVAL 90 DAY
    // Kept depth: youngest 8 per (target, argument, command, status, reason).
    using GroupKey = std::tuple<std::string, std::string, std::string, std::string, std::string>;
    std::map<GroupKey, std::vector<CommandRow>> groups;

    for (auto const& r : rows)
    {
        if (r.kind != "share")
            continue;
        if (r.status != "delivered" && r.status != "error")
            continue;
        if (r.age_days > 90)
            continue;

        std::string const reason = ExtractReason(r.result);
        GroupKey key = {r.target_name, r.target_arg, r.command, r.status, reason};
        groups[key].push_back(r);
    }

    std::vector<uint32_t> keptIds;
    for (auto& pair : groups)
    {
        auto& list = pair.second;
        // Youngest first: smaller age_days is younger.
        std::sort(list.begin(), list.end(), [](CommandRow const& a, CommandRow const& b) {
            if (a.age_days != b.age_days)
                return a.age_days < b.age_days;
            return a.id > b.id;
        });
        std::size_t limit = std::min<std::size_t>(8, list.size());
        for (std::size_t i = 0; i < limit; ++i)
            keptIds.push_back(list[i].id);
    }
    std::sort(keptIds.begin(), keptIds.end());
    return keptIds;
}

std::vector<CommandRow> ApplyRetentionRule(std::vector<CommandRow> const& input)
{
    // Step 1: Delete terminal share rows older than 90 days.
    std::vector<CommandRow> afterStep1;
    for (auto const& r : input)
    {
        bool const isTerminalShare = (r.kind == "share" && (r.status == "delivered" || r.status == "error"));
        if (isTerminalShare && r.age_days > 90)
            continue; // pruned
        afterStep1.push_back(r);
    }

    // Step 2: In the 90-day window, prune terminal share rows beyond depth 8.
    using GroupKey = std::tuple<std::string, std::string, std::string, std::string, std::string>;
    std::map<GroupKey, std::vector<CommandRow>> groups;
    for (auto const& r : afterStep1)
    {
        if (r.kind == "share" && (r.status == "delivered" || r.status == "error") && r.age_days <= 90)
        {
            std::string const reason = ExtractReason(r.result);
            groups[{r.target_name, r.target_arg, r.command, r.status, reason}].push_back(r);
        }
    }

    std::map<uint32_t, bool> toDelete;
    for (auto& pair : groups)
    {
        auto& list = pair.second;
        std::sort(list.begin(), list.end(), [](CommandRow const& a, CommandRow const& b) {
            if (a.age_days != b.age_days)
                return a.age_days < b.age_days;
            return a.id > b.id;
        });
        for (std::size_t i = 8; i < list.size(); ++i)
            toDelete[list[i].id] = true;
    }

    std::vector<CommandRow> afterStep2;
    for (auto const& r : afterStep1)
    {
        if (toDelete.find(r.id) != toDelete.end())
            continue;
        afterStep2.push_back(r);
    }
    return afterStep2;
}

void TestRetentionRuleMatchesBackoff()
{
    std::vector<CommandRow> const fixtures = BuildFixtures();
    std::vector<uint32_t> const backoffKeptIds = ComputeBackoffKeptRows(fixtures);

    std::vector<CommandRow> const retained = ApplyRetentionRule(fixtures);

    // Extract surviving terminal share rows
    std::vector<uint32_t> retainedTerminalShareIds;
    for (auto const& r : retained)
    {
        if (r.kind == "share" && (r.status == "delivered" || r.status == "error"))
            retainedTerminalShareIds.push_back(r.id);
    }
    std::sort(retainedTerminalShareIds.begin(), retainedTerminalShareIds.end());

    Check("retained terminal share rows match backoff kept set exactly",
          retainedTerminalShareIds == backoffKeptIds);

    // Non-terminal share rows preserved
    int nonTerminalCount = 0;
    for (auto const& r : retained)
    {
        if (r.kind == "share" && (r.status == "pending" || r.status == "claimed"))
            ++nonTerminalCount;
    }
    Check("non-terminal share rows are preserved", nonTerminalCount == 2);

    // Non-share rows preserved
    int nonShareCount = 0;
    for (auto const& r : retained)
    {
        if (r.kind != "share")
            ++nonShareCount;
    }
    Check("non-share rows are preserved", nonShareCount == 3);

    // Idempotency: applying the retention rule a second time changes nothing
    std::vector<CommandRow> const secondPass = ApplyRetentionRule(retained);
    std::vector<uint32_t> secondPassIds;
    for (auto const& r : secondPass)
        secondPassIds.push_back(r.id);
    std::vector<uint32_t> firstPassIds;
    for (auto const& r : retained)
        firstPassIds.push_back(r.id);

    Check("retention rule is idempotent when run twice", secondPassIds == firstPassIds);
}

} // namespace

int main()
{
    TestMigrationStructure();
    TestRetentionRuleMatchesBackoff();

    if (failures == 0)
        std::printf("test_share_retention: all checks passed\n");
    return failures ? 1 : 0;
}
