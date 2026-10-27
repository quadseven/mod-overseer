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

using OverseerDecisions::CountsAsInADungeonRun;
using OverseerDecisions::MayCraftNow;
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

// Body() ends at the first close at the class's four-space indentation. A
// body that walked past its own function's close would hold the next member's
// four-space line (a signature, a comment); one that did not holds none.
bool OneMember(std::string const& body)
{
    std::size_t at = body.find('\n');
    while (at != std::string::npos)
    {
        std::size_t const line = at + 1;
        char const next = line + 4 < body.size() ? body[line + 4] : ' ';
        if (body.compare(line, 4, "    ") == 0 && next != ' ' && next != '\n' &&
            next != '{' && next != '}' && next != '#')
            return false;
        at = body.find('\n', line);
    }
    return true;
}

// A guild run writes no family run row, and the empty family run this change
// stops opening was the only thing that stood the other drives down for a lent
// member. So the run-presence check counts the guild run itself.
void ALentMemberInsideAGuildClearCountsAsInARun()
{
    Check("a member named by a guild run, on the dungeon map, is in a run",
          CountsAsInADungeonRun(true, false, true));
    Check("a family run row on the map still counts", CountsAsInADungeonRun(true, true, false));
    Check("on a dungeon map with neither, it is not in a run",
          !CountsAsInADungeonRun(false, false, false) && !CountsAsInADungeonRun(true, false, false));
    Check("named by a guild run but not on a dungeon map, it is not inside one",
          !CountsAsInADungeonRun(false, false, true));
    Check("a lent healer on a campaign job does not craft mid-clear",
          !MayCraftNow("dungeon", CountsAsInADungeonRun(true, false, true), true, false, false));
    Check("nor on a town run job",
          !MayCraftNow("town run", CountsAsInADungeonRun(true, false, true), true, false, false));
}

// Every drive that stood down for the orphan family run row asks the check
// that counts a guild run, not the row-only InDungeonRun.
void TheStandDownDrivesAskTheGuildRunToo(std::string const& text)
{
    std::string const helper = Body(text, "bool InAnyDungeonRun(Player* bot) const");
    Check("InAnyDungeonRun is found", !helper.empty());
    Check("which asks the decision",
          helper.find("OverseerDecisions::CountsAsInADungeonRun(") != std::string::npos);
    Check("with the family run row", helper.find("ActiveRunOnMap(") != std::string::npos);
    Check("and the guild runs", helper.find("NamedByAGuildRun(") != std::string::npos);

    struct Site
    {
        char const* drive;
        char const* asks;
        char const* what;
    };
    Site const sites[] = {
        {"void DriveCraft()", "MayCraftNow(jobIt->second, InAnyDungeonRun(bot),",
         "the craft drive does not craft for a lent member mid-clear"},
        {"void DriveLockpick()", "if (InAnyDungeonRun(bot))",
         "the lockpick errand stands down for a lent member"},
        {"void DriveRecipeManuals()", "|| InAnyDungeonRun(bot))",
         "the recipe-manual errand stands down for a lent member"},
        {"void DriveStuckRevival()", "bool const inRun = InAnyDungeonRun(bot);",
         "the healer-death path treats a lent member as in a run"},
        {"void DriveFamilyQuests(", "if (InAnyDungeonRun(bot))",
         "the quest drive stands down for a lent member"},
        {"void DriveProfessions()", "if (InAnyDungeonRun(bot))",
         "the profession plan stands down for a lent member"},
        {"void DriveCatchUp(", "|| InAnyDungeonRun(p))",
         "the catch-up walk leaves a lent member alone"},
    };
    for (Site const& site : sites)
    {
        std::string const body = Body(text, site.drive);
        Check(site.drive, !body.empty());
        Check("and its body is that drive's alone", OneMember(body));
        Check(site.what, body.find(site.asks) != std::string::npos);
    }
}

void TheArmingDriveAsksTheGuildRunsFirst()
{
    std::ifstream source("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable", static_cast<bool>(source));
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    TheStandDownDrivesAskTheGuildRunToo(text);

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
    Check("and forgets its dc on record, so its family asks afresh later",
          lentBranch.find("_dcOnIssued.erase(name);") != std::string::npos);
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
    ALentMemberInsideAGuildClearCountsAsInARun();
    TheArmingDriveAsksTheGuildRunsFirst();
    if (failures)
        return 1;
    std::printf("ok test_guild_run_opens_no_family_run\n");
    return 0;
}
