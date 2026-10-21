/*
 * A member a stand-in sends to sit out holds no dungeon run (#1092).
 *
 * On 2026-10-20 a member sent off to craft stayed inside the family's dungeon.
 * The arming drive opened a run with it as the solo leader, its presence was
 * that run's only heartbeat, and the run stayed active for twelve hours while
 * the craft it sat out for waited on it leaving. The decisions below are what
 * the arming drive and the coordinator's walk out ask; the second half pins the
 * wiring as text, because a decision nothing asks is the failure this
 * repository has shipped before.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::FamilyStandin;
using OverseerDecisions::MayOpenRunFor;
using OverseerDecisions::SitOutRunEndedReason;
using OverseerDecisions::SitOutRunOutcome;
using OverseerDecisions::SitsOutOfRuns;
using OverseerDecisions::StandinSeat;

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

FamilyStandin Row(std::string const& out)
{
    FamilyStandin row;
    row.family = "Grug";
    row.outName = out;
    row.inName = "Flintt";
    row.seat = StandinSeat::Damage;
    return row;
}

void OnlyTheNamedMemberSitsOut()
{
    FamilyStandin const row = Row("Og");
    Check("the member named out_name sits out", SitsOutOfRuns(&row, "Og"));
    Check("another member does not", !SitsOutOfRuns(&row, "Ugga"));
    Check("the guest does not sit out", !SitsOutOfRuns(&row, "Flintt"));
    Check("the head does not sit out", !SitsOutOfRuns(&row, "Grug"));
    Check("nobody sits out with no row", !SitsOutOfRuns(nullptr, "Og"));
    FamilyStandin const none = Row("");
    Check("an empty row sits nobody out", !SitsOutOfRuns(&none, ""));
    Check("an empty name never sits out", !SitsOutOfRuns(&row, ""));
}

void TheMemberSittingOutOpensNoRun()
{
    Check("a member sitting out may not open a run", !MayOpenRunFor(true));
    Check("a member in the party may", MayOpenRunFor(false));
}

void ARunTheMemberLeadsEndsNamingTheSitOut()
{
    std::string const reason = SitOutRunEndedReason("Og");
    Check("the reason names the sit-out", reason.find("sitting out") != std::string::npos);
    Check("the reason names the leader", reason.find("'Og'") != std::string::npos);
    Check("the reason fits ended_reason (200)", reason.size() <= 200);
    Check("the outcome is a word", std::string(SitOutRunOutcome()) == "sat_out");
    Check("the outcome fits its column (16)", std::string(SitOutRunOutcome()).size() <= 16);
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

void TheArmingDriveAsksBeforeItOpensARun()
{
    std::ifstream source("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable", static_cast<bool>(source));
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::string const drive = Body(text, "void DriveDungeonClear()");
    Check("DriveDungeonClear is found", !drive.empty());
    std::size_t const asks = drive.find("OverseerDecisions::MayOpenRunFor(");
    std::size_t const opens = drive.find("OpenOrTouchRun(");
    Check("the arming drive asks MayOpenRunFor", asks != std::string::npos);
    Check("it asks BEFORE it opens a run", asks != std::string::npos &&
                                              opens != std::string::npos && asks < opens);
    Check("it ends a run the member leads", drive.find("EndRunLedByMemberSittingOut(") !=
                                                std::string::npos);

    std::string const end = Body(text, "void EndRunLedByMemberSittingOut(");
    Check("the run is closed with the sit-out reason",
          end.find("SitOutRunEndedReason(") != std::string::npos &&
              end.find("CloseRun(") != std::string::npos);

    std::string const leave = Body(text, "void DriveSittingOutLeave()");
    Check("the walk out reuses the evacuation escort",
          leave.find("EscortToward(") != std::string::npos &&
              leave.find("EscortPurpose::LeaveInstance") != std::string::npos);
    Check("the walk out is driven on the coordinator's clock",
          text.find("DriveTownTrip();\n            // THE MEMBER SITTING OUT LEAVES") !=
              std::string::npos);
}

}  // namespace

int main()
{
    OnlyTheNamedMemberSitsOut();
    TheMemberSittingOutOpensNoRun();
    ARunTheMemberLeadsEndsNamingTheSitOut();
    TheArmingDriveAsksBeforeItOpensARun();
    if (failures)
        return 1;
    std::printf("ok test_sit_out_of_runs\n");
    return 0;
}
