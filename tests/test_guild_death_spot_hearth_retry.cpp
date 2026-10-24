/*
 * A guild death-spot hearth-out refused because the character was moving is
 * retried with the character held still, bounded; and a member whose hearth-out
 * failed is kept out of the zone it died in.
 *
 * MEASURED ON THE DEV REALM, 2026-10-24. Highlights, level 31, died 113 times
 * in 24 hours at the gate of Ironforge, killed by level 75 guards. Every cycle
 * the hearth-out was refused "character is moving", the hold placed by the
 * executor was never re-asserted, and the member walked back and died again.
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
    Check("the death spot keep-out decision is wired",
          source.find("OverseerDecisions::KeepOutOfDeathZone(") != std::string::npos, true);
}

}  // namespace

int main()
{
    TheRetryDecision();
    TheKeepOutDecision();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_guild_death_spot_hearth_retry\n");
    return 0;
}
