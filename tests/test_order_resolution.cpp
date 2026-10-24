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
using OverseerDecisions::FormatRowsWritten;
using OverseerDecisions::OrderCall;
using OverseerDecisions::OrderLookups;
using OverseerDecisions::OrderPlan;
using OverseerDecisions::PlanOrder;
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

void ALonePartialMatchIsAnIdButNoMatchIsNot()
{
    // "run Deadmines" must find the map named "The Deadmines".
    OrderResolution r = ResolveOrderName("map", "deadmines", {{36, "The Deadmines"}});
    CheckNum("lone partial writes", r.accepted, 1);
    CheckNum("lone partial id", r.id, 36);

    r = ResolveOrderName("item", "Sword of Nothing", {});
    CheckNum("no match still writes nothing", r.accepted, 0);
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
    if (!in.is_open())
    {
        std::printf("FAIL migration file cannot be opened\n");
        ++failures;
        return;
    }
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


std::string Cell(OrderPlan const& plan, size_t row, std::string const& col)
{
    if (row >= plan.rows.size())
        return "<no row>";
    for (auto const& kv : plan.rows[row].columns)
        if (kv.first == col)
            return kv.second;
    return "<no column>";
}

void RunDeadminesUntilItemDropsWritesOneCampaignRow()
{
    OrderCall call{"start_campaign", {{"map", "Deadmines"}, {"goal_kind", "item"},
                                      {"item", "Cruel Barb"}, {"runs", "30"}}};
    OrderLookups look;
    look.maps = {{36, "The Deadmines"}};
    look.items = {{2873, "Cruel Barb"}};
    OrderPlan plan = PlanOrder(call, look);
    CheckNum("campaign accepted", plan.accepted, 1);
    CheckNum("campaign writes one row", plan.rows.size(), 1);
    CheckStr("campaign table", plan.rows[0].table, "overseer_dungeon_campaign");
    CheckStr("campaign map", Cell(plan, 0, "map"), "36");
    CheckStr("campaign goal kind", Cell(plan, 0, "goal_kind"), "item");
    CheckStr("campaign item", Cell(plan, 0, "goal_item"), "2873");
    CheckStr("campaign runs", Cell(plan, 0, "runs_wanted"), "30");
    CheckStr("reply echoes map and item ids",
             FormatOrderReply(5, plan.resolved), "Order 5 written: map 36, item 2873.");
}

void FarmWritesAnAim()
{
    OrderLookups look;
    look.items = {{2589, "Linen Cloth"}};
    OrderPlan plan = PlanOrder({"farm_target", {{"item", "Linen Cloth"}}}, look);
    CheckNum("farm accepted", plan.accepted, 1);
    CheckNum("farm writes one row", plan.rows.size(), 1);
    CheckStr("farm table", plan.rows[0].table, "overseer_aim");
    CheckStr("farm item", Cell(plan, 0, "item"), "2589");
}

void GoLevelWritesALevelGoalForTheMember()
{
    OrderLookups look;
    look.members = {{4101, "Bork"}};
    OrderPlan plan = PlanOrder({"set_level_target", {{"member", "Bork"}, {"level", "40"}}}, look);
    CheckNum("level accepted", plan.accepted, 1);
    CheckStr("level table", plan.rows[0].table, "overseer_level_goal");
    CheckStr("level member", Cell(plan, 0, "member"), "Bork");
    CheckStr("level target", Cell(plan, 0, "level"), "40");
    CheckStr("level reply", FormatOrderReply(6, plan.resolved), "Order 6 written: member Bork, level 40.");
}

void AnUnknownItemAsksAndWritesNoRow()
{
    OrderCall call{"start_campaign", {{"map", "Deadmines"}, {"goal_kind", "item"},
                                      {"item", "Sword of Nothing"}, {"runs", "30"}}};
    OrderLookups look;
    look.maps = {{36, "The Deadmines"}};
    OrderPlan plan = PlanOrder(call, look);
    CheckNum("unknown item not accepted", plan.accepted, 0);
    CheckNum("unknown item writes no row", plan.rows.size(), 0);
    CheckStr("unknown item question", plan.question,
             "I know no item called 'Sword of Nothing'. Which did you mean?");

    look.members = {};
    plan = PlanOrder({"set_level_target", {{"member", "Nobody"}, {"level", "40"}}}, look);
    CheckNum("unknown member writes no row", plan.rows.size(), 0);
    plan = PlanOrder({"bake_cake", {}}, look);
    CheckNum("unknown tool not accepted", plan.accepted, 0);
}

void TheRowsWrittenColumnNamesEveryRow()
{
    OrderLookups look;
    look.maps = {{36, "The Deadmines"}};
    look.items = {{2873, "Cruel Barb"}};
    OrderPlan plan = PlanOrder({"start_campaign", {{"map", "Deadmines"}, {"goal_kind", "item"},
                                                   {"item", "Cruel Barb"}, {"runs", "30"}}}, look);
    CheckStr("rows_written", FormatRowsWritten(plan.rows),
             "[{\"table\":\"overseer_dungeon_campaign\",\"key\":\"map=36,goal_item=2873\"}]");
}

}  // namespace

int main()
{
    OneMatchIsAnId();
    AnUnknownNameAsksAndWritesNothing();
    TwoMatchesAskWhichAndNameBothIds();
    AnExactNameBeatsAPartialOne();
    ALonePartialMatchIsAnIdButNoMatchIsNot();
    TheReplyEchoesEveryResolvedId();
    TheAuditMigrationRecordsWhoWhatAndRows();
    RunDeadminesUntilItemDropsWritesOneCampaignRow();
    FarmWritesAnAim();
    GoLevelWritesALevelGoalForTheMember();
    AnUnknownItemAsksAndWritesNoRow();
    TheRowsWrittenColumnNamesEveryRow();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
