/*
 * A guild finder run skips a boss the dungeon brain cannot reach, then ends as
 * stalled (#1086).
 *
 * The defect: DriveGuildRun only armed, paused and re-armed the dungeon brain.
 * Nothing ever sent `dc skip`, so a run stopped at a door or event the brain
 * cannot open waited out the 2 h ceiling and the 1 h progress extension. On
 * 2026-10-20 a Deadmines run sat at the Iron Clad Door from 14:12 to its
 * time-out at 16:01 while its tank said "Sort it and I'll continue, or `dc
 * skip`" three times, and three more runs timed out the same way.
 *
 * Compiles against the pure decision file.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

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

// An armed run, tank alive inside, nobody fighting, nothing held.
GuildRunStallFacts Quiet(unsigned secondsStill)
{
    GuildRunStallFacts f;
    f.armed = true;
    f.secondsStill = secondsStill;
    f.secondsSinceCredit = secondsStill;
    return f;
}

void AQuietRunIsSkipped()
{
    Check("no credit or movement for 20 min out of combat gives Skip",
          GuildRunStallNext(Quiet(20 * 60)) == GuildRunStallStep::Skip);
    Check("no credit or movement for 5 min with the brain silent gives Nothing",
          GuildRunStallNext(Quiet(5 * 60)) == GuildRunStallStep::Nothing);
    Check("the quiet window itself gives Skip",
          GuildRunStallNext(Quiet(GUILD_RUN_QUIET_SECONDS)) == GuildRunStallStep::Skip);
    Check("a second short of the quiet window gives Nothing",
          GuildRunStallNext(Quiet(GUILD_RUN_QUIET_SECONDS - 1)) == GuildRunStallStep::Nothing);

    GuildRunStallFacts f = Quiet(GUILD_RUN_STALL_SECONDS);
    f.brainStalled = true;
    Check("the brain saying it is stalled skips after the shorter window",
          GuildRunStallNext(f) == GuildRunStallStep::Skip);
    f.secondsStill = GUILD_RUN_STALL_SECONDS - 1;
    f.secondsSinceCredit = f.secondsStill;
    Check("...and not a second before it", GuildRunStallNext(f) == GuildRunStallStep::Nothing);
}

void AStalledBrainThatPacesIsSkipped()
{
    // "Opening the door to Gilnid" 15 times: the tank walks to the door and
    // back, so it moves, and nothing is credited.
    GuildRunStallFacts f = Quiet(60);
    f.brainStalled = true;
    f.secondsSinceCredit = GUILD_RUN_CREDITLESS_SECONDS;
    Check("the brain stalled with no credit for the creditless window gives Skip, moving or not",
          GuildRunStallNext(f) == GuildRunStallStep::Skip);
    f.brainStalled = false;
    Check("a moving run the brain does not call stalled gives Nothing",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);
}

void TheLadderEnds()
{
    GuildRunStallFacts f = Quiet(GUILD_RUN_QUIET_SECONDS);
    f.skips = 1;
    Check("one skip with no progress gives a second Skip",
          GuildRunStallNext(f) == GuildRunStallStep::Skip);
    f.skips = GUILD_RUN_MAX_SKIPS;
    Check("2 skips with no progress gives End", GuildRunStallNext(f) == GuildRunStallStep::End);
    Check("the budget is 2 skips", GUILD_RUN_MAX_SKIPS == 2);

    f.skips = 0;
    f.skipRefused = true;
    Check("a refused skip gives End at once", GuildRunStallNext(f) == GuildRunStallStep::End);
}

void ProgressResetsTheLadder()
{
    // The adapter zeroes `skips` and both clocks on a credit; what the
    // decision sees after a credit is a fresh, quiet run.
    GuildRunStallFacts f = Quiet(0);
    f.skips = 0;
    Check("credit after a skip resets the count: the next poll gives Nothing",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);
    f = Quiet(GUILD_RUN_QUIET_SECONDS);
    Check("...and the next stall starts at Skip again, not End",
          GuildRunStallNext(f) == GuildRunStallStep::Skip);
}

void BusyRunsAreLeftAlone()
{
    GuildRunStallFacts f = Quiet(GUILD_RUN_STALL_SECONDS);
    f.brainStalled = true;
    f.fighting = true;
    Check("combat gives Nothing, even with the brain stalled",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);
    f.secondsStill = GUILD_RUN_STALL_BUSY_SECONDS;
    f.secondsSinceCredit = f.secondsStill;
    Check("combat past the busy ceiling with no progress is not believed: Skip",
          GuildRunStallNext(f) == GuildRunStallStep::Skip);

    f = Quiet(GUILD_RUN_QUIET_SECONDS);
    f.held = true;
    Check("a held brain (rest or regroup) gives Nothing",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);

    f = Quiet(GUILD_RUN_QUIET_SECONDS);
    f.armed = false;
    Check("an unarmed brain or a tank not alive inside gives Nothing",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);

    f = Quiet(60);
    f.secondsSinceCredit = 60;
    f.brainStalled = true;
    Check("a recent kill gives Nothing even when the brain is stalled",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);
}

// A GROUP THAT SPLIT AND NEVER CLOSED UP ENDS (#1308). Zul'Farrak, 2026-11-01:
// 7 of 11 runs timed out, with two members at a camp and three at the door 440
// yards away. Each wipe reset the stillness clock, so no run reached the ladder.
void ASplitGroupEnds()
{
    GuildRunStallFacts f;
    f.secondsSplit = GUILD_RUN_SPLIT_END_SECONDS;
    Check("a split past its window ends with nobody armed",
          GuildRunStallNext(f) == GuildRunStallStep::EndSplit);
    f.held = true;
    Check("a brain the overseer holds does not hide the split",
          GuildRunStallNext(f) == GuildRunStallStep::EndSplit);
    f.secondsSplit = GUILD_RUN_SPLIT_END_SECONDS - 1;
    Check("a split inside its window is left alone",
          GuildRunStallNext(f) == GuildRunStallStep::Nothing);
}

void OnlyClosingUpClearsTheSplit()
{
    using OverseerDecisions::GuildRunSplitClock;
    using OverseerDecisions::GuildRunSplitReading;
    Check("a split starts the clock",
          GuildRunSplitClock(0, GuildRunSplitReading::Split, 100) == 100);
    Check("a split keeps its start",
          GuildRunSplitClock(100, GuildRunSplitReading::Split, 200) == 100);
    Check("a wipe keeps the clock",
          GuildRunSplitClock(100, GuildRunSplitReading::Unread, 200) == 100);
    Check("a wipe starts nothing",
          GuildRunSplitClock(0, GuildRunSplitReading::Unread, 200) == 0);
    Check("closing up clears it",
          GuildRunSplitClock(100, GuildRunSplitReading::Together, 200) == 0);
}

void TheVerdictHasAWord()
{
    Check("the stalled verdict is worded 'stalled'",
          std::strcmp(GuildRunVerdictWord(GuildRunVerdict::Stalled), "stalled") == 0);
    Check("timed out keeps its word",
          std::strcmp(GuildRunVerdictWord(GuildRunVerdict::TimedOut), "timed out") == 0);
}
}  // namespace

int main()
{
    AQuietRunIsSkipped();
    AStalledBrainThatPacesIsSkipped();
    TheLadderEnds();
    ProgressResetsTheLadder();
    BusyRunsAreLeftAlone();
    ASplitGroupEnds();
    OnlyClosingUpClearsTheSplit();
    TheVerdictHasAWord();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
