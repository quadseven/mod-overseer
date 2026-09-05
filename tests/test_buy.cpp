/*
 * The town trip's purchase decisions, exercised without a world.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - The command grammar is `entry:<n>` with an optional `count:` and an
 *     optional `max:`, in either order. `entry:` and not `guid:` is the one
 *     place this module addresses an item by TYPE, and it has to: the item
 *     does not exist until the purchase creates it, so there is no
 *     item_instance row to name.
 *   - `max:` is a copper ceiling the sender sets, and `capped` is what tells
 *     "no ceiling" from "a ceiling of nothing". `max:0` is a real request -
 *     "only if it is free" - and the two cases are pinned separately here
 *     because collapsing them would turn every uncapped row into one that can
 *     never buy anything.
 *   - Among several vendors in reach, one that HAS the thing beats one that
 *     merely sells it, which beats one that does not sell it at all; then the
 *     cheaper reputation discount, then the nearer, then the lower index. A
 *     vendor that does not stock the item is still chosen when it is the only
 *     one in reach, so the refusal can name it.
 *   - A refusal's retry class is decided from the `detail` literal alone. The
 *     one that matters most is "vendor is out of stock", which is LATER and
 *     not NEVER: a limited-stock vendor restocks on a timer, and the family's
 *     nearest weapon dealer really does sell healing potions three at a time.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::BuyRefusalRetry;
using OverseerDecisions::BuyRequest;
using OverseerDecisions::BuyVendorCandidate;
using OverseerDecisions::ChooseBuyVendor;
using OverseerDecisions::ParseBuyRequest;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

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

BuyVendorCandidate Vendor(float distance, float discount, bool stocks, bool inStock)
{
    BuyVendorCandidate candidate;
    candidate.distance = distance;
    candidate.discount = discount;
    candidate.stocksItem = stocks;
    candidate.inStock = inStock;
    return candidate;
}

// ---- the grammar -----------------------------------------------------------

void AnEntryAloneBuysOne()
{
    BuyRequest const request = ParseBuyRequest("entry:4594");
    Check("an entry alone is valid", request.valid, true);
    CheckNumber("and names the item type", request.entry, 4594);
    CheckNumber("and means one purchase", request.count, 1);
    Check("and sets no ceiling", request.capped, false);
}

void ACountIsPurchasesAndNotItems()
{
    BuyRequest const request = ParseBuyRequest("entry:4594 count:20");
    Check("entry then count is valid", request.valid, true);
    CheckNumber("and names the item type", request.entry, 4594);
    CheckNumber("and the number of purchases", request.count, 20);
}

void AMaxIsACeilingInCopper()
{
    BuyRequest const request = ParseBuyRequest("entry:4594 count:20 max:20000");
    Check("entry, count and max is valid", request.valid, true);
    Check("and the ceiling is set", request.capped, true);
    CheckNumber("and is the number given", request.maxCopper, 20000);
}

void TheTwoOptionsMayComeInEitherOrder()
{
    BuyRequest const request = ParseBuyRequest("entry:159 max:500 count:20");
    Check("max before count is valid", request.valid, true);
    CheckNumber("count is still read", request.count, 20);
    CheckNumber("and so is max", request.maxCopper, 500);
    Check("and the ceiling is set", request.capped, true);
}

void AFreeOnlyCeilingIsNotTheSameAsNoCeiling()
{
    BuyRequest const zero = ParseBuyRequest("entry:159 max:0");
    Check("max:0 is valid", zero.valid, true);
    Check("and sets a ceiling", zero.capped, true);
    CheckNumber("of nothing", zero.maxCopper, 0);

    BuyRequest const absent = ParseBuyRequest("entry:159");
    Check("no max sets no ceiling", absent.capped, false);
    CheckNumber("and leaves the number at zero, which nothing reads",
                absent.maxCopper, 0);
}

void SurplusBlanksAreNotWords()
{
    Check("leading, doubled and trailing blanks are tolerated",
          ParseBuyRequest("  entry:159   count:2 ").valid, true);
    Check("and a tab is a blank too", ParseBuyRequest("\tentry:159\tmax:10\t").valid, true);
}

void EverythingElseIsMalformed()
{
    Check("an empty command", ParseBuyRequest("").valid, false);
    Check("a bare number", ParseBuyRequest("4594").valid, false);
    Check("a guid, which names an item that does not exist yet",
          ParseBuyRequest("guid:4594").valid, false);
    Check("an entry of zero", ParseBuyRequest("entry:0").valid, false);
    Check("an entry with letters in it", ParseBuyRequest("entry:12ab").valid, false);
    Check("an entry with nothing after the colon", ParseBuyRequest("entry:").valid, false);
    Check("count before entry", ParseBuyRequest("count:2 entry:159").valid, false);
    Check("a count of zero is not one", ParseBuyRequest("entry:159 count:0").valid, false);
    Check("a count given twice", ParseBuyRequest("entry:159 count:2 count:3").valid, false);
    Check("a max given twice", ParseBuyRequest("entry:159 max:2 max:3").valid, false);
    Check("an unknown key", ParseBuyRequest("entry:159 slot:2").valid, false);
    Check("a trailing bare word", ParseBuyRequest("entry:159 now").valid, false);
    Check("an entry that does not fit in 32 bits",
          ParseBuyRequest("entry:4294967296").valid, false);
    Check("an entry that only just does", ParseBuyRequest("entry:4294967295").valid, true);
}

void AMalformedRequestSaysWhichWordWasWrong()
{
    CheckWord("an empty command names the whole grammar", ParseBuyRequest("").error,
              "malformed buy: want entry:<item_template.entry>[ count:<n>][ max:<copper>]");
    CheckWord("a zero entry names the entry", ParseBuyRequest("entry:0").error,
              "malformed buy: first word must be entry:<item_template.entry>, not 0");
    CheckWord("a zero count names the count", ParseBuyRequest("entry:1 count:0").error,
              "malformed buy: count must be 1 or more");
    CheckWord("an unknown key names the two that are known",
              ParseBuyRequest("entry:1 slot:2").error,
              "malformed buy: unknown word (want count:<n> or max:<copper>)");
}

// ---- which vendor ----------------------------------------------------------

void NoVendorIsNoChoice()
{
    CheckNumber("an empty list chooses nobody", ChooseBuyVendor({}), -1);
}

void HavingTheThingBeatsMerelySellingIt()
{
    std::vector<BuyVendorCandidate> const candidates = {
        Vendor(1.0f, 0.80f, true, false),  // sells it, sold out, cheap and close
        Vendor(5.4f, 1.00f, true, true),   // sells it and has it
    };
    CheckNumber("stock beats price and distance", ChooseBuyVendor(candidates), 1);
}

void SellingItBeatsNotSellingIt()
{
    std::vector<BuyVendorCandidate> const candidates = {
        Vendor(0.5f, 0.80f, false, false),
        Vendor(5.4f, 1.00f, true, false),
    };
    CheckNumber("a sold-out stocker beats a vendor that never had it",
                ChooseBuyVendor(candidates), 1);
}

void AmongVendorsWithTheThingTheCheaperWins()
{
    std::vector<BuyVendorCandidate> const candidates = {
        Vendor(1.0f, 1.00f, true, true),
        Vendor(5.4f, 0.90f, true, true),
    };
    CheckNumber("the cheaper vendor wins", ChooseBuyVendor(candidates), 1);
}

void DistanceBreaksATieInThePrice()
{
    std::vector<BuyVendorCandidate> const candidates = {
        Vendor(5.4f, 0.90f, true, true),
        Vendor(1.0f, 0.90f, true, true),
    };
    CheckNumber("same price, the nearer one wins", ChooseBuyVendor(candidates), 1);
}

void ATieGoesToTheLowerIndex()
{
    std::vector<BuyVendorCandidate> const candidates = {
        Vendor(3.0f, 1.0f, true, true),
        Vendor(3.0f, 1.0f, true, true),
    };
    CheckNumber("identical vendors resolve to the first", ChooseBuyVendor(candidates), 0);
}

void AVendorThatDoesNotStockItIsStillNamed()
{
    // So the refusal can be "this vendor does not stock 4594" rather than the
    // useless "no vendor", which is a different problem with a different fix.
    std::vector<BuyVendorCandidate> const candidates = {Vendor(2.0f, 1.0f, false, false)};
    CheckNumber("the only vendor in reach is chosen even though it cannot help",
                ChooseBuyVendor(candidates), 0);
}

// ---- whether a refusal is worth asking again -------------------------------

void TheItemAndTheCommandAreNeverWorthRetrying()
{
    for (char const* detail :
         {"malformed buy: want entry:<item_template.entry>[ count:<n>][ max:<copper>]",
          "malformed buy: first word must be entry:<item_template.entry>, not 0",
          "malformed buy: count must be 1 or more", "malformed buy: count given twice",
          "malformed buy: max given twice",
          "malformed buy: unknown word (want count:<n> or max:<copper>)",
          "malformed buy request", "no such item",
          "item is not for this class", "item is for the other faction",
          "item is not bought with gold", "price exceeds the cap the row set",
          "count exceeds what the packet carries", "count would overflow the purse"})
        Check(detail, BuyRefusalRetry(detail) == TownRetry::Never, true);
}

void ThisVendorIsWorthRetryingElsewhere()
{
    Check("no vendor here", BuyRefusalRetry("vendor not in range") == TownRetry::Elsewhere, true);
    Check("this vendor does not sell it",
          BuyRefusalRetry("vendor does not stock the item") == TownRetry::Elsewhere, true);
}

void StockAndMoneyAndBagsAreWorthRetryingLater()
{
    // A limited-stock vendor restocks on its own timer, the purse fills from
    // the sale queued behind this row, and the bags empty the same way. All
    // three are the same vendor, later.
    for (char const* detail : {"vendor is out of stock", "cannot afford the purchase",
                               "bags cannot take the item", "buyer is dead"})
        Check(detail, BuyRefusalRetry(detail) == TownRetry::Later, true);
}

void AnUnknownRefusalIsRetriedLater()
{
    Check("a literal this table has never heard of",
          BuyRefusalRetry("something nobody wrote down") == TownRetry::Later, true);
    Check("and the empty string, which is what a success carries",
          BuyRefusalRetry("") == TownRetry::Later, true);
}

void TheWordsAreTheOnesTheRowCarries()
{
    CheckWord("never", TownRetryWord(TownRetry::Never), "never");
    CheckWord("elsewhere", TownRetryWord(TownRetry::Elsewhere), "elsewhere");
    CheckWord("later", TownRetryWord(TownRetry::Later), "later");
}

}  // namespace

int main()
{
    AnEntryAloneBuysOne();
    ACountIsPurchasesAndNotItems();
    AMaxIsACeilingInCopper();
    TheTwoOptionsMayComeInEitherOrder();
    AFreeOnlyCeilingIsNotTheSameAsNoCeiling();
    SurplusBlanksAreNotWords();
    EverythingElseIsMalformed();
    AMalformedRequestSaysWhichWordWasWrong();

    NoVendorIsNoChoice();
    HavingTheThingBeatsMerelySellingIt();
    SellingItBeatsNotSellingIt();
    AmongVendorsWithTheThingTheCheaperWins();
    DistanceBreaksATieInThePrice();
    ATieGoesToTheLowerIndex();
    AVendorThatDoesNotStockItIsStillNamed();

    TheItemAndTheCommandAreNeverWorthRetrying();
    ThisVendorIsWorthRetryingElsewhere();
    StockAndMoneyAndBagsAreWorthRetryingLater();
    AnUnknownRefusalIsRetriedLater();
    TheWordsAreTheOnesTheRowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the purchase decides what it should\n");
    return EXIT_SUCCESS;
}
