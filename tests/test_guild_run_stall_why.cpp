/*
 * A guild run's end says who was missing and where it stalled (issue 936).
 *
 * The live failure this pins: 73 guild runs timed out at 7200 s in the 24
 * hours to 2026-10-12 18:13 UTC, about 720 member-hours, and 21 runs of one
 * guild were abandoned for missing roles. The row said only "inside for
 * 7200s" or "the assigned tank and healer roles were not both alive inside
 * for 300s", and the worldserver log holds about five minutes, so the stall
 * point of any older run was lost. The finder's wait never named the member
 * it waited on either.
 *
 * The abandoned why now names each absent seat member and its state, and the
 * timed-out why the dungeon brain's next boss and the minutes since the last
 * boss kill. The heartbeat row carries the same two fields.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::GuildRunAbsence;
using OverseerDecisions::GuildRunAbsenceOf;
using OverseerDecisions::GuildRunAbsenceWord;
using OverseerDecisions::GuildRunAbsentMember;
using OverseerDecisions::GuildRunAbsentWhy;
using OverseerDecisions::GuildRunNextBossIndex;
using OverseerDecisions::GuildRunResume;
using OverseerDecisions::GuildRunTimedOutWhy;
using OverseerDecisions::GuildRunVerdict;
using OverseerDecisions::GuildRunVerdictWord;
using OverseerDecisions::GuildSeat;
using OverseerDecisions::ReadGuildRunResume;

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

void CheckSays(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++failures;
}

GuildRunAbsentMember Member(char const* name, GuildSeat seat, GuildRunAbsence absence,
                            bool inside, bool cameInside)
{
    GuildRunAbsentMember m;
    m.name = name;
    m.seat = seat;
    m.absence = absence;
    m.inside = inside;
    m.cameInside = cameInside;
    return m;
}

void TheStateIsTheFirstThatHolds()
{
    Check("logged out is not in world",
          GuildRunAbsenceOf(false, false, false, false) == GuildRunAbsence::NotInWorld);
    Check("a body is dead before it is anywhere",
          GuildRunAbsenceOf(true, false, true, true) == GuildRunAbsence::Dead);
    Check("a fight comes before the map",
          GuildRunAbsenceOf(true, true, true, false) == GuildRunAbsence::InCombat);
    Check("alive and free elsewhere is another map",
          GuildRunAbsenceOf(true, true, false, false) == GuildRunAbsence::OtherMap);
    Check("alive and free in the copy is here",
          GuildRunAbsenceOf(true, true, false, true) == GuildRunAbsence::Present);
    CheckSays("the words", std::string(GuildRunAbsenceWord(GuildRunAbsence::NotInWorld)) + "|" +
                               GuildRunAbsenceWord(GuildRunAbsence::Dead) + "|" +
                               GuildRunAbsenceWord(GuildRunAbsence::InCombat) + "|" +
                               GuildRunAbsenceWord(GuildRunAbsence::OtherMap),
              "not in world|dead|in combat|other map");
}

void TheAbandonedWhyNamesTheAbsent()
{
    CheckSays("a healer that never came in, fighting outside",
              GuildRunAbsentWhy({Member("Healbot", GuildSeat::Healer, GuildRunAbsence::InCombat,
                                        false, false)}),
              "'Healbot' (healer) never came inside: in combat");

    CheckSays("a tank dead inside and a healer that left the copy",
              GuildRunAbsentWhy(
                  {Member("Tankbot", GuildSeat::Tank, GuildRunAbsence::Dead, true, true),
                   Member("Healbot", GuildSeat::Healer, GuildRunAbsence::NotInWorld, false,
                          true)}),
              "'Tankbot' (tank) is inside: dead; 'Healbot' (healer) left the copy: not in "
              "world");

    CheckSays("the present are left out",
              GuildRunAbsentWhy(
                  {Member("Tankbot", GuildSeat::Tank, GuildRunAbsence::Present, true, true),
                   Member("Stabbot", GuildSeat::Damage, GuildRunAbsence::OtherMap, false,
                          false)}),
              "'Stabbot' (dps) never came inside: other map");

    CheckSays("nobody absent says nothing",
              GuildRunAbsentWhy(
                  {Member("Tankbot", GuildSeat::Tank, GuildRunAbsence::Present, true, true)}),
              "");
}

void TheTimedOutWhySaysWhereItStalled()
{
    CheckSays("the next boss and the minutes since the last kill",
              GuildRunTimedOutWhy(7200, "Gelihast", true, 95 * 60 + 59),
              "inside for 7200s; next Gelihast, last kill 95 min ago");
    CheckSays("no kill yet", GuildRunTimedOutWhy(7200, "Ghamoo-ra", false, 0),
              "inside for 7200s; next Ghamoo-ra, no kill yet");
    CheckSays("no next boss read", GuildRunTimedOutWhy(7201, "", true, 30),
              "inside for 7201s; next boss unknown, last kill 0 min ago");

    CheckSays("a kill this very second is 0 min ago", GuildRunTimedOutWhy(7200, "Gelihast", true, 0),
              "inside for 7200s; next Gelihast, last kill 0 min ago");
    CheckSays("59 s floors to 0 min and 60 s is 1 min",
              GuildRunTimedOutWhy(7200, "Gelihast", true, 59) + "|" +
                  GuildRunTimedOutWhy(7200, "Gelihast", true, 60),
              "inside for 7200s; next Gelihast, last kill 0 min ago|"
              "inside for 7200s; next Gelihast, last kill 1 min ago");

    Check("the next boss is the lowest expected bit not credited",
          GuildRunNextBossIndex(0x7Fu, 0x0Bu) == 2);
    Check("nothing expected has no next boss", GuildRunNextBossIndex(0, 0) == -1);
    Check("everything credited has no next boss", GuildRunNextBossIndex(0x7Fu, 0xFFu) == -1);
}

void TheVerdictWordsAreUnchanged()
{
    CheckSays("verdict words",
              std::string(GuildRunVerdictWord(GuildRunVerdict::Running)) + "|" +
                  GuildRunVerdictWord(GuildRunVerdict::Cleared) + "|" +
                  GuildRunVerdictWord(GuildRunVerdict::Wiped) + "|" +
                  GuildRunVerdictWord(GuildRunVerdict::Abandoned) + "|" +
                  GuildRunVerdictWord(GuildRunVerdict::TimedOut),
              "running|cleared|wiped|abandoned|timed out");
}

void TheRowCarriesTheLastKillAcrossARestart()
{
    GuildRunResume const with = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":389,\"bosses_done\":2,\"bosses_total\":4,"
        "\"next_boss\":\"Bazzalan\",\"seconds_since_kill\":1830,\"members\":[]}");
    Check("bosses down are read back", with.bossesDone == 2);
    Check("a kill is read back", with.killSeen && with.secondsSinceKill == 1830);

    GuildRunResume const without = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":389,\"bosses_done\":0,\"next_boss\":\"Oggleflint\","
        "\"members\":[]}");
    Check("a row with no kill reads none", !without.killSeen);
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const a = text.find(from);
    if (a == std::string::npos)
        return std::string();
    std::size_t const b = text.find(to, a + 1);
    return text.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

void TheAdapterIsWired()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable", !source.empty());

    std::string const drive =
        Between(source, "    bool DriveGuildRun(GuildRun& run, std::time_t now)",
                "    // A RESTARTED WORLDSERVER'S RUN, TAKEN BACK UP");
    Check("the queued poll log names who it waits on",
          drive.find("absent != run.saidAbsent") != std::string::npos);
    Check("the abandoned why names the absent seats",
          drive.find("GuildRunAbsentees(run, run.instanceId, poll.inside > 0)") !=
              std::string::npos);
    Check("the timed-out why says where it stalled",
          drive.find("OverseerDecisions::GuildRunTimedOutWhy(") != std::string::npos);

    std::string const json =
        Between(source, "    std::string GuildRunJson(", "    char const* DoGuildFinderRun(");
    Check("the heartbeat carries the next boss",
          json.find("\\\"next_boss\\\":") != std::string::npos);
    Check("the heartbeat carries the seconds since the last kill",
          json.find("\\\"seconds_since_kill\\\":") != std::string::npos);
}

}  // namespace

int main()
{
    TheStateIsTheFirstThatHolds();
    TheAbandonedWhyNamesTheAbsent();
    TheTimedOutWhySaysWhereItStalled();
    TheVerdictWordsAreUnchanged();
    TheRowCarriesTheLastKillAcrossARestart();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
