/*
 * What a run that got inside and never got inside TOGETHER costs a campaign
 * (#384).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter is what notices that STAGED_INSIDE has stopped assembling -
 * it ratchets on how many members the census finds on the instance map and
 * closes the run 'split_failed' when that number has not moved for a quarter of
 * an hour. What is pinned here is the accounting consequence of that word,
 * which is the half a wrong answer would hide: whether the attempt fills a slot
 * in a campaign of a hundred, and whether a party that keeps splitting on the
 * same door is ever stopped.
 *
 * The measurement it exists for: a run adopted at STAGED_INSIDE with one of five
 * members inside sat 'active' for over 36 minutes. Four members were inside and
 * had not moved one yard across samples sixteen minutes apart; the fifth had
 * taken environmental damage inside the instance and released to a graveyard on
 * the outdoor map about 2200 yards from the door, and nothing was going to bring
 * it back. The census could not reach five by any path, and nothing anywhere
 * would have ended that run.
 *
 * The trap this is really guarding is the one #225 already paid for once. A
 * failure that spends a campaign slot is bounded by the cap and reports a
 * finished campaign having cleared nothing; a failure that spends no slot is
 * bounded by nothing at all unless the consecutive-failure stop can see it.
 * 'split_failed' has to be on exactly one side of that line, and it is the same
 * side 'reset_failed' and 'staging_failed' are on.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonCampaignAfterRun;
using OverseerDecisions::DungeonCampaignProgress;
using OverseerDecisions::DungeonCampaignStopsOnFailures;
using OverseerDecisions::DungeonRunEnteredTheInstance;
using OverseerDecisions::DungeonRunTrailingFailures;

namespace
{

int failures = 0;

// The adapter's own DUNGEON_CAMPAIGN_CONSECUTIVE_FAILURES. Written here rather
// than imported, for the reason the sibling ceiling test gives: this is about
// the shape of the rule and must keep meaning the same thing if the adapter
// retunes its number.
constexpr unsigned STOP_AFTER = 3;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckUnsigned(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void ASplitRunIsNotARun()
{
    // THE ONE ASSERTION EVERYTHING ELSE HERE RESTS ON. A run that never got the
    // party inside together was never handed to the clearing drive, never armed
    // the dungeon brain and cleared nothing, so it did not happen.
    CheckBool("split_failed did not enter",
              DungeonRunEnteredTheInstance("split_failed"), false);

    // Its two older siblings are unchanged, which is worth asserting beside it:
    // widening a closed set is exactly the edit that quietly changes the
    // members already in it.
    CheckBool("reset_failed did not enter",
              DungeonRunEnteredTheInstance("reset_failed"), false);
    CheckBool("staging_failed did not enter",
              DungeonRunEnteredTheInstance("staging_failed"), false);

    // And so are the endings of runs that really happened. 'stalled' is the
    // interesting one to keep beside 'split_failed': both are runs that went
    // nowhere, and only one of them had a party inside doing it.
    CheckBool("left entered", DungeonRunEnteredTheInstance("left"), true);
    CheckBool("stalled entered", DungeonRunEnteredTheInstance("stalled"), true);
    CheckBool("wipe entered", DungeonRunEnteredTheInstance("wipe"), true);
    CheckBool("emptied entered", DungeonRunEnteredTheInstance("emptied"), true);
    CheckBool("complete entered", DungeonRunEnteredTheInstance("complete"), true);

    // An unknown word still means "it entered", which is the direction that
    // loses a campaign's progress least often - see the header's argument. A
    // near miss on the new word is the case that would be silently wrong if
    // anybody ever wrote the outcome twice, so it is asserted rather than
    // assumed.
    CheckBool("an unknown outcome entered", DungeonRunEnteredTheInstance("split"),
              true);
    CheckBool("an empty outcome entered", DungeonRunEnteredTheInstance(""), true);
}

void ASplitRunDoesNotFillTheSlotItWasAimingAt()
{
    // Run 2 of a campaign of 30 split and was closed. The slot it was aiming at
    // is not filled, so the campaign still stands at one run done and the next
    // attempt is run 2 again. This is #225's arithmetic and the whole reason
    // the word has to be on the unentered side of the line: the alternative is
    // a campaign of a hundred that finishes in a day having cleared nothing.
    DungeonCampaignProgress const split =
        DungeonCampaignAfterRun("split_failed", 2, 30, true);
    CheckBool("a split run counts", split.counted, false);
    CheckUnsigned("runs done after a split", split.runsDone, 1);
    CheckUnsigned("the next run is the same number", split.nextRunNumber, 2);
    CheckBool("the campaign is not over", split.campaignOver, false);

    // The same run, had it actually been driven to an ending, does fill it.
    DungeonCampaignProgress const real =
        DungeonCampaignAfterRun("left", 2, 30, true);
    CheckBool("a real run counts", real.counted, true);
    CheckUnsigned("runs done after a real run", real.runsDone, 2);
    CheckUnsigned("the next run is the one after", real.nextRunNumber, 3);

    // THE LAST SLOT, which is the trap DungeonCampaignAfterRun was written for.
    // Run 30 of 30 splits: the campaign must not report itself finished with
    // twenty-nine dungeons actually cleared.
    DungeonCampaignProgress const lastSlot =
        DungeonCampaignAfterRun("split_failed", 30, 30, true);
    CheckUnsigned("runs done after the last slot split", lastSlot.runsDone, 29);
    CheckBool("the campaign is not declared finished", lastSlot.campaignOver, false);
    CheckUnsigned("and it tries that slot again", lastSlot.nextRunNumber, 30);
}

void APartyThatKeepsSplittingIsStopped()
{
    // The rows come back newest first, exactly as the adapter's
    // `ORDER BY id DESC LIMIT n` returns them.
    std::vector<std::string> const three{"split_failed", "split_failed",
                                         "split_failed"};
    CheckUnsigned("three splits in a row", DungeonRunTrailingFailures(three), 3);
    CheckBool("and the campaign stops",
              DungeonCampaignStopsOnFailures(DungeonRunTrailingFailures(three),
                                             STOP_AFTER),
              true);

    // MIXED WITH THE OLDER TWO, because the streak is about "never got inside
    // together" and not about one particular way of failing at it. A door that
    // strands a member on every attempt can perfectly well fail a reset once in
    // the middle of doing it.
    std::vector<std::string> const mixed{"split_failed", "staging_failed",
                                         "reset_failed"};
    CheckUnsigned("a mixed streak still counts three",
                  DungeonRunTrailingFailures(mixed), 3);
    CheckBool("and still stops",
              DungeonCampaignStopsOnFailures(DungeonRunTrailingFailures(mixed),
                                             STOP_AFTER),
              true);

    // ONE GOOD RUN BREAKS THE STREAK, which is the property that keeps this
    // from stopping a campaign that is mostly working. The newest row is a run
    // that entered, so the count is zero however bad the two behind it were.
    std::vector<std::string> const recovered{"left", "split_failed",
                                             "split_failed"};
    CheckUnsigned("a run that entered ends the streak",
                  DungeonRunTrailingFailures(recovered), 0);
    CheckBool("so nothing is stopped",
              DungeonCampaignStopsOnFailures(DungeonRunTrailingFailures(recovered),
                                             STOP_AFTER),
              false);

    // TWO IS NOT THREE. The bound is the third failure, not the fourth and not
    // the second, and a campaign that has split twice is still allowed the
    // attempt that might work.
    std::vector<std::string> const two{"split_failed", "split_failed", "wipe"};
    CheckUnsigned("two splits behind a real run",
                  DungeonRunTrailingFailures(two), 2);
    CheckBool("two does not stop a campaign",
              DungeonCampaignStopsOnFailures(DungeonRunTrailingFailures(two),
                                             STOP_AFTER),
              false);
}

} // namespace

int main()
{
    ASplitRunIsNotARun();
    ASplitRunDoesNotFillTheSlotItWasAimingAt();
    APartyThatKeepsSplittingIsStopped();
    return failures ? 1 : 0;
}
