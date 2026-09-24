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
using OverseerDecisions::DungeonCampaignRecovers;
using OverseerDecisions::DungeonRunCountsAsDone;
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

void OnlyTheClosedSetOfOutcomesThatClearedNothingIsNotARun()
{
    Check("reset_failed", DungeonRunEnteredTheInstance("reset_failed"), false);
    Check("staging_failed", DungeonRunEnteredTheInstance("staging_failed"), false);
    // The party crossed but the census never reached everybody (#384), so the
    // run was never handed to the clearing drive.
    Check("split_failed", DungeonRunEnteredTheInstance("split_failed"), false);
    // And the party was inside, with the run under way, and walked straight
    // back out because a member had stopped being able to loot (#429). The bar
    // was never geography for its own sake: a run that cleared nothing did not
    // fill a slot. Measured at six seconds inside map 189, twice in a row, each
    // one spending a slot of a twenty-five run campaign.
    Check("evacuated", DungeonRunEnteredTheInstance("evacuated"), false);
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

void OnlyAnExplicitClearCountsAsCampaignProgress()
{
    Check("complete counts", DungeonRunCountsAsDone("complete"), true);
    Check("left does not count", DungeonRunCountsAsDone("left"), false);
    Check("split_failed does not count", DungeonRunCountsAsDone("split_failed"), false);
    Check("staging_failed does not count", DungeonRunCountsAsDone("staging_failed"), false);
    Check("reset_failed does not count", DungeonRunCountsAsDone("reset_failed"), false);
    Check("emptied does not count", DungeonRunCountsAsDone("emptied"), false);
    Check("stalled does not count", DungeonRunCountsAsDone("stalled"), false);
    Check("wipe does not count", DungeonRunCountsAsDone("wipe"), false);
    Check("evacuated does not count", DungeonRunCountsAsDone("evacuated"), false);
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

// What the streak above is counted FOR. It used to be a stop: three in a row
// ended the campaign and asked the operator to restart it by hand. The operator
// restarted campaigns that way several times on 2026-09-23 and ordered that it
// never be needed again, so any streak now recovers and no streak stops.
void NoStreakStopsTheCampaign()
{
    Check("one failure recovers", DungeonCampaignRecovers(1), true);
    Check("three in a row recovers", DungeonCampaignRecovers(3), true);
    // MEASURED 2026-09-07: seven consecutive staging failures. That campaign
    // now recovers seven times rather than stopping at the third.
    std::vector<std::string> const seven(7, "staging_failed");
    Check("seven in a row still recovers",
          DungeonCampaignRecovers(DungeonRunTrailingFailures(seven)), true);
    // The one boundary, in both directions and in the caller's own shape.
    Check("no failures, nothing to recover",
          DungeonCampaignRecovers(DungeonRunTrailingFailures({"complete"})), false);
    Check("the first failure recovers",
          DungeonCampaignRecovers(DungeonRunTrailingFailures({"staging_failed", "complete"})),
          true);
}

void TheStreakReadsRealRowsTheWayTheCallerHandsThemOver()
{
    std::vector<std::string> const recovered = {"staging_failed", "wipe",
                                                "staging_failed"};
    CheckCount("a run that happened clears the streak to the newest failure",
               DungeonRunTrailingFailures(recovered), 1u);
    std::vector<std::string> const mixed = {"reset_failed", "staging_failed",
                                            "reset_failed"};
    Check("a mixed streak recovers",
          DungeonCampaignRecovers(DungeonRunTrailingFailures(mixed)), true);
    Check("an empty table starts a campaign with nothing to recover",
          DungeonCampaignRecovers(DungeonRunTrailingFailures({})), false);
}

void ARunThatEnteredFillsItsSlot()
{
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("complete", 4, 100, true);
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
        DungeonCampaignAfterRun("complete", 100, 100, true);
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
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("complete", 7, 0, false);
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
    DungeonCampaignProgress const p = DungeonCampaignAfterRun("complete", 1, 0, true);
    Check("over", p.campaignOver, true);
    CheckCount("nothing next", p.nextRunNumber, 0u);
}

} // namespace

int main()
{
    OnlyTheClosedSetOfOutcomesThatClearedNothingIsNotARun();
    EveryOutcomeWrittenAboutAPartyInsideIsARun();
    OnlyAnExplicitClearCountsAsCampaignProgress();
    TheStreakStopsAtTheFirstRunThatHappened();
    NoStreakStopsTheCampaign();
    TheStreakReadsRealRowsTheWayTheCallerHandsThemOver();
    ARunThatEnteredFillsItsSlot();
    ARunThatNeverGotInsideLeavesItsSlotEmpty();
    TheLastSlotIsWhereTheCountUsedToLie();
    AnUnknownCapNeverEndsACampaign();
    AnUnnumberedRunDoesNotWrapTheCount();
    AZeroCapStartsNothing();
    return failures ? 1 : 0;
}
