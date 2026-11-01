/*
 * The share retention is off by default and bounded (#1251). The rows it keeps
 * are proved on MySQL by tests/replay_share_retention.py; this pins the parts
 * that need no database. Reads src/mod_overseer.cpp as text (run from the repo
 * root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

static int failures = 0;

static void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream source;
    source << file.rdbuf();
    Check(!source.str().empty(), "the module source was read");
    Check(source.str().find(
              "GetOption<bool>(\"Overseer.ShareRetention.Enable\", false)") !=
              std::string::npos,
          "the sweep is off unless the realm turns it on");

    std::string const select = ShareRetentionSelectSql();
    Check(select.find("LIMIT 5000") != std::string::npos, "the select is bounded");
    Check(select.find("kind = 'share'") != std::string::npos, "the select reads shares only");
    Check(ShareRetentionCountSql().find("LIMIT") == std::string::npos,
          "the count is of every candidate");

    Check(ShareRetentionDeleteSql({}).empty(), "no ids, no statement");
    std::string const del = ShareRetentionDeleteSql({7, 9});
    Check(del.find("id IN (7,9)") != std::string::npos, "the delete names its ids");
    Check(del.find("kind = 'share'") != std::string::npos, "the delete rechecks the kind");
    Check(del.find("status IN ('delivered', 'error')") != std::string::npos,
          "the delete rechecks the status");

    if (failures)
        return 1;
    std::printf("share retention: all passed\n");
    return 0;
}
