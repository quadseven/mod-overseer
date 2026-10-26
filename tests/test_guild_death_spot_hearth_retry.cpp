/*
 * A guild death-spot hearth-out refused because the character was moving is
 * retried with the character held still, bounded; and a member whose hearth-out
 * failed is kept out of the zone it died in.
 *
 * Reproduces the reported loop: a member killed by a much higher-level guard
 * has hearth-out refused because it is moving, then walks back after the hold
 * is lost. The decision retries are bounded and failure holds the member out.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * the module source for the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::DecideHearthOutRetry;
using OverseerDecisions::HearthOutRetry;
using OverseerDecisions::HearthOutCannotSucceed;
using OverseerDecisions::KeepOutOfDeathZone;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckRetry(char const* what, HearthOutRetry got, HearthOutRetry want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, static_cast<int>(got),
                static_cast<int>(want));
    ++failures;
}

void TheRetryDecision()
{
    // Refused because the character was moving: hold it still and try again.
    CheckRetry("moving, first try",
               DecideHearthOutRetry("character is moving", false, 0, 3),
               HearthOutRetry::Retry);
    CheckRetry("moving, under the bound",
               DecideHearthOutRetry("character is moving", false, 2, 3),
               HearthOutRetry::Retry);
    // The bound stops the same member being asked every poll for ever.
    CheckRetry("moving, at the bound",
               DecideHearthOutRetry("character is moving", false, 3, 3),
               HearthOutRetry::GiveUp);
    CheckRetry("moving, past the bound",
               DecideHearthOutRetry("character is moving", false, 4, 3),
               HearthOutRetry::GiveUp);
    // A refusal nothing this poll can change is not retried.
    CheckRetry("on cooldown",
               DecideHearthOutRetry("hearthstone is on cooldown", false, 0, 3),
               HearthOutRetry::GiveUp);
    CheckRetry("no stone",
               DecideHearthOutRetry("character carries no hearthstone", false, 0, 3),
               HearthOutRetry::GiveUp);
    // The hearth landed but left the character on the death map: try again.
    CheckRetry("still on the death map, cast landed",
               DecideHearthOutRetry("", true, 0, 3), HearthOutRetry::Retry);
    CheckRetry("still on the death map, at the bound",
               DecideHearthOutRetry("", true, 3, 3), HearthOutRetry::GiveUp);
    // Nothing to retry.
    CheckRetry("no refusal, not on the death map",
               DecideHearthOutRetry("", false, 0, 3), HearthOutRetry::GiveUp);
}

void TheKeepOutDecision()
{
    // The hearth-out failed and the member still stands in the death zone: keep
    // it out until it leaves the map or the guild moves it.
    Check("hearth failed, still in the zone, not moved by guild",
          KeepOutOfDeathZone(true, true, false), true);
    // It left the map: nothing to keep it out of.
    Check("hearth failed, left the map",
          !KeepOutOfDeathZone(true, false, false), true);
    // The guild moved it: not this module's concern any more.
    Check("hearth failed, guild moved it",
          !KeepOutOfDeathZone(true, true, true), true);
    // The hearth did not fail: no keep-out.
    Check("hearth did not fail",
          !KeepOutOfDeathZone(false, true, false), true);
}

void TheHearthThatCannotSucceed()
{
    // The 2026-10-24 loop: the hearth was cast and broken, or landed and left
    // the member where it was, and the next poll found the stone not ready.
    // A stone that is not ready is a hearth-out that cannot succeed, and the
    // member killed far above its level is kept out rather than released.
    Check("stone on cooldown", HearthOutCannotSucceed(true, true, 0, 3), true);
    Check("no hearthstone", HearthOutCannotSucceed(false, false, 0, 3), true);
    Check("tries spent", HearthOutCannotSucceed(true, false, 3, 3), true);
    Check("stone ready, tries left", HearthOutCannotSucceed(true, false, 2, 3), false);
    Check("an outmatched member whose stone is on cooldown is kept out",
          KeepOutOfDeathZone(HearthOutCannotSucceed(true, true, 1, 3), true, false), true);
    Check("a member not outmatched here is not held for a cooldown",
          KeepOutOfDeathZone(HearthOutCannotSucceed(true, true, 1, 3), false, false), false);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)",
          !source.empty(), true);
    Check("the death spot hearth retry decision is wired",
          source.find("OverseerDecisions::DecideHearthOutRetry(") != std::string::npos, true);
    // The retry is asked whether the member is still on its death map, not
    // whether an outmatched death is marked here.
    Check("the retry is given the death-map fact",
          source.find("DecideHearthOutRetry(refusal.c_str(), stillOnDeathMap,") !=
              std::string::npos, true);
    Check("the retry is not given the outmatched mark as the death-map fact",
          source.find("DecideHearthOutRetry(refusal.c_str(), outmatchedHere,") ==
              std::string::npos, true);
    Check("the death spot keep-out decision is wired",
          source.find("OverseerDecisions::KeepOutOfDeathZone(") != std::string::npos, true);
    Check("a failed death-spot hearth clears the outside walk",
          source.find("bot->GetMotionMaster()->Clear();\n            bot->StopMoving();") !=
              std::string::npos, true);

    // Every death-spot hearth, the first and each retry, is driven with the
    // outside walk cleared and the member held still: a cast driven while
    // something else keeps a walk on the character is broken by the movement.
    std::size_t const spot = source.find("bool LeaveGuildDeathSpot(");
    std::size_t const still = source.find("holdStill();\n", spot);
    std::size_t const cast = source.find(
        "DoHearth(bot, \"use\", status, evidence, _pendingHearths, 0);", spot);
    Check("a death-spot hearth stops the member before it is driven",
          spot != std::string::npos && still != std::string::npos &&
              cast != std::string::npos && still < cast,
          true);
    // GuildLeavesDeathSpot says Stay for a stone that is not ready, and Stay
    // returned the member to the drives that walk it back toward the killer.
    // The keep-out for a hearth-out that cannot succeed is asked first.
    std::size_t const cannot = source.find("OverseerDecisions::HearthOutCannotSucceed(", spot);
    std::size_t const gate = source.find("OverseerDecisions::GuildLeavesDeathSpot(", spot);
    Check("a hearth-out that cannot succeed keeps the member out before the stone gate",
          spot != std::string::npos && cannot != std::string::npos &&
              gate != std::string::npos && cannot < gate,
          true);
    Check("a failed death-spot hearth holds the member still",
          source.find("HoldStillAndReport(bot, name, \"hearth\", hold);") !=
              std::string::npos, true);

    // DoHearth must stop a moving character before returning this refusal;
    // LeaveGuildDeathSpot then asks the decision again on a later poll.
    std::size_t const moving = source.find("if (who->isMoving())");
    std::size_t const stop = source.find(
        "HoldStillAndReport(who, ev.character, \"hearth\", ev.hold);", moving);
    std::size_t const refused = source.find("return refuse(\"character is moving\");", stop);
    Check("a moving hearth refusal holds still before it returns",
          moving != std::string::npos && stop != std::string::npos &&
              refused != std::string::npos && moving < stop && stop < refused,
          true);
    Check("a moving hearth refusal selects a bounded retry",
          DecideHearthOutRetry("character is moving", false, 0, 3) ==
              HearthOutRetry::Retry,
          true);
}


// wow-overseer#1514: mod-overseer#1144 stopped erasing the outmatched mark at
// cast start and erased it nowhere else, so a member whose hearth landed in
// the zone it died in, or one with no stone, was held still every poll for the
// whole window. The mark goes when the hearth read-back says Arrived, and a
// summon of the member (the guild moving it) frees it from the hold.
void TheMarkEndsOnArrival()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();

    std::size_t const resolve = source.find("void ResolveHearthChecks(uint32 elapsedMs)");
    std::size_t const arrived = source.find("case HearthOutcome::Arrived:", resolve);
    std::size_t const stayed = source.find("case HearthOutcome::Stayed:", arrived);
    std::size_t const erase = source.find("g_guildOutmatched.erase(", arrived);
    Check("an arrived hearth erases the outmatched mark",
          resolve != std::string::npos && arrived != std::string::npos &&
              stayed != std::string::npos && erase != std::string::npos && erase < stayed,
          true);

    std::size_t const spot = source.find("bool LeaveGuildDeathSpot(");
    std::size_t const end = source.find("\n    }\n", spot);
    std::string const body = source.substr(spot, end - spot);
    Check("the guild move is read in LeaveGuildDeathSpot",
          body.find("bool const guildMovedIt = SummonPendingFor(name);") != std::string::npos,
          true);
    Check("no keep-out in LeaveGuildDeathSpot is told the guild never moves it",
          body.find("outmatchedHere, false)") == std::string::npos, true);
    Check("a summon in flight is looked up by the summoned name",
          source.find("bool SummonPendingFor(std::string const& name) const") !=
              std::string::npos,
          true);
}

}  // namespace

int main()
{
    TheRetryDecision();
    TheKeepOutDecision();
    TheHearthThatCannotSucceed();
    TheAdapterIsWired();
    TheMarkEndsOnArrival();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_guild_death_spot_hearth_retry\n");
    return 0;
}
