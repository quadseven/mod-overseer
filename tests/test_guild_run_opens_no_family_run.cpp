/*
 * A member lent to a guild run opens no family dungeon run (#1092).
 *
 * Measured 2026-10-23 to 2026-10-27: seven family dungeon runs stayed active
 * for 68 to 136 minutes with an empty member list, each opened seconds after
 * the same healer left its family's party for a guild finder run. The arming
 * drive saw a roster character on an instance map and opened a family run for
 * it, the member's presence in the guild clear was that run's heartbeat, and
 * the run ended 'emptied' only when the member left. The decision below is
 * what the arming drive asks; the second half pins the wiring as text, because
 * a decision nothing asks is the failure this repository has shipped before.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::MayOpenRunFor;

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

void AMemberLentToAGuildRunOpensNoRun()
{
    Check("a member lent to a guild run may not open a family run",
          !MayOpenRunFor(false, true));
    Check("nor one lent and sitting out", !MayOpenRunFor(true, true));
    Check("a member in its family's party still may", MayOpenRunFor(false, false));
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void TheArmingDriveAsksTheGuildRunsFirst()
{
    std::ifstream source("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable", static_cast<bool>(source));
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const drive = Body(text, "void DriveDungeonClear()");
    Check("DriveDungeonClear is found", !drive.empty());
    std::size_t const lent = drive.find("NamedByAGuildRun(name)");
    std::size_t const asks = drive.find("OverseerDecisions::MayOpenRunFor(");
    std::size_t const opens = drive.find("OpenOrTouchRun(");
    Check("the arming drive asks whether the member is lent", lent != std::string::npos);
    Check("and asks it before it opens a run", lent != std::string::npos &&
                                                   opens != std::string::npos && lent < opens);
    Check("MayOpenRunFor is told about the guild run",
          asks != std::string::npos &&
              drive.find("lentToAGuildRun)", asks) != std::string::npos &&
              drive.find("lentToAGuildRun)", asks) < opens);

    // The lent branch leaves the dungeon brain to the guild run: it neither
    // stands it down nor closes a run as a sit-out.
    std::size_t const branch = drive.find("if (lentToAGuildRun)");
    Check("the lent member has its own branch", branch != std::string::npos);
    std::size_t const branchEnd = drive.find("continue;", branch);
    Check("which skips the member", branchEnd != std::string::npos && branchEnd < opens);
    std::string const lentBranch =
        branch == std::string::npos ? std::string() : drive.substr(branch, branchEnd - branch);
    Check("without standing the brain down",
          lentBranch.find("StandDownDungeonBrain(") == std::string::npos);
    Check("and without closing a run as a sit-out",
          lentBranch.find("EndRunLedByMemberSittingOut(") == std::string::npos);

    std::string const leave = Body(text, "void DriveSittingOutLeave()");
    Check("DriveSittingOutLeave is found", !leave.empty());
    std::size_t const leaveLent = leave.find("NamedByAGuildRun(standin.outName)");
    std::size_t const escort = leave.find("EscortToward(");
    Check("the sit-out walk leaves a member lent to a guild run alone",
          leaveLent != std::string::npos && escort != std::string::npos && leaveLent < escort);
}

}  // namespace

int main()
{
    AMemberLentToAGuildRunOpensNoRun();
    TheArmingDriveAsksTheGuildRunsFirst();
    if (failures)
        return 1;
    std::printf("ok test_guild_run_opens_no_family_run\n");
    return 0;
}
