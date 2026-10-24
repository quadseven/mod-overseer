/*
 * Resolving a name typed in a Discord order (mod-overseer#150).
 *
 * The bridge turns "run Deadmines until <item> drops" into a campaign row, but
 * only after the name in the sentence has become exactly one id. This pins the
 * rule it applies to the rows `item_template`, the map table or the roster
 * returned for that name: one match is an id, anything else is a question and
 * no row. Also pins the reply, which must echo the ids, and the audit table's
 * migration, which makes every accepted order queryable.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::FormatOrderReply;
using OverseerDecisions::OrderCandidate;
using OverseerDecisions::OrderResolution;
using OverseerDecisions::ResolveOrderName;

namespace
{

int failures = 0;

void CheckStr(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckNum(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void OneMatchIsAnId()
{
    OrderResolution r = ResolveOrderName("item", "Cruel Barb", {{2873, "Cruel Barb"}});
    CheckNum("one match writes", r.accepted, 1);
    CheckNum("one match id", r.id, 2873);
    CheckStr("one match asks nothing", r.question, "");
}

void AnUnknownNameAsksAndWritesNothing()
{
    OrderResolution r = ResolveOrderName("item", "Sword of Nothing", {});
    CheckNum("unknown writes nothing", r.accepted, 0);
    CheckNum("unknown id", r.id, 0);
    CheckStr("unknown question", r.question,
             "I know no item called 'Sword of Nothing'. Which did you mean?");
}

void TwoMatchesAskWhichAndNameBothIds()
{
    OrderResolution r = ResolveOrderName(
        "item", "Linen Cloth", {{2589, "Linen Cloth"}, {2590, "Linen Cloth"}});
    CheckNum("ambiguous writes nothing", r.accepted, 0);
    CheckStr("ambiguous question", r.question,
             "More than one item is called 'Linen Cloth': 2589, 2590. Which one?");
}

void AnExactNameBeatsAPartialOne()
{
    OrderResolution r = ResolveOrderName(
        "map", "deadmines", {{36, "The Deadmines"}, {1000, "Deadmines Raid"}});
    CheckNum("two partial matches stay ambiguous", r.accepted, 0);

    r = ResolveOrderName("map", "the deadmines",
                         {{36, "The Deadmines"}, {1000, "The Deadmines Raid"}});
    CheckNum("exact (case-blind) wins", r.accepted, 1);
    CheckNum("exact id", r.id, 36);
}

void TheReplyEchoesEveryResolvedId()
{
    std::string reply = FormatOrderReply(
        77, {{"map", "36"}, {"item", "2873"}, {"member", "Bork"}});
    CheckStr("reply", reply, "Order 77 written: map 36, item 2873, member Bork.");
}

void TheAuditMigrationRecordsWhoWhatAndRows()
{
    std::ifstream in("data/sql/characters/base/2026_10_24_00_overseer_order.sql");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string sql = ss.str();
    for (char const* col : {"`author`", "`text`", "`rows_written`", "`bridge`"})
    {
        if (sql.find(col) == std::string::npos)
        {
            std::printf("FAIL migration lacks %s\n", col);
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    OneMatchIsAnId();
    AnUnknownNameAsksAndWritesNothing();
    TwoMatchesAskWhichAndNameBothIds();
    AnExactNameBeatsAPartialOne();
    TheReplyEchoesEveryResolvedId();
    TheAuditMigrationRecordsWhoWhatAndRows();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
