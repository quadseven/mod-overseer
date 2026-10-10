/*
 * A guild member killed by something far above its level leaves that ground
 * after the FIRST such death, by hearthstone, rather than after the second
 * death at one spot.
 *
 * MEASURED ON THE DEV REALM, 2026-10-09 AND 10. Whakk, level 20, died in
 * Searing Gorge at 23:20 ET to a level 45 spider and hearthed out at 23:43
 * after seven deaths there. Mok, level 22, died there 152 times in 22 hours.
 * The death-spot hearth (wow-overseer#597) waits for two deaths within 60
 * yards in ten minutes, and in ground like this the member revives at a
 * graveyard hundreds of yards from its corpse, so the deaths rarely fall
 * within 60 yards of each other: the graveyard, the corpse, the walk between.
 *
 * A player of level 20 killed by a level 45 does not try again. Killed by
 * something ten levels over itself (the `??` con the game draws), a member
 * hearths out as soon as it stands alive in that zone with its stone ready.
 * The Gray Bears of Hillsbrad (21 and 22) killing members of 20 to 28 are
 * not that and keep the old rule.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings, plus a read of the module source for the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

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

void WhatOutmatchedMeans()
{
    Check("a level 45 spider outmatches a level 20", OutmatchedDeath(20, 45));
    Check("ten levels over is the `??` con", OutmatchedDeath(20, 30));
    Check("nine is a fight a player might lose", !OutmatchedDeath(20, 29));
    Check("a Gray Bear (22) does not outmatch a level 23", !OutmatchedDeath(23, 22));
    Check("an unknown killer level is no evidence", !OutmatchedDeath(20, 0));
}

void TheMarkStandsForTheZone()
{
    GuildOutmatchedMark mark;
    mark.at = 1000;
    mark.mapId = 0;
    mark.zoneId = 51;
    mark.memberLevel = 20;
    mark.killerLevel = 45;
    Check("alive in the gorge after it", OutmatchedHere(mark, 1000 + 120, 0, 51,
                                                        GUILD_OUTMATCHED_WINDOW_MINUTES));
    Check("still there when the stone comes off cooldown",
          OutmatchedHere(mark, 1000 + 40 * 60, 0, 51, GUILD_OUTMATCHED_WINDOW_MINUTES));
    Check("home in Dun Morogh is not the gorge",
          !OutmatchedHere(mark, 1000 + 120, 0, 1, GUILD_OUTMATCHED_WINDOW_MINUTES));
    Check("another map is not the gorge",
          !OutmatchedHere(mark, 1000 + 120, 1, 51, GUILD_OUTMATCHED_WINDOW_MINUTES));
    Check("an old death is forgotten",
          !OutmatchedHere(mark, 1000 + GUILD_OUTMATCHED_WINDOW_MINUTES * 60 + 1, 0, 51,
                          GUILD_OUTMATCHED_WINDOW_MINUTES));
    GuildOutmatchedMark none;
    Check("no mark, nothing to leave", !OutmatchedHere(none, 1000, 0, 0,
                                                       GUILD_OUTMATCHED_WINDOW_MINUTES));
}

void OneDeathIsEnough()
{
    Check("one outmatched death and the stone ready hearths",
          GuildLeavesDeathSpot(0, 2, true, false, true) == GuildDeathSpotStep::Hearth);
    Check("...wherever in the zone it revived",
          GuildLeavesDeathSpot(1, 2, true, false, true) == GuildDeathSpotStep::Hearth);
    Check("a stone on cooldown waits for the stone",
          GuildLeavesDeathSpot(0, 2, false, false, true) == GuildDeathSpotStep::Stay);
    Check("in combat it fights first (the core refuses the cast)",
          GuildLeavesDeathSpot(0, 2, true, true, true) == GuildDeathSpotStep::Stay);
    Check("an ordinary death still waits for the second",
          GuildLeavesDeathSpot(1, 2, true, false, false) == GuildDeathSpotStep::Stay);
    Check("...and two still hearth",
          GuildLeavesDeathSpot(2, 2, true, false, false) == GuildDeathSpotStep::Hearth);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the death hook asks whether the killer outmatched the member",
          source.find("OverseerDecisions::OutmatchedDeath(") != std::string::npos);
    Check("the guild drive asks whether it still stands there",
          source.find("OverseerDecisions::OutmatchedHere(") != std::string::npos);
    Check("the deploy proof is said",
          source.find("far above its own level") != std::string::npos);
}

}  // namespace

int main()
{
    WhatOutmatchedMeans();
    TheMarkStandsForTheZone();
    OneDeathIsEnough();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_guild_outmatched_death\n");
    return 0;
}
