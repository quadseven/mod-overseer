/*
 * A guild run in the Deadmines plays the way a group of players plays it.
 *
 * The live failure this pins. Both guilds have cleared the Deadmines 0 times
 * in 62 guild runs (2026-10-08 to 2026-10-10). Of the 45 runs that wiped (four
 * or five of five dead), 39 went down in one fight and the tank died first in
 * 32. Run 507 (2026-10-10, America/New_York) showed two more faults:
 *
 *   11:10:33 and 11:10:42: two members died to Defias Evokers. A body inside
 *     released the rest hold at once, so the dungeon module's rez could walk
 *     to it; the healer sat at 0 to 5% mana "waiting on mana" for that rez,
 *     and the unpaused brain walked the tank on to the next door with two
 *     down. Goblin Woodcarvers met it at 11:11:39, and the tank, the healer
 *     and the last damage dealer were dead by 11:12:13.
 *   11:39:16 to 11:44:28: with the tank dead, `dc ensure` went to the living
 *     once a minute and each refused: the dungeon module elects its leader
 *     among living tank bots only ("No tank bot found in your group"). The
 *     brain came back by itself when the tank did (11:46:31).
 *
 * A group of players drinks first, then raises its dead, then pulls; it kills
 * a marked skull first; and it does not ask a dead tank to lead.
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

using OverseerDecisions::GUILD_RUN_REARM_SECONDS;
using OverseerDecisions::GUILD_RUN_REST_MAX_SECONDS;
using OverseerDecisions::GuildRunHold;
using OverseerDecisions::GuildRunHoldFacts;
using OverseerDecisions::GuildRunHoldNext;
using OverseerDecisions::GuildRunHoldStep;
using OverseerDecisions::GuildRunHoldWhy;
using OverseerDecisions::GuildRunHoldWhyWord;
using OverseerDecisions::GuildRunRearmFacts;
using OverseerDecisions::GuildRunRearmNext;
using OverseerDecisions::GuildRunRearmStep;
using OverseerDecisions::GuildRunRestNeed;
using OverseerDecisions::GuildRunSeatTactics;
using OverseerDecisions::GuildSeat;

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

void ExpectHold(char const* what, GuildRunHoldFacts const& facts, GuildRunHoldStep step,
                GuildRunHoldWhy why)
{
    GuildRunHold const got = GuildRunHoldNext(facts);
    if (got.step == step && got.why == why)
        return;
    std::printf("FAIL %s: got step %d (%s), wanted step %d (%s)\n", what,
                static_cast<int>(got.step), GuildRunHoldWhyWord(got.why),
                static_cast<int>(step), GuildRunHoldWhyWord(why));
    ++failures;
}

// Run 507 at 11:10:45: the fight over, two of five dead inside, the healer dry.
GuildRunHoldFacts Run507TwoDown()
{
    GuildRunHoldFacts facts;
    facts.armed = true;
    facts.aliveInside = 3;
    facts.deadInside = 2;
    facts.need = GuildRunRestNeed::Low;
    return facts;
}

// ------------------------------------------------- rest before the rez --

void ADryGroupWithItsDeadRestsBeforeItMovesOn()
{
    GuildRunHoldFacts facts = Run507TwoDown();
    ExpectHold("two down and the healer dry: the brain is held for a rest", facts,
               GuildRunHoldStep::Hold, GuildRunHoldWhy::Rest);

    facts.held = true;
    facts.secondsResting = 60;
    ExpectHold("and stays held while the healer drinks", facts, GuildRunHoldStep::Nothing,
               GuildRunHoldWhy::Rest);

    facts.need = GuildRunRestNeed::Partial;
    ExpectHold("over the triggers: let go, and the body is why (the rez walks to it)", facts,
               GuildRunHoldStep::Release, GuildRunHoldWhy::Dead);

    facts.need = GuildRunRestNeed::Low;
    facts.secondsResting = GUILD_RUN_REST_MAX_SECONDS;
    ExpectHold("a rest with bodies is bounded like any rest", facts, GuildRunHoldStep::Release,
               GuildRunHoldWhy::RestTimedOut);

    GuildRunHoldFacts cooling = Run507TwoDown();
    cooling.restRearmed = false;
    ExpectHold("inside a timed-out rest's cooldown it is not held again", cooling,
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);

    GuildRunHoldFacts fighting = Run507TwoDown();
    fighting.anyFighting = true;
    ExpectHold("never mid-fight", fighting, GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);

    GuildRunHoldFacts fresh = Run507TwoDown();
    fresh.need = GuildRunRestNeed::Rested;
    ExpectHold("a body inside and a rested group: nothing to hold for", fresh,
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);
}

// ---------------------------------------------------------- the skull --

void TheTankMarksASkullForTheGroup()
{
    std::vector<std::string> const tank = GuildRunSeatTactics(GuildSeat::Tank);
    Check("the tank fights with mod-playerbots' own mark rti",
          tank.size() == 1 && tank.front() == "mark rti");
    Check("the healer's engine is left as it is", GuildRunSeatTactics(GuildSeat::Healer).empty());
    Check("a damage dealer already takes the skull first (its dps target reads the rti)",
          GuildRunSeatTactics(GuildSeat::Damage).empty());
}

// ----------------------------------------------------- a dead tank --

void ADeadTankIsNotAskedToLead()
{
    GuildRunRearmFacts f;
    f.armed = true;
    f.aliveInside = 1;
    f.secondsSinceIssued = GUILD_RUN_REARM_SECONDS;
    f.tankAlive = false;
    Check("run 507's tank lies dead and one member lives: nothing is asked",
          GuildRunRearmNext(f) == GuildRunRearmStep::Skip);
    f.tankAlive = true;
    Check("the tank back alive inside: asked again", GuildRunRearmNext(f) == GuildRunRearmStep::Issue);
}

// ---------------------------------------------------------- the wiring --

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

void TheAdapterIsWired()
{
    std::string const source = Read("src/mod_overseer.cpp");
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::string const drive = Between(source, "    bool DriveGuildRun(GuildRun& run, std::time_t now)",
                                      "    void DriveGuildRunStrays(");
    Check("the drive is found", !drive.empty());
    Check("the re-arm asks whether the tank is alive inside",
          drive.find("rearm.tankAlive = GuildRunAliveInside(run, tank);") != std::string::npos);
    std::size_t const tactics = drive.find("ApplyGuildRunTactics(run);");
    std::size_t const hold = drive.find("StepGuildRunHold(run, poll, ghostsWalking, now);");
    Check("the seat tactics are applied on every poll of an armed run, before the hold",
          tactics != std::string::npos && hold != std::string::npos && tactics < hold &&
              drive.find("if (run.dcAccepted)\n                ApplyGuildRunTactics(run);") !=
                  std::string::npos);

    std::string const apply = Between(source, "    void ApplyGuildRunTactics(GuildRun& run)",
                                      "    static void ReturnGuildRunLeases(");
    Check("the tactics come from the pure rule",
          apply.find("OverseerDecisions::GuildRunSeatTactics(run.seats[i])") != std::string::npos);
    Check("a strategy is turned on in the combat engine only when it is missing",
          apply.find("if (ai->HasStrategy(strategy, BOT_STATE_COMBAT))\n                    continue;") !=
                  std::string::npos &&
              apply.find("ai->ChangeStrategy(\"+\" + strategy, BOT_STATE_COMBAT);") != std::string::npos);
    Check("and only what was turned on is taken off",
          apply.find("run.tactics[run.names[i]].push_back(strategy);") != std::string::npos &&
              apply.find("ai->ChangeStrategy(\"-\" + strategy, BOT_STATE_COMBAT);") != std::string::npos);
    Check("the tactics' deploy proof is said",
          apply.find("overseer: guild run tactics {} - '{}' ({}) fights with '{}'") != std::string::npos);
    Check("the end of a run takes the tactics off",
          Between(source, "    void EndGuildRun(", "    static bool LeaveGuildRunQueue(")
                  .find("ReturnGuildRunTactics(run);") != std::string::npos);
    Check("nothing is granted: no teleport, no raise, no aura in the tactics",
          apply.find("TeleportTo") == std::string::npos &&
              apply.find("ResurrectPlayer") == std::string::npos &&
              apply.find("AddAura") == std::string::npos);
}

}  // namespace

int main()
{
    ADryGroupWithItsDeadRestsBeforeItMovesOn();
    TheTankMarksASkullForTheGroup();
    ADeadTankIsNotAskedToLead();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild run tactics: all checks passed\n");
    return 0;
}
