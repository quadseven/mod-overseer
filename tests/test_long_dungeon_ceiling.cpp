/*
 * A long dungeon gets a ceiling that scales with its bosses, and a boss still
 * being fought when the ceiling hits gets a bounded extension (wow-overseer#835).
 *
 * The defect: GUILD_RUN_CEILING_SECONDS (7200 s) was flat for a row that ends
 * its dungeon. Blackfathom Deeps (map 48, seven bosses at about 30 minutes a
 * boss) ended `timed out` six times with no clear, the last at 7230 s with four
 * of seven down. Other dungeons keep 7200 s.
 *
 * Compiles against the pure decision file.
 */

#include "overseer_decisions.h"

#include <cstdio>

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

GuildRunPoll Inside()
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 5;
    return poll;
}

void ABlackfathomRunGetsTimeForSevenBosses()
{
    Check("Blackfathom, seven bosses, gets at least 14400 s",
          GuildRunCeilingForMap(48, true, 7) >= 14400);
    Check("exactly seven half hours and a margin",
          GuildRunCeilingForMap(48, true, 7) ==
              7 * 30 * 60 + GUILD_RUN_CEILING_MARGIN_SECONDS);

    GuildRunPoll p = Inside();
    p.bossesTotal = 7;
    p.bossesDone = 4;
    p.ceilingSeconds = GuildRunCeilingForMap(48, true, 7);
    p.secondsInside = 7230;
    Check("run 560 at 7230 s with four bosses is still running",
          GuildRunNext(p) == GuildRunVerdict::Running);
    p.secondsInside = p.ceilingSeconds;
    Check("and is timed out at the scaled ceiling",
          GuildRunNext(p) == GuildRunVerdict::TimedOut);
}

void EveryOtherDungeonKeepsItsCeiling()
{
    Check("Deadmines keeps 7200 s", GuildRunCeilingForMap(36, true, 6) == GUILD_RUN_CEILING_SECONDS);
    Check("Wailing Caverns keeps 7200 s",
          GuildRunCeilingForMap(43, true, 7) == GUILD_RUN_CEILING_SECONDS);
    Check("a map with no bosses read keeps 7200 s",
          GuildRunCeilingForMap(48, true, 0) == GUILD_RUN_CEILING_SECONDS);
    Check("a wing row keeps the per-boss rule",
          GuildRunCeilingForMap(230, false, 15) == GuildRunCeilingSeconds(false, 15));
}

void ABossInCombatBuysOneBoundedExtension()
{
    GuildRunPoll p = Inside();
    p.ceilingSeconds = GUILD_RUN_CEILING_SECONDS;
    p.secondsInside = GUILD_RUN_CEILING_SECONDS + 1;
    Check("past the ceiling with no boss in combat is timed out",
          GuildRunNext(p) == GuildRunVerdict::TimedOut);
    p.bossInCombat = true;
    Check("past the ceiling with a boss in combat is still running",
          GuildRunNext(p) == GuildRunVerdict::Running);
    p.secondsInside = GUILD_RUN_CEILING_SECONDS + GUILD_RUN_BOSS_EXTENSION_SECONDS - 1;
    Check("up to the end of the extension", GuildRunNext(p) == GuildRunVerdict::Running);
    p.secondsInside = GUILD_RUN_CEILING_SECONDS + GUILD_RUN_BOSS_EXTENSION_SECONDS;
    Check("the extension is bounded to 600 s", GUILD_RUN_BOSS_EXTENSION_SECONDS == 600);
    Check("and ends the run whatever is in combat", GuildRunNext(p) == GuildRunVerdict::TimedOut);
}
}  // namespace

int main()
{
    ABlackfathomRunGetsTimeForSevenBosses();
    EveryOtherDungeonKeepsItsCeiling();
    ABossInCombatBuysOneBoundedExtension();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
