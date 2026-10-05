/*
 * A guild member that has died twice at one spot leaves it once it is alive,
 * by hearthstone, instead of fighting on beside what killed it.
 *
 * wow-overseer#597, measured on wow-dev 23:07 to 23:56 ET on 2026-10-04,
 * after mod-overseer#842: 311 guild deaths were repeats (same member, within
 * 60 yards and 15 minutes of its last death). Every recovery path fed them:
 * the corpse run, the spirit healer at a graveyard beside the killer, the
 * walk back from the graveyard, and the playerbots revive in place. 161 of
 * the 311 were a third or later death in one chain, so the member had
 * already died twice at the spot and was revived there again.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GuildDeathSpotStep;
using OverseerDecisions::GuildLeavesDeathSpot;

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
    Check("two deaths here with the stone ready hearths",
          GuildLeavesDeathSpot(2, 2, true, false) == GuildDeathSpotStep::Hearth);
    Check("five deaths here hearths too",
          GuildLeavesDeathSpot(5, 2, true, false) == GuildDeathSpotStep::Hearth);
    Check("one death here stays", GuildLeavesDeathSpot(1, 2, true, false) == GuildDeathSpotStep::Stay);
    Check("no deaths here stays", GuildLeavesDeathSpot(0, 2, true, false) == GuildDeathSpotStep::Stay);
    Check("a stone on cooldown stays",
          GuildLeavesDeathSpot(3, 2, false, false) == GuildDeathSpotStep::Stay);
    Check("in combat it fights first (the core refuses the cast)",
          GuildLeavesDeathSpot(3, 2, true, true) == GuildDeathSpotStep::Stay);
    Check("a zero limit never hearths", GuildLeavesDeathSpot(9, 0, true, false) == GuildDeathSpotStep::Stay);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the guild drive asks the decision",
          source.find("OverseerDecisions::GuildLeavesDeathSpot(") != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_guild_leaves_death_spot\n");
    return 0;
}
