/*
 * The release gate must not fence off an aim this book claimed itself (#822).
 *
 * The live failure this pins. In Scarlet Monastery (map 189) three characters
 * were sent to `repair`, found no such spawn there, and released the errand -
 * and TravelAimBook::ReleaseImpl skipped the column write every time, because
 * TravelReleaseFence answers "foreign" for the four counter keywords and the
 * gate asked it even when the book itself had claimed the aim standing in the
 * column. The column kept the stale `repair`, the release erased the claim and
 * the drive state, and the next poll re-read the same aim as a new errand:
 * sent to repair, no spawn, release, skip the write - forever.
 *
 * The gate's own contract (on TravelReleaseFence) has always said the write is
 * skipped "for an aim it never claimed". The fence protects genuinely foreign
 * aims - a repair/vendor/banker/auctioneer aim the bridge wrote, a positional
 * aim another pass is walking, a column that moved after this book's claim.
 * A claimed aim whose column has not moved since is this book's errand ending,
 * and the column write goes through.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::TravelReleaseGate;
using OverseerDecisions::TravelReleaseGateFacts;

namespace
{

int failures = 0;

void Check(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: fence='%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

TravelReleaseGateFacts Facts(bool claimed, bool changedSinceClaim,
                             uint32_t learnSkill, char const* standing)
{
    TravelReleaseGateFacts facts;
    facts.claimed = claimed;
    facts.changedSinceClaim = changedSinceClaim;
    facts.learnSkill = learnSkill;
    facts.standing = standing;
    return facts;
}

// THE DEFECT ITSELF (#822). A self-claimed `repair` aim, column unchanged
// since the claim, must clear: the fence is for aims this book never claimed,
// and skipping the write here is what reborn the same stale errand every poll
// in Scarlet Monastery.
void ASelfClaimedRepairAimIsCleared()
{
    Check("a self-claimed repair aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "repair")), "");
    Check("a self-claimed vendor aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "vendor")), "");
    Check("a self-claimed banker aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "banker")), "");
    Check("a self-claimed auctioneer aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "auctioneer")), "");
    // The positional half of the same defect: the book claims its own `at:`
    // aims (gathering, vault, catch-up), and ending one must clear it.
    Check("a self-claimed positional aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "at:1:-1175.1,-2532.8,123.9")), "");
    // A claimed keyword aim the book may always clear stays clear.
    Check("a self-claimed trainer aim raises no fence",
          TravelReleaseGate(Facts(true, false, 0, "trainer")), "");
}

// THE HALF THAT ALREADY WORKED KEEPS WORKING. A genuinely foreign aim - one
// this book never claimed - stays protected, or the fix has traded the repair
// loop for erased economy errands.
void AGenuinelyForeignAimStaysProtected()
{
    char const* const keywords[] = { "vendor", "banker", "repair", "auctioneer" };
    for (char const* aim : keywords)
    {
        std::string const fence = TravelReleaseGate(Facts(false, false, 0, aim));
        if (fence.empty())
        {
            std::printf("FAIL '%s': an unclaimed counter aim must stay fenced\n", aim);
            ++failures;
        }
    }
    if (TravelReleaseGate(Facts(false, false, 0, "at:1:-7203.1,-3821.1,8.6")).empty())
    {
        std::printf("FAIL 'at:...': an unclaimed positional aim must stay fenced\n");
        ++failures;
    }
    // The profession fence is untouched: a pending learn with no claim still
    // holds the column.
    if (TravelReleaseGate(Facts(false, false, 197, "")).empty())
    {
        std::printf("FAIL: a pending profession errand must stay fenced\n");
        ++failures;
    }
    if (TravelReleaseGate(Facts(false, false, 202, "trainer")).empty())
    {
        std::printf("FAIL: a profession fence over a plain keyword must stay fenced\n");
        ++failures;
    }
}

// A COLUMN THAT MOVED AFTER THE CLAIM IS NOT THIS BOOK'S TO ERASE. The
// changed-since-claim fence is the other half of the gate and is untouched by
// this fix: whatever the new writer wrote stays standing.
void AChangedColumnStaysProtected()
{
    std::string const fence =
        TravelReleaseGate(Facts(false, true, 0, "repair"));
    if (fence.empty())
    {
        std::printf("FAIL: a column changed after the claim must stay fenced\n");
        ++failures;
    }
    // Even a claimed record does not clear a moved column: `claimed` means the
    // book wrote what stands, so a move means it no longer does.
    if (TravelReleaseGate(Facts(true, true, 0, "repair")).empty())
    {
        std::printf("FAIL: a moved column must stay fenced even with a claim on record\n");
        ++failures;
    }
}

}  // namespace

int main()
{
    ASelfClaimedRepairAimIsCleared();
    AGenuinelyForeignAimStaysProtected();
    AChangedColumnStaysProtected();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a claimed aim clears, a foreign aim does not\n");
    return EXIT_SUCCESS;
}
