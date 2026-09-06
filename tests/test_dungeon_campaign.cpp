/*
 * What counts as a run of a dungeon campaign (#225).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter decides how a run ended and writes the word down; this
 * test pins what that word means for the campaign's count, for the stop that
 * bounds a campaign whose runs keep failing before entry, and for the
 * arithmetic on the last slot, which is where the measured defect was.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonCampaignAfterRun;
using OverseerDecisions::DungeonCampaignProgress;
using OverseerDecisions::DungeonRunEnteredTheInstance;
using OverseerDecisions::DungeonRunTrailingFailures;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckCount(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, want %u\n", what, got, want);
    ++failures;
}

void OnlyTheTwoPreEntryFailuresAreNotRuns()
{
    Check("reset_failed", DungeonRunEnteredTheInstance("reset_failed"), false);
    Check("staging_failed", DungeonRunEnteredTheInstance("staging_failed"), false);
}

void EveryOutcomeWrittenAboutAPartyInsideIsARun()
{
    Check("left", DungeonRunEnteredTheInstance("left"), true);
    Check("stalled", DungeonRunEnteredTheInstance("stalled"), true);
    Check("wipe", DungeonRunEnteredTheInstance("wipe"), true);
    Check("emptied", DungeonRunEnteredTheInstance("emptied"), true);
    // The cold-heartbeat close leaves no outcome, and that row exists only
    // because the arming drive saw somebody on the instance map.
    Check("empty outcome", DungeonRunEnteredTheInstance(""), true);
    // A word this table has never heard of counts, on the reasoning the
    // header gives: the vocabulary grows toward endings of real runs, and
    // 'complete' is already named as the next one.
    Check("complete", DungeonRunEnteredTheInstance("complete"), true);
    Check("unknown word", DungeonRunEnteredTheInstance("something_new"), true);
}

void TheStreakStopsAtTheFirstRunThatHappened()
{
    CheckCount("no rows at all", DungeonRunTrailingFailures({}), 0u);
    CheckCount("newest is a real run",
               DungeonRunTrailingFailures({"left", "staging_failed", "reset_failed"}), 0u);
    CheckCount("one failure then a run",
               DungeonRunTrailingFailures({"staging_failed", "left", "reset_failed"}), 1u);
    // The widening #225 needed: a streak of BOTH kinds of pre-entry failure is
    // one streak, because the campaign is equally stuck either way.
    CheckCount("mixed streak",
               DungeonRunTrailingFailures({"staging_failed", "reset_failed",
                                           "staging_failed", "left"}), 3u);
    CheckCount("every row a failure",
               DungeonRunTrailingFailures({"reset_failed", "reset_failed",
                                           "reset_failed"}), 3u);
    // A wipe is a run that happened. It is a bad run, not an absent one, and
    // it must not be read as the instance refusing to start.
    CheckCount("a wipe breaks the streak",
               DungeonRunTrailingFailures({"staging_failed", "wipe", "staging_failed"}), 1u);
}

void ARunThatEnteredFillsItsSlot()
{
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("left", 4, 100, true);
    Check("counted", p.counted, true);
    CheckCount("runsDone", p.runsDone, 4u);
    CheckCount("nextRunNumber", p.nextRunNumber, 5u);
    Check("campaignOver", p.campaignOver, false);
}

void ARunThatNeverGotInsideLeavesItsSlotEmpty()
{
    // THE MEASURED DEFECT, in one assertion: run 4 of 100 failed at the
    // barrier, and the campaign is still waiting for a run 4.
    DungeonCampaignProgress const p =
        DungeonCampaignAfterRun("staging_failed", 4, 100, true);
    Check("counted", p.counted, false);
    CheckCount("runsDone", p.runsDone, 3u);
    CheckCount("nextRunNumber", p.nextRunNumber, 4u);
    Check("campaignOver", p.campaignOver, false);

    DungeonCampaignProgress const reset =
        DungeonCampaignAfterRun("reset_failed", 1, 100, true);
    Check("reset counted", reset.counted, false);
    CheckCount("reset runsDone", reset.runsDone, 0u);
    CheckCount("reset nextRunNumber", reset.nextRunNumber, 1u);
}

void TheLastSlotIsWhereTheCountUsedToLie()
{
    DungeonCampaignProgress const done =
        DungeonCampaignAfterRun("left", 100, 100, true);
    CheckCount("last run done", done.runsDone, 100u);
    Check("last run ends the campaign", done.campaignOver, true);
    CheckCount("nothing next", done.nextRunNumber, 0u);

    // The same slot, failed. A straight `runNumber >= wanted` would call this
    // campaign finished on ninety-nine dungeons.
    DungeonCampaignProgress const failed =
        DungeonCampaignAfterRun("staging_failed", 100, 100, true);
    CheckCount("last slot still empty", failed.runsDone, 99u);
    Check("campaign is not over", failed.campaignOver, false);
    CheckCount("the last slot is next again", failed.nextRunNumber, 100u);
}

void AnUnknownCapNeverEndsACampaign()
{
    // A database that cannot answer is not a campaign that is finished. The
    // caller has its own branch for this and must not be told the count ran
    // out instead.
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("left", 7, 0, false);
    Check("counted", p.counted, true);
    CheckCount("runsDone", p.runsDone, 7u);
    Check("not over", p.campaignOver, false);
    CheckCount("next", p.nextRunNumber, 8u);
}

void AnUnnumberedRunDoesNotWrapTheCount()
{
    // An adopted run whose row was never stamped reads back as run 0. Zero
    // minus one is the whole campaign on an unsigned, which is exactly the
    // kind of arithmetic this function exists to hold in one place.
    DungeonCampaignProgress const p =
        DungeonCampaignAfterRun("staging_failed", 0, 100, true);
    CheckCount("runsDone", p.runsDone, 0u);
    CheckCount("nextRunNumber", p.nextRunNumber, 1u);
    Check("not over", p.campaignOver, false);
}

void AZeroCapStartsNothing()
{
    // dungeon_runs_wanted = 0 is the operator stopping the campaign outright,
    // and the roster migration says so. A run that somehow ended anyway must
    // not report room for another.
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("left", 1, 0, true);
    Check("over", p.campaignOver, true);
    CheckCount("nothing next", p.nextRunNumber, 0u);
}

} // namespace

int main()
{
    OnlyTheTwoPreEntryFailuresAreNotRuns();
    EveryOutcomeWrittenAboutAPartyInsideIsARun();
    TheStreakStopsAtTheFirstRunThatHappened();
    ARunThatEnteredFillsItsSlot();
    ARunThatNeverGotInsideLeavesItsSlotEmpty();
    TheLastSlotIsWhereTheCountUsedToLie();
    AnUnknownCapNeverEndsACampaign();
    AnUnnumberedRunDoesNotWrapTheCount();
    AZeroCapStartsNothing();
    return failures ? 1 : 0;
}
