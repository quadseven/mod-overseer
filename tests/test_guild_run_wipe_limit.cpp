/*
 * A guild run that keeps wiping with no boss down is ended, not pulled again.
 *
 * The live failure this pins: on the dev realm on 2026-11-18 a level 60 guild
 * run in Scholomance (run 6257) wiped five times between 01:43 and 02:22 and
 * a sixth at 02:26, 28 deaths in 85 minutes, with one boss down and the
 * dungeon brain pulling again after every corpse run. Nothing ended it: the
 * verdict reads a wipe only when nobody is back inside within the recovery
 * window (every wipe came back within it), the module's no-kill watchdog
 * (30 minutes) is re-armed by every cleared room, and the run's ceiling is two
 * hours. A run that wipes GUILD_RUN_WIPE_LIMIT times without a boss credit in
 * between is over: the verdict is Wiped, and a boss credit starts the count
 * again.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GUILD_RUN_WIPE_LIMIT;
using OverseerDecisions::GuildRunNext;
using OverseerDecisions::GuildRunPoll;
using OverseerDecisions::GuildRunVerdict;
using OverseerDecisions::GuildRunVerdictWord;

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

void Expect(char const* what, GuildRunPoll const& poll, GuildRunVerdict want)
{
    GuildRunVerdict const got = GuildRunNext(poll);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, GuildRunVerdictWord(got),
                GuildRunVerdictWord(want));
    ++failures;
}

// Run 6257 mid-run: five level 60 members alive inside, 1 of 13 bosses down.
GuildRunPoll Run6257()
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 5;
    poll.secondsInside = 5000;
    poll.bossesDone = 1;
    poll.bossesTotal = 13;
    return poll;
}

void TheLimitIsFiveWipes()
{
    Check("the limit is five wipes", GUILD_RUN_WIPE_LIMIT == 5);
}

void FiveWipesWithNoBossEndsTheRun()
{
    GuildRunPoll poll = Run6257();
    poll.wipesSinceKill = GUILD_RUN_WIPE_LIMIT - 1;
    Expect("four wipes is still a run that is learning", poll, GuildRunVerdict::Running);

    poll.wipesSinceKill = GUILD_RUN_WIPE_LIMIT;
    Expect("the fifth wipe ends the run, raised or not", poll, GuildRunVerdict::Wiped);

    // The fifth wipe is the corpse run in flight: nobody inside, five ghosts.
    GuildRunPoll ghosts = Run6257();
    ghosts.inside = 0;
    ghosts.aliveInside = 0;
    ghosts.ghostsComingBack = 5;
    ghosts.secondsRecovering = 30;
    ghosts.secondsEmpty = 30;
    ghosts.wipesSinceKill = GUILD_RUN_WIPE_LIMIT;
    Expect("it ends at once, without waiting out the recovery window", ghosts,
           GuildRunVerdict::Wiped);
}

void AClearedRunIsClearedWhateverItsWipes()
{
    GuildRunPoll poll = Run6257();
    poll.wipesSinceKill = GUILD_RUN_WIPE_LIMIT + 3;
    poll.bossesDone = 13;
    poll.finderFinished = true;
    Expect("a run whose last boss died is cleared first", poll, GuildRunVerdict::Cleared);
}

void ANoLimitMeansOff()
{
    GuildRunPoll poll = Run6257();
    poll.wipesSinceKill = 40;
    poll.wipeLimit = 0;
    Expect("a limit of 0 turns it off", poll, GuildRunVerdict::Running);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    auto has = [&](char const* what, char const* needle)
    { Check(what, source.find(needle) != std::string::npos); };
    has("the run counts its wipes", "++run.wipesSinceKill;");
    has("a boss credit starts the count again", "run.wipesSinceKill = 0;");
    has("the poll carries the count", "poll.wipesSinceKill = run.wipesSinceKill;");
    has("the wipe that ends it is said", "overseer: guild run {} wipe {} of {}");
    has("the ending says why", "\" times with no boss down\"");
}

}  // namespace

int main()
{
    TheLimitIsFiveWipes();
    FiveWipesWithNoBossEndsTheRun();
    AClearedRunIsClearedWhateverItsWipes();
    ANoLimitMeansOff();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild run wipe limit: all checks passed\n");
    return 0;
}
