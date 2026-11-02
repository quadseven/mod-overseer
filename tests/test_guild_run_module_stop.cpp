/*
 * A guild finder run does not arm the dungeon module back into the stop it
 * ended on (#1205).
 *
 * The defect: the guild run's re-arm asked `dc ensure` once a minute whenever
 * the module was off, whatever had switched it off. A Deadmines party with no
 * opener for the cannon failed the Iron Clad Door event, the module ended the
 * clear with that reason, and the re-arm walked the party back to the same
 * door about half a minute later. The run looped until the two-hour cap and
 * was recorded as a time-out.
 *
 * A stop the module itself says to resume from (a wipe, a death, a corpse run
 * outside) is armed again as before, and a party-wait stop is armed again up
 * to the same three times the family coordinator allows (#1308). Every other
 * stop ends the run with the module's own words.
 *
 * Also pins the adapter: the re-arm asks GuildRunArmAfterModuleStop before it
 * sends the verb, and the run ends on that answer only after the verdict.
 *
 * Compiles against the pure decision file.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

char const* const kCannon =
    "Dungeon event 'Iron Clad Door (Defias Cannon)' failed at step 1; ending dungeon clear.";
char const* const kWait =
    "Dungeon clear stopped: the party never became ready after 180s (Waiting on Bo (low mana))";

void AFailedEventEndsTheRun()
{
    Check("a failed dungeon event gives EndWithReason",
          GuildRunArmAfterModuleStop(kCannon, 0) == GuildRunModuleStopStep::EndWithReason);
    Check("nothing left to reach gives EndWithReason",
          GuildRunArmAfterModuleStop("No bosses left - disabling", 0) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("a failed portal event gives EndWithReason",
          GuildRunArmAfterModuleStop("Medivh's shield hit 0 - the Dark Portal event failed", 0) ==
              GuildRunModuleStopStep::EndWithReason);
}

void APartyWaitIsArmedWithinItsBudget()
{
    Check("a first party-wait stop gives Rearm",
          GuildRunArmAfterModuleStop(kWait, 0) == GuildRunModuleStopStep::Rearm);
    Check("the last party-wait re-arm in budget gives Rearm",
          GuildRunArmAfterModuleStop(kWait, GUILD_RUN_WAIT_REARMS - 1) ==
              GuildRunModuleStopStep::Rearm);
    Check("a party-wait stop past the budget gives EndWithReason",
          GuildRunArmAfterModuleStop(kWait, GUILD_RUN_WAIT_REARMS) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("the budget is the family coordinator's three", GUILD_RUN_WAIT_REARMS == 3);
}

void AResumableStopIsArmedAsBefore()
{
    Check("no stop reason gives Rearm",
          GuildRunArmAfterModuleStop("", 0) == GuildRunModuleStopStep::Rearm);
    Check("a wipe gives Rearm",
          GuildRunArmAfterModuleStop("The party wiped - dungeon clear disabled. Type 'dc on' "
                                     "when ready to resume.",
                                     0) == GuildRunModuleStopStep::Rearm);
    Check("a death gives Rearm",
          GuildRunArmAfterModuleStop("Bo died - dungeon clear disabled. Type 'dc on' when "
                                     "ready to resume.",
                                     5) == GuildRunModuleStopStep::Rearm);
    Check("a corpse run outside gives Rearm",
          GuildRunArmAfterModuleStop("Corpse run ended outside its dungeon instance.", 0) ==
              GuildRunModuleStopStep::Rearm);
    Check("leaving the dungeon gives Rearm",
          GuildRunArmAfterModuleStop("Left the dungeon - dungeon clear disabled.", 0) ==
              GuildRunModuleStopStep::Rearm);
    Check("a plain 'dc off' gives Rearm",
          GuildRunArmAfterModuleStop("Dungeon clear disabled.", 0) ==
              GuildRunModuleStopStep::Rearm);
}

void TheWhyCarriesTheModuleWords()
{
    std::string const why =
        GuildRunModuleStopWhy(kCannon, "next Mr. Smite, last kill 79 min ago");
    Check("the why leads with the module's words",
          why.rfind("dungeon clear stopped: Dungeon event 'Iron Clad Door (Defias Cannon)' "
                    "failed at step 1",
                    0) == 0);
    Check("the why says the run was not armed again",
          why.find("not armed again") != std::string::npos);
    std::string const stall = "next Mr. Smite, last kill 79 min ago";
    Check("the why ends with where the run stood",
          why.size() >= stall.size() &&
              why.compare(why.size() - stall.size(), stall.size(), stall) == 0);
    Check("the why is not a time-out", why.find("timed out") == std::string::npos);
}

std::string Body(std::string const& text, char const* signature, char const* end)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const stop = text.find(end, start);
    return stop == std::string::npos ? std::string() : text.substr(start, stop - start);
}

void TheAdapterAsksBeforeItArms()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    Check("src/mod_overseer.cpp is readable", !text.empty());

    std::string const drive = Body(text, "    bool DriveGuildRun(", "\n    }\n\n");
    std::size_t const ask = drive.find("GuildRunArmAfterModuleStop(");
    std::size_t const send = drive.find("GUILD_RUN_REARM_VERB,");
    Check("the re-arm asks GuildRunArmAfterModuleStop before it sends the verb",
          ask != std::string::npos && send != std::string::npos && ask < send);
    std::size_t const verdict = drive.find("GuildRunNext(poll)");
    std::size_t const ends = drive.find("GuildRunModuleStopWhy(");
    std::size_t const ladder = drive.find("StepGuildRunStall(run");
    Check("the run ends on the module's stop after the verdict and before the ladder",
          verdict != std::string::npos && ends != std::string::npos &&
              ladder != std::string::npos && verdict < ends && ends < ladder);
}
}  // namespace

int main()
{
    AFailedEventEndsTheRun();
    APartyWaitIsArmedWithinItsBudget();
    AResumableStopIsArmedAsBefore();
    TheWhyCarriesTheModuleWords();
    TheAdapterAsksBeforeItArms();
    return failures ? 1 : 0;
}
