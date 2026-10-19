/*
 * A guild run still killing bosses is not ended by the clock
 * (wow-overseer#835).
 *
 * The defect: past the ceiling only a boss in combat kept a run going, so a run
 * between bosses was timed out however recently it had killed. On 2026-10-19
 * two Deadmines runs ended `timed out` at 7200 s, both "last kill 2 min ago":
 * one at 5 of 7 heading to Captain Greenskin, one at 6 of 7 heading to Edwin
 * VanCleef, its last boss.
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

// A Deadmines run at the flat ceiling, everybody alive inside, between bosses.
GuildRunPoll DeadminesAtTheCeiling(unsigned bossesDone)
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 5;
    poll.bossesTotal = 7;
    poll.bossesDone = bossesDone;
    poll.ceilingSeconds = GuildRunCeilingForMap(36, true, 7);
    poll.secondsInside = poll.ceilingSeconds;
    return poll;
}

void ARunThatJustKilledABossGoesOn()
{
    GuildRunPoll p = DeadminesAtTheCeiling(6);
    p.bossKilled = true;
    p.secondsSinceKill = 2 * 60;
    Check("6 of 7 at the ceiling, last kill 2 min ago, is still running",
          GuildRunNext(p) == GuildRunVerdict::Running);

    p = DeadminesAtTheCeiling(5);
    p.bossKilled = true;
    p.secondsSinceKill = 2 * 60;
    p.secondsInside = p.ceilingSeconds + 30;
    Check("5 of 7 just past the ceiling, last kill 2 min ago, is still running",
          GuildRunNext(p) == GuildRunVerdict::Running);

    p.secondsSinceKill = GUILD_RUN_PROGRESS_WINDOW_SECONDS - 1;
    Check("up to the end of the progress window",
          GuildRunNext(p) == GuildRunVerdict::Running);
}

void AStalledRunIsStillTimedOut()
{
    GuildRunPoll p = DeadminesAtTheCeiling(3);
    Check("no kill this stay is timed out at the ceiling",
          GuildRunNext(p) == GuildRunVerdict::TimedOut);

    p.bossKilled = true;
    p.secondsSinceKill = GUILD_RUN_PROGRESS_WINDOW_SECONDS;
    Check("a last kill a full window ago is timed out",
          GuildRunNext(p) == GuildRunVerdict::TimedOut);

    p.secondsSinceKill = 95 * 60;
    Check("a last kill 95 min ago is timed out", GuildRunNext(p) == GuildRunVerdict::TimedOut);
}

void TheProgressExtensionIsBounded()
{
    GuildRunPoll p = DeadminesAtTheCeiling(6);
    p.bossKilled = true;
    p.secondsSinceKill = 60;
    p.secondsInside = p.ceilingSeconds + GUILD_RUN_PROGRESS_EXTENSION_SECONDS - 1;
    Check("a run still killing goes on to the end of the extension",
          GuildRunNext(p) == GuildRunVerdict::Running);
    p.secondsInside = p.ceilingSeconds + GUILD_RUN_PROGRESS_EXTENSION_SECONDS;
    Check("and is timed out there however recent its last kill",
          GuildRunNext(p) == GuildRunVerdict::TimedOut);
    Check("the window is 30 min", GUILD_RUN_PROGRESS_WINDOW_SECONDS == 30 * 60);
    Check("the extension is 60 min", GUILD_RUN_PROGRESS_EXTENSION_SECONDS == 60 * 60);
}

void TheVerdictOrderStands()
{
    GuildRunPoll p = DeadminesAtTheCeiling(7);
    p.bossKilled = true;
    p.secondsSinceKill = 0;
    Check("every boss down is cleared, not running", GuildRunNext(p) == GuildRunVerdict::Cleared);

    p = DeadminesAtTheCeiling(6);
    p.bossKilled = true;
    p.secondsSinceKill = 60;
    p.groupGone = true;
    Check("a gone group is abandoned whatever its last kill",
          GuildRunNext(p) == GuildRunVerdict::Abandoned);
}
}  // namespace

int main()
{
    ARunThatJustKilledABossGoesOn();
    AStalledRunIsStillTimedOut();
    TheProgressExtensionIsBounded();
    TheVerdictOrderStands();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
