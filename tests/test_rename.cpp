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

using OverseerDecisions::IsRenameRow;
using OverseerDecisions::ParseRenameRequest;
using OverseerDecisions::RenameFacts;
using OverseerDecisions::RenameRefusal;
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
    WhatMovesWithIt();
    if (failures)
        return 1;
    std::printf("ok test_rename\n");
    return 0;
}
