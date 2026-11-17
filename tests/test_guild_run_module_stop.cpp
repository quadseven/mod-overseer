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
 * One stop is neither: the module switches itself off with "All bosses
 * cleared!" when it has counted every boss down, and that list above did not
 * name it, so a guild run that cleared ended with EndWithReason and was
 * booked stalled (#1331) - the guild version of the family defect #1328
 * fixed. It gives EndComplete, and the run ends cleared.
 *
 * But only when the instance mask does not contradict it (#1587): the module
 * counts its own boss list, which can be shorter than what the run expects, so
 * a mask that says NotYet outranks the module and the run ends with the
 * module's reason instead, as a family run does (#1514).
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
// A map that credits nothing: no mask to contradict the module's word.
DungeonCompletion const kNoMask = DungeonCompletion::Unknowable;
char const* const kWait =
    "Dungeon clear stopped: the party never became ready after 180s (Waiting on Bo (low mana))";

void AFailedEventEndsTheRun()
{
    Check("a failed dungeon event gives EndWithReason",
          GuildRunArmAfterModuleStop(kCannon, 0, kNoMask) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("nothing left to reach gives EndWithReason",
          GuildRunArmAfterModuleStop("No bosses left - disabling", 0, kNoMask) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("a failed portal event gives EndWithReason",
          GuildRunArmAfterModuleStop("Medivh's shield hit 0 - the Dark Portal event failed", 0,
                                     kNoMask) ==
              GuildRunModuleStopStep::EndWithReason);
}

void AClearedStopEndsTheRunComplete()
{
    Check("a cleared stop gives EndComplete",
          GuildRunArmAfterModuleStop("All bosses cleared!", 0, kNoMask) ==
              GuildRunModuleStopStep::EndComplete);
    Check("a cleared stop with the module's prefix gives EndComplete",
          GuildRunArmAfterModuleStop("Dungeon clear stopped: All bosses cleared!", 0, kNoMask) ==
              GuildRunModuleStopStep::EndComplete);
    Check("a sentence that only contains the words is not a clear",
          GuildRunArmAfterModuleStop("Not All bosses cleared! before the timer", 0, kNoMask) ==
              GuildRunModuleStopStep::EndWithReason);
}

void AClearedStopNeedsTheMaskToAgree()
{
    // The module counts its own boss list, which can be shorter than what the
    // run expects (#1587, the guild run's version of #1514).
    Check("a cleared stop the mask contradicts gives EndWithReason, not EndComplete",
          GuildRunArmAfterModuleStop("All bosses cleared!", 0, DungeonCompletion::NotYet) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("a prefixed cleared stop the mask contradicts gives EndWithReason",
          GuildRunArmAfterModuleStop("Dungeon clear stopped: All bosses cleared!", 0,
                                     DungeonCompletion::NotYet) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("a contradicted cleared stop is never armed back into",
          GuildRunArmAfterModuleStop("All bosses cleared!", 0, DungeonCompletion::NotYet) !=
              GuildRunModuleStopStep::Rearm);
    Check("a cleared stop the mask agrees with gives EndComplete",
          GuildRunArmAfterModuleStop("All bosses cleared!", 0, DungeonCompletion::Complete) ==
              GuildRunModuleStopStep::EndComplete);
    Check("a cleared stop on a map that credits nothing gives EndComplete",
          GuildRunArmAfterModuleStop("All bosses cleared!", 0, DungeonCompletion::Unknowable) ==
              GuildRunModuleStopStep::EndComplete);
    Check("the mask does not turn a resumable stop into an ending",
          GuildRunArmAfterModuleStop("Dungeon clear disabled.", 0, DungeonCompletion::NotYet) ==
              GuildRunModuleStopStep::Rearm);
    Check("the mask does not turn a failed event into a clear",
          GuildRunArmAfterModuleStop(kCannon, 0, DungeonCompletion::Complete) ==
              GuildRunModuleStopStep::EndWithReason);
}

void APartyWaitIsArmedWithinItsBudget()
{
    Check("a first party-wait stop gives Rearm",
          GuildRunArmAfterModuleStop(kWait, 0, kNoMask) == GuildRunModuleStopStep::Rearm);
    Check("the last party-wait re-arm in budget gives Rearm",
          GuildRunArmAfterModuleStop(kWait, GUILD_RUN_WAIT_REARMS - 1, kNoMask) ==
              GuildRunModuleStopStep::Rearm);
    Check("a party-wait stop past the budget gives EndWithReason",
          GuildRunArmAfterModuleStop(kWait, GUILD_RUN_WAIT_REARMS, kNoMask) ==
              GuildRunModuleStopStep::EndWithReason);
    Check("the budget is the family coordinator's three", GUILD_RUN_WAIT_REARMS == 3);
}

void AResumableStopIsArmedAsBefore()
{
    Check("no stop reason gives Rearm",
          GuildRunArmAfterModuleStop("", 0, kNoMask) == GuildRunModuleStopStep::Rearm);
    Check("a wipe gives Rearm",
          GuildRunArmAfterModuleStop("The party wiped - dungeon clear disabled. Type 'dc on' "
                                     "when ready to resume.",
                                     0, kNoMask) == GuildRunModuleStopStep::Rearm);
    Check("a death gives Rearm",
          GuildRunArmAfterModuleStop("Bo died - dungeon clear disabled. Type 'dc on' when "
                                     "ready to resume.",
                                     5, kNoMask) == GuildRunModuleStopStep::Rearm);
    Check("a corpse run outside gives Rearm",
          GuildRunArmAfterModuleStop("Corpse run ended outside its dungeon instance.", 0,
                                     kNoMask) ==
              GuildRunModuleStopStep::Rearm);
    Check("leaving the dungeon gives Rearm",
          GuildRunArmAfterModuleStop("Left the dungeon - dungeon clear disabled.", 0, kNoMask) ==
              GuildRunModuleStopStep::Rearm);
    Check("a plain 'dc off' gives Rearm",
          GuildRunArmAfterModuleStop("Dungeon clear disabled.", 0, kNoMask) ==
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
    // The run's own masks are what may contradict the module's count (#1587).
    std::size_t const masks = drive.find(
        "maskSays =\n                    "
        "OverseerDecisions::DungeonRunCompletion(poll.expectedMask, poll.creditedMask);");
    std::size_t const handed = drive.find("run.waitRearms, maskSays)", ask);
    Check("the re-arm hands the decision the run's expected and credited masks",
          masks != std::string::npos && handed != std::string::npos && masks < handed &&
              handed < send);
    std::size_t const complete = drive.find("GuildRunModuleStopStep::EndComplete");
    std::size_t const flag = drive.find("run.moduleCleared = true");
    Check("a cleared stop marks the run cleared where the re-arm asks",
          complete != std::string::npos && flag != std::string::npos && complete < flag);
    std::size_t const block = drive.find("if (!run.moduleStop.empty())");
    std::size_t const clearedEnd = drive.find("GuildRunVerdict::Cleared", block);
    std::size_t const stalledEnd = drive.find("GuildRunVerdict::Stalled", block);
    Check("a run the module cleared ends cleared, ahead of the stalled ending",
          block != std::string::npos && clearedEnd != std::string::npos &&
              stalledEnd != std::string::npos && clearedEnd < stalledEnd);
}
}  // namespace

int main()
{
    AFailedEventEndsTheRun();
    AClearedStopEndsTheRunComplete();
    AClearedStopNeedsTheMaskToAgree();
    APartyWaitIsArmedWithinItsBudget();
    AResumableStopIsArmedAsBefore();
    TheWhyCarriesTheModuleWords();
    TheAdapterAsksBeforeItArms();
    return failures ? 1 : 0;
}
