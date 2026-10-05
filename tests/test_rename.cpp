/*
 * A rename is one verb: the core's rename and the move of every name-keyed
 * row this module owns (2026-10-05, the operator names the raiders).
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::IsRenameRow;
using OverseerDecisions::ParseRenameRequest;
using OverseerDecisions::RenameFacts;
using OverseerDecisions::RenamePlan;
using OverseerDecisions::RenamePlanFor;
using OverseerDecisions::RenameRefusal;
using OverseerDecisions::RenameStep;
using OverseerDecisions::RenameSteps;
using OverseerDecisions::RenameTables;

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

void TheRow()
{
    Check("rename-to is a rename row", IsRenameRow("rename-to Bonk"));
    Check("a job mode is not", !IsRenameRow("quest"));
    auto const r = ParseRenameRequest("  rename-to   Hoofhearted ");
    Check("the name is read", r.ok && r.newName == "Hoofhearted");
    Check("case is kept", ParseRenameRequest("rename-to McCoom").newName == "McCoom");
    Check("no name is refused", !ParseRenameRequest("rename-to").ok);
    Check("two names are refused", !ParseRenameRequest("rename-to Bonk Crag").ok);
}

RenameFacts Fine()
{
    RenameFacts f;
    f.nameValid = true;
    return f;
}

void WhenItIsRefused()
{
    Check("a free, valid name out of danger goes ahead", RenameRefusal(Fine()) == nullptr);
    RenameFacts f = Fine();
    f.inDungeonRun = true;
    Check("not on a dungeon run", RenameRefusal(f) != nullptr);
    f = Fine();
    f.inInstance = true;
    Check("not inside an instance", RenameRefusal(f) != nullptr);
    f = Fine();
    f.inCombat = true;
    Check("not in a fight", RenameRefusal(f) != nullptr);
    f = Fine();
    f.nameTaken = true;
    Check("not to a taken name", RenameRefusal(f) != nullptr);
    f = Fine();
    f.nameValid = false;
    Check("not to a name the core refuses", RenameRefusal(f) != nullptr);
    f = Fine();
    f.sameName = true;
    Check("not to the same name", RenameRefusal(f) != nullptr);
}

// Where a step sits in a plan, or -1 when the plan never takes it.
int At(RenamePlan plan, RenameStep step)
{
    std::vector<RenameStep> const& steps = RenameSteps(plan);
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (steps[i] == step)
            return int(i);
    return -1;
}

void HowItIsCarriedOut()
{
    // The plan, from the facts.
    Check("an online bot is logged out and renamed offline",
          RenamePlanFor(false, true, true) == RenamePlan::EvictThenRename);
    Check("a real client takes the core's online path",
          RenamePlanFor(false, false, true) == RenamePlan::RenameThenKick);
    Check("a refusal stands for a bot", RenamePlanFor(true, true, true) == RenamePlan::Refuse);
    Check("a refusal stands for a client", RenamePlanFor(true, false, true) == RenamePlan::Refuse);
    Check("a bot logging in or out is not touched",
          RenamePlanFor(false, true, false) == RenamePlan::Refuse);
    Check("a refused rename does nothing at all", RenameSteps(RenamePlan::Refuse).empty());

    // THE BOT: out of the world before its name changes (2026-10-05, ten
    // online guild bots renamed in place). A live Player is never renamed.
    RenamePlan const bot = RenamePlan::EvictThenRename;
    Check("a bot is logged out first", At(bot, RenameStep::LogOut) == 0);
    Check("a bot is never renamed while live", At(bot, RenameStep::RenameLive) == -1);
    Check("its name is written after the logout",
          At(bot, RenameStep::WriteName) > At(bot, RenameStep::LogOut));
    Check("its rows move after the name",
          At(bot, RenameStep::MoveRows) > At(bot, RenameStep::WriteName));
    Check("it is queued back in last",
          At(bot, RenameStep::LogIn) == int(RenameSteps(bot).size()) - 1);
    Check("a bot has no client to kick", At(bot, RenameStep::Kick) == -1);

    // THE CLIENT: the in-place name comes before the kick, because the logout
    // save the kick causes writes whatever name the Player carries.
    RenamePlan const client = RenamePlan::RenameThenKick;
    Check("a client is renamed before it is kicked",
          At(client, RenameStep::RenameLive) >= 0 &&
              At(client, RenameStep::RenameLive) < At(client, RenameStep::Kick));
    Check("a client's rows move before the kick",
          At(client, RenameStep::MoveRows) >= 0 &&
              At(client, RenameStep::MoveRows) < At(client, RenameStep::Kick));
    Check("a client is not logged out as a bot", At(client, RenameStep::LogOut) == -1 &&
                                                     At(client, RenameStep::LogIn) == -1);
}

bool Moves(char const* table)
{
    for (auto const& t : RenameTables())
        if (!std::strcmp(t.table, table))
            return true;
    return false;
}

void WhatMovesWithIt()
{
    // The rows that make a guild member who it is: its job, family and
    // professions (roster), its raid seat, its kept items, goals and trades.
    Check("the roster moves", Moves("overseer_roster"));
    Check("the raid spec moves", Moves("overseer_raid_spec"));
    Check("the raid seat moves", Moves("overseer_raid_seat"));
    Check("the keep list moves", Moves("overseer_keep"));
    Check("goals move", Moves("overseer_goal"));
    Check("trades move", Moves("overseer_trade"));
    Check("history stays as it was", !Moves("overseer_death") && !Moves("overseer_event"));
}

}  // namespace

int main()
{
    TheRow();
    WhenItIsRefused();
    HowItIsCarriedOut();
    WhatMovesWithIt();
    if (failures)
        return 1;
    std::printf("ok test_rename\n");
    return 0;
}
