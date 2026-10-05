/*
 * A guild member that may not use the stuck-revival ladder still leaves a
 * death loop: after repeat deaths it takes the spirit healer, not the corpse
 * run back to its killer (wow-overseer#586: half of 912 guild deaths an hour
 * were repeat deaths at the same corpse).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GhostRecovery;
using OverseerDecisions::GuildGhostFallback;

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

void TheDecision()
{
    Check("a second death here takes the spirit healer",
          GuildGhostFallback(2, true, 2) == GhostRecovery::SpiritHealer);
    Check("a third does too", GuildGhostFallback(3, true, 2) == GhostRecovery::SpiritHealer);
    Check("a first death runs the corpse", GuildGhostFallback(1, true, 2) == GhostRecovery::CorpseRun);
    Check("no graveyard known runs the corpse",
          GuildGhostFallback(3, false, 2) == GhostRecovery::CorpseRun);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("a caller without the ladder uses the fallback",
          source.find("OverseerDecisions::GuildGhostFallback(") != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_guild_ghost_fallback\n");
    return 0;
}
