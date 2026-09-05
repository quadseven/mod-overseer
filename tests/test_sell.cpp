/*
 * The vendor sale's three pure decisions, exercised without a world.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - The command grammar is `guid:<n>[ count:<n>]` and NOTHING ELSE. An
 *     `entry:` form is refused on purpose: an entry names a type, and a family
 *     carrying two stacks of the same thing would have the executor choosing
 *     which to sell, which is the one decision it must never make. A count of
 *     zero is refused rather than read as "all", because the core treats 0 as
 *     its own sell-all special case (ItemHandler.cpp:638) and a sender that
 *     wants the whole stack says so by leaving count out.
 *   - Among several vendors in reach, the nearest one that BUYS wins, and a
 *     vendor flagged as refusing sales is chosen only when it is the only
 *     kind present - so the refusal can be named as the vendor's.
 *   - A refusal's retry class is decided from the `detail` literal alone,
 *     because that literal is the one string a row carries that both sides of
 *     the queue read. The literals pinned here are the ones the executor
 *     returns; renaming one there without renaming it here turns a permanent
 *     refusal into an endlessly retried one, which is what this test exists
 *     to catch.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ChooseSellVendor;
using OverseerDecisions::ParseSellSpec;
using OverseerDecisions::SellRefusalRetry;
using OverseerDecisions::SellRetry;
using OverseerDecisions::SellRetryWord;
using OverseerDecisions::SellSpec;
using OverseerDecisions::SellVendorCandidate;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNumber(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, got, want);
    ++failures;
}

void CheckWord(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// ---- the grammar -----------------------------------------------------------

void AWholeStackIsTheGuidAlone()
{
    SellSpec const spec = ParseSellSpec("guid:494263");
    Check("guid alone is valid", spec.valid, true);
    CheckNumber("and names the guid", spec.guid, 494263);
    CheckNumber("and means the whole stack", spec.count, 0);
}

void PartOfAStackIsGuidThenCount()
{
    SellSpec const spec = ParseSellSpec("guid:494263 count:7");
    Check("guid then count is valid", spec.valid, true);
    CheckNumber("and names the guid", spec.guid, 494263);
    CheckNumber("and the count", spec.count, 7);
}

void SurplusSpacesAreNotAThirdToken()
{
    Check("leading, doubled and trailing spaces are tolerated",
          ParseSellSpec("  guid:1   count:2 ").valid, true);
}

void EverythingElseIsMalformed()
{
    Check("an empty command", ParseSellSpec("").valid, false);
    Check("a bare number", ParseSellSpec("494263").valid, false);
    Check("an entry, which would make the executor choose",
          ParseSellSpec("entry:4562").valid, false);
    Check("a guid of zero", ParseSellSpec("guid:0").valid, false);
    Check("a guid with letters in it", ParseSellSpec("guid:12ab").valid, false);
    Check("a guid with nothing after the colon", ParseSellSpec("guid:").valid, false);
    Check("count before guid", ParseSellSpec("count:2 guid:5").valid, false);
    Check("a count of zero is not 'all'", ParseSellSpec("guid:5 count:0").valid, false);
    Check("a count with letters in it", ParseSellSpec("guid:5 count:x").valid, false);
    Check("a third token", ParseSellSpec("guid:5 count:1 now").valid, false);
    Check("the wrong key for the second token", ParseSellSpec("guid:5 entry:1").valid, false);
    Check("a guid that does not fit in 32 bits", ParseSellSpec("guid:4294967296").valid, false);
    Check("a guid that only just does", ParseSellSpec("guid:4294967295").valid, true);
}

// ---- which vendor ----------------------------------------------------------

void NoVendorIsNoChoice()
{
    CheckNumber("an empty list chooses nobody", ChooseSellVendor({}), -1);
}

void TheNearestBuyerWins()
{
    std::vector<SellVendorCandidate> const vendors = {
        {4.5f, false},
        {2.0f, false},
        {3.0f, false},
    };
    CheckNumber("the nearest buyer is chosen", ChooseSellVendor(vendors), 1);
}

void ABuyerBeatsANearerRefuser()
{
    std::vector<SellVendorCandidate> const vendors = {
        {1.0f, true},
        {5.0f, false},
    };
    CheckNumber("the farther buyer beats the nearer refuser", ChooseSellVendor(vendors), 1);
}

void ARefuserIsChosenOnlyAlone()
{
    std::vector<SellVendorCandidate> const vendors = {
        {3.0f, true},
        {1.0f, true},
    };
    CheckNumber("with only refusers, the nearest is still named", ChooseSellVendor(vendors), 1);
}

void ATieGoesToTheLowerIndex()
{
    std::vector<SellVendorCandidate> const vendors = {
        {2.0f, false},
        {2.0f, false},
    };
    CheckNumber("equal distances keep the first", ChooseSellVendor(vendors), 0);
}

// ---- is it worth retrying --------------------------------------------------

void TheItemItselfIsNeverWorthRetrying()
{
    Check("a malformed command",
          SellRefusalRetry("malformed sell: want guid:<item_instance.guid>[ count:<n>]") == SellRetry::Never, true);
    Check("an item that is not carried", SellRefusalRetry("item not carried") == SellRetry::Never, true);
    Check("a quest item", SellRefusalRetry("item is a quest item") == SellRetry::Never, true);
    Check("an item with no sell price", SellRefusalRetry("item cannot be sold") == SellRetry::Never, true);
    Check("a count past the stack", SellRefusalRetry("count exceeds stack") == SellRetry::Never, true);
}

void ThisSpotIsWorthRetryingElsewhere()
{
    Check("no vendor in range", SellRefusalRetry("vendor not in range") == SellRetry::Elsewhere, true);
    Check("a vendor that does not buy", SellRefusalRetry("vendor refuses item") == SellRetry::Elsewhere, true);
}

void TheSellersStateIsWorthRetryingLater()
{
    Check("dead", SellRefusalRetry("seller is dead") == SellRetry::Later, true);
    Check("in flight", SellRefusalRetry("seller is in flight") == SellRetry::Later, true);
    Check("a bag with things in it", SellRefusalRetry("item is a non-empty bag") == SellRetry::Later, true);
    Check("a loot window open on it", SellRefusalRetry("item is being looted") == SellRetry::Later, true);
    Check("still refundable", SellRefusalRetry("item is still refundable") == SellRetry::Later, true);
    Check("at the gold cap", SellRefusalRetry("too much gold") == SellRetry::Later, true);
    Check("the core said no for a reason of its own",
          SellRefusalRetry("the core refused the sale") == SellRetry::Later, true);
}

void AnUnknownRefusalIsRetriedLater()
{
    Check("a literal this table has never seen",
          SellRefusalRetry("something new") == SellRetry::Later, true);
    Check("and so is an empty one", SellRefusalRetry("") == SellRetry::Later, true);
}

void TheWordsAreTheOnesTheRowCarries()
{
    CheckWord("never", SellRetryWord(SellRetry::Never), "never");
    CheckWord("elsewhere", SellRetryWord(SellRetry::Elsewhere), "elsewhere");
    CheckWord("later", SellRetryWord(SellRetry::Later), "later");
}

}  // namespace

int main()
{
    AWholeStackIsTheGuidAlone();
    PartOfAStackIsGuidThenCount();
    SurplusSpacesAreNotAThirdToken();
    EverythingElseIsMalformed();

    NoVendorIsNoChoice();
    TheNearestBuyerWins();
    ABuyerBeatsANearerRefuser();
    ARefuserIsChosenOnlyAlone();
    ATieGoesToTheLowerIndex();

    TheItemItselfIsNeverWorthRetrying();
    ThisSpotIsWorthRetryingElsewhere();
    TheSellersStateIsWorthRetryingLater();
    AnUnknownRefusalIsRetriedLater();
    TheWordsAreTheOnesTheRowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the vendor sale decides what it should\n");
    return EXIT_SUCCESS;
}
