/*
 * A guild run's end says who was missing and where it stalled (issue 936).
 *
 * 73 guild runs timed out in a day and each row said only "inside for
 * 7200s"; 21 were abandoned and said only "not both alive inside for 300s".
 * The worldserver log holds minutes, so where any older run stalled was lost,
 * and the finder's wait never named the member it waited on. These are the
 * words the queued poll's log, the abandoned and timed-out `why` and the
 * run's heartbeat now carry, and the heartbeat read back after a restart.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::GuildFinderWaitWords;
using OverseerDecisions::GuildRunAbandonedWhy;
using OverseerDecisions::GuildRunMissingWords;
using OverseerDecisions::GuildRunPoll;
using OverseerDecisions::GuildRunResume;
using OverseerDecisions::GuildRunSeatFacts;
using OverseerDecisions::GuildRunSeatState;
using OverseerDecisions::GuildRunSeatStateOf;
using OverseerDecisions::GuildRunStall;
using OverseerDecisions::GuildRunStallWords;
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

void CheckWords(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++failures;
}

GuildRunSeatFacts Seat(char const* name, GuildSeat seat, bool inWorld, bool alive, bool inCombat,
                       bool inRunCopy)
{
    GuildRunSeatFacts facts;
    facts.name = name;
    facts.seat = seat;
    facts.inWorld = inWorld;
    facts.alive = alive;
    facts.inCombat = inCombat;
    facts.inRunCopy = inRunCopy;
    return facts;
}

// Tank, healer and three damage dealers, all alive inside and quiet.
std::vector<GuildRunSeatFacts> AllInside()
{
    return {Seat("Tanko", GuildSeat::Tank, true, true, false, true),
            Seat("Mendy", GuildSeat::Healer, true, true, false, true),
            Seat("Stabba", GuildSeat::Damage, true, true, false, true),
            Seat("Zappo", GuildSeat::Damage, true, true, false, true),
            Seat("Shooty", GuildSeat::Damage, true, true, false, true)};
}

void EachSeatStateIsTheFirstThatHolds()
{
    Check("not in world is asked first",
          GuildRunSeatStateOf(Seat("A", GuildSeat::Tank, false, false, false, false)) ==
              GuildRunSeatState::NotInWorld);
    Check("then dead, wherever it lies",
          GuildRunSeatStateOf(Seat("A", GuildSeat::Tank, true, false, false, true)) ==
              GuildRunSeatState::Dead);
    Check("then in combat",
          GuildRunSeatStateOf(Seat("A", GuildSeat::Tank, true, true, true, false)) ==
              GuildRunSeatState::InCombat);
    Check("then not in the run's copy",
          GuildRunSeatStateOf(Seat("A", GuildSeat::Tank, true, true, false, false)) ==
              GuildRunSeatState::OtherMap);
    Check("alive, quiet and in the copy is inside",
          GuildRunSeatStateOf(Seat("A", GuildSeat::Tank, true, true, false, true)) ==
              GuildRunSeatState::Inside);
}

void TheFinderWaitNamesEachMemberItWaitsOn()
{
    // The members out in the world while queued: one fighting, one dead, one
    // logged out, two simply elsewhere (the finder does not wait on those).
    std::vector<GuildRunSeatFacts> const seats = {
        Seat("Tanko", GuildSeat::Tank, true, true, false, false),
        Seat("Mendy", GuildSeat::Healer, true, true, true, false),
        Seat("Stabba", GuildSeat::Damage, true, false, false, false),
        Seat("Zappo", GuildSeat::Damage, false, false, false, false),
        Seat("Shooty", GuildSeat::Damage, true, true, false, false)};
    CheckWords("the wait names who is not ready and why", GuildFinderWaitWords(seats),
               "'Mendy' (healer): in combat, 'Stabba' (dps): dead, 'Zappo' (dps): not in world");
    std::vector<GuildRunSeatFacts> ready = seats;
    for (GuildRunSeatFacts& facts : ready)
        facts = Seat(facts.name.c_str(), facts.seat, true, true, false, false);
    CheckWords("nobody to wait on says nothing", GuildFinderWaitWords(ready), "");
}

void TheAbandonedWhyNamesTheHealerThatNeverCameInside()
{
    // The finder group was made; the healer was fighting outside and never
    // teleported, and the tank fights inside.
    std::vector<GuildRunSeatFacts> seats = AllInside();
    seats[0].inCombat = true;
    seats[1] = Seat("Mendy", GuildSeat::Healer, true, true, true, false);
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 4;
    poll.aliveInside = 4;
    poll.healerAliveInside = false;
    poll.secondsWithoutRoles = 300;
    CheckWords("a missing healer is named with its state", GuildRunAbandonedWhy(poll, seats),
               "the assigned tank and healer roles were not both alive inside for 300s; "
               "missing 'Mendy' (healer): in combat");
}

void TheAbandonedWhyNamesEveryoneNotAliveInTheCopy()
{
    std::vector<GuildRunSeatFacts> seats = AllInside();
    seats[0] = Seat("Tanko", GuildSeat::Tank, true, false, false, true);    // dead inside
    seats[1] = Seat("Mendy", GuildSeat::Healer, true, true, false, false);  // elsewhere
    seats[4] = Seat("Shooty", GuildSeat::Damage, false, false, false, false);
    CheckWords("each missing member, in seat order", GuildRunMissingWords(seats),
               "'Tanko' (tank): dead, 'Mendy' (healer): other map, 'Shooty' (dps): not in world");

    GuildRunPoll gone;
    gone.groupGone = true;
    CheckWords("a gone group still names the missing", GuildRunAbandonedWhy(gone, seats),
               "the group is gone; missing 'Tanko' (tank): dead, 'Mendy' (healer): other map, "
               "'Shooty' (dps): not in world");

    std::vector<GuildRunSeatFacts> out = AllInside();
    for (GuildRunSeatFacts& facts : out)
        facts.inRunCopy = false;
    GuildRunPoll empty;
    empty.secondsEmpty = 120;
    CheckWords("an empty dungeon names all five",
               GuildRunAbandonedWhy(empty, out),
               "nobody has been inside for 120s; missing 'Tanko' (tank): other map, 'Mendy' "
               "(healer): other map, 'Stabba' (dps): other map, 'Zappo' (dps): other map, "
               "'Shooty' (dps): other map");

    GuildRunPoll inside;
    inside.inside = 5;
    inside.secondsWithoutRoles = 300;
    CheckWords("nobody missing adds nothing", GuildRunAbandonedWhy(inside, AllInside()),
               "the assigned tank and healer roles were not both alive inside for 300s");
}

void TheTimedOutWhySaysTheNextBossAndTheLastKill()
{
    GuildRunPoll poll;
    poll.secondsInside = 7200;
    GuildRunStall stall;
    stall.nextBoss = "Gelihast";
    stall.killSeen = true;
    stall.secondsSinceKill = 95 * 60 + 59;
    CheckWords("the next boss and the whole minutes since the kill",
               GuildRunTimedOutWhy(poll, stall),
               "inside for 7200s; next Gelihast, last kill 95 min ago");

    stall.killSeen = false;
    CheckWords("no kill yet says so", GuildRunStallWords(stall),
               "next Gelihast, no boss kill seen");

    stall.nextBoss.clear();
    stall.killSeen = true;
    stall.secondsSinceKill = 30;
    CheckWords("an unknown next boss is left out", GuildRunStallWords(stall),
               "last kill 0 min ago");
}

void TheVerdictWordsAreUnchanged()
{
    CheckWords("running", GuildRunVerdictWord(GuildRunVerdict::Running), "running");
    CheckWords("cleared", GuildRunVerdictWord(GuildRunVerdict::Cleared), "cleared");
    CheckWords("wiped", GuildRunVerdictWord(GuildRunVerdict::Wiped), "wiped");
    CheckWords("abandoned", GuildRunVerdictWord(GuildRunVerdict::Abandoned), "abandoned");
    CheckWords("timed out", GuildRunVerdictWord(GuildRunVerdict::TimedOut), "timed out");
}

void TheHeartbeatsKillIsReadBackAfterARestart()
{
    GuildRunResume const seen = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":43,\"bosses_done\":3,\"bosses_total\":7,"
        "\"next_boss\":\"Skum\",\"seconds_since_kill\":1500,\"dc_on\":true,"
        "\"members\":[{\"name\":\"Tanko\",\"seconds_since_kill\":9}]}");
    Check("the boss count is read", seen.bossesDone == 3);
    Check("a kill is seen", seen.killSeen);
    Check("its age is read from the run, not a member", seen.secondsSinceKill == 1500);

    GuildRunResume const none = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"bosses_done\":0,\"next_boss\":null,"
        "\"seconds_since_kill\":null,\"members\":[]}");
    Check("null is no kill", !none.killSeen && none.secondsSinceKill == 0);

    GuildRunResume const old = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"bosses_done\":2,\"members\":[]}");
    Check("a row from before the field is no kill", !old.killSeen && old.bossesDone == 2);

    GuildRunResume const empty = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"seconds_since_kill\":,\"members\":[]}");
    Check("an empty value is no kill", !empty.killSeen);
}

}  // namespace

int main()
{
    EachSeatStateIsTheFirstThatHolds();
    TheFinderWaitNamesEachMemberItWaitsOn();
    TheAbandonedWhyNamesTheHealerThatNeverCameInside();
    TheAbandonedWhyNamesEveryoneNotAliveInTheCopy();
    TheTimedOutWhySaysTheNextBossAndTheLastKill();
    TheVerdictWordsAreUnchanged();
    TheHeartbeatsKillIsReadBackAfterARestart();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
