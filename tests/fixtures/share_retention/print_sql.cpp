// Prints the shipped retention statements, so a test or a dry run reads the
// real text. Arguments: select, count, or delete followed by ids.
#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::string const mode = argc > 1 ? argv[1] : "";
    std::string sql;
    if (mode == "select")
        sql = OverseerDecisions::ShareRetentionSelectSql();
    else if (mode == "count")
        sql = OverseerDecisions::ShareRetentionCountSql();
    else if (mode == "delete")
    {
        std::vector<uint32_t> ids;
        for (int i = 2; i < argc; ++i)
            ids.push_back(static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10)));
        sql = OverseerDecisions::ShareRetentionDeleteSql(ids);
    }
    else
        return 2;
    std::printf("%s\n", sql.c_str());
    return 0;
}
