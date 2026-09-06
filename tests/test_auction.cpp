/*
 * The auction executor's pure half, decided without a world.
 *
 * WHAT IT IS FOR. kind='auction' drives three of the core's own WorldSession
 * handlers, and every one of them answers the CLIENT: a status packet on the
 * session, a void return, or - for a bad bid or a bad duration - nothing at
 * all. A bot has no client, so the module has to say before the call which
 * wall a row is about to hit. The parts of that which need nothing from the
 * world live in overseer_decisions and are pinned here:
 *
 *   - The grammar. Four verbs, key:value arguments in any order, each once,
 *     every one required. A row that is short a field, or carries one its
 *     verb does not take, is refused rather than guessed at, because a bid
 *     that was meant as a buyout is real money.
 *   - The duration rule. The core accepts exactly 12, 24 and 48 hours and
 *     returns silently for anything else (AuctionHouseHandler.cpp:185-193),
 *     so the packet minutes for those three, and 0 for every other count,
 *     are worth a case each.
 *   - The bid rule, in the core's order (AuctionHouseHandler.cpp:488-497),
 *     with the outbid step passed IN so the core's 5%-or-1-copper rule is
 *     not carried here a second time.
 *   - What a bid costs, which is the difference when the bidder is already
 *     the top bidder (AuctionHouseHandler.cpp:512-513, :553-554).
 *   - Which refusals are worth a plain retry. A wall that moves (fighting,
 *     dead, broke, not yet at the auctioneer) is; a wall that never will
 *     (soulbound, malformed, gone, someone else's) is not. The classification
 *     is keyed on the same literals DoAuction writes into `detail`, so the
 *     test names them through the header rather than retyping them.
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

using OverseerDecisions::AuctionBidAcceptable;
using OverseerDecisions::AuctionBidCost;
using OverseerDecisions::AuctionBidVerdict;
using OverseerDecisions::AuctionDurationMinutes;
using OverseerDecisions::AuctionRefusalRetryable;
using OverseerDecisions::AuctionRequest;
using OverseerDecisions::AuctionVerb;
using OverseerDecisions::ParseAuctionRequest;
namespace AuctionRefusal = OverseerDecisions::AuctionRefusal;

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

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

char const* VerbName(AuctionVerb verb)
{
    switch (verb)
    {
        case AuctionVerb::None:   return "none";
        case AuctionVerb::List:   return "list";
        case AuctionVerb::Buy:    return "buy";
        case AuctionVerb::Bid:    return "bid";
        case AuctionVerb::Cancel: return "cancel";
    }
    return "?";
}

void CheckVerb(char const* what, AuctionVerb got, AuctionVerb want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: verb %s, wanted %s\n", what, VerbName(got), VerbName(want));
    ++failures;
}

char const* VerdictName(AuctionBidVerdict verdict)
{
    switch (verdict)
    {
        case AuctionBidVerdict::Ok:              return "ok";
        case AuctionBidVerdict::NotAboveCurrent: return "not above current";
        case AuctionBidVerdict::BelowIncrement:  return "below increment";
    }
    return "?";
}

void CheckVerdict(char const* what, AuctionBidVerdict got, AuctionBidVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: %s, wanted %s\n", what, VerdictName(got), VerdictName(want));
    ++failures;
}

// ---------------------------------------------------------------- grammar --

void AListParsesEveryField()
{
    AuctionRequest const r = ParseAuctionRequest("list guid:1303004 bid:1500 buyout:2500 hours:24");
    CheckVerb("a full list line parses", r.verb, AuctionVerb::List);
    CheckText("and carries no error", r.error, "");
    CheckNumber("item guid", r.itemGuid, 1303004);
    CheckNumber("start bid", r.bid, 1500);
    CheckNumber("buyout", r.buyout, 2500);
    CheckNumber("hours", r.hours, 24);
}

void FieldsMayComeInAnyOrder()
{
    AuctionRequest const r = ParseAuctionRequest("list hours:48 buyout:0 bid:7 guid:42");
    CheckVerb("order does not matter", r.verb, AuctionVerb::List);
    CheckNumber("guid still lands", r.itemGuid, 42);
    CheckNumber("a zero buyout is a listing with no buyout", r.buyout, 0);
    CheckNumber("hours still lands", r.hours, 48);
}

void ABuyTakesOnlyTheAuction()
{
    AuctionRequest const r = ParseAuctionRequest("buy auction:9001");
    CheckVerb("a buy parses", r.verb, AuctionVerb::Buy);
    CheckNumber("auction id", r.auctionId, 9001);

    AuctionRequest const extra = ParseAuctionRequest("buy auction:9001 bid:5");
    CheckVerb("a buy with a bid is refused, not guessed at", extra.verb, AuctionVerb::None);
    CheckText("as malformed", extra.error, AuctionRefusal::Malformed);
}

void ABidTakesAuctionAndBid()
{
    AuctionRequest const r = ParseAuctionRequest("bid auction:9001 bid:120");
    CheckVerb("a bid parses", r.verb, AuctionVerb::Bid);
    CheckNumber("auction id", r.auctionId, 9001);
    CheckNumber("bid", r.bid, 120);

    AuctionRequest const missing = ParseAuctionRequest("bid auction:9001");
    CheckVerb("a bid without an amount is refused", missing.verb, AuctionVerb::None);
    CheckText("as malformed", missing.error, AuctionRefusal::Malformed);

    AuctionRequest const zero = ParseAuctionRequest("bid auction:9001 bid:0");
    CheckVerb("a zero bid is refused", zero.verb, AuctionVerb::None);
    CheckText("by name", zero.error, AuctionRefusal::ZeroBid);
}

void ACancelTakesOnlyTheAuction()
{
    AuctionRequest const r = ParseAuctionRequest("cancel auction:77");
    CheckVerb("a cancel parses", r.verb, AuctionVerb::Cancel);
    CheckNumber("auction id", r.auctionId, 77);
}

void WhatDoesNotParse()
{
    struct Case
    {
        char const* line;
        char const* why;
    };
    Case const cases[] = {
        {"", "an empty line"},
        {"sell guid:1 bid:1 buyout:1 hours:12", "an unknown verb"},
        {"list", "a list with no fields"},
        {"list guid:1 bid:1 buyout:1", "a list short of its hours"},
        {"list guid:1 bid:1 buyout:1 hours:12 hours:24", "a repeated field"},
        {"list guid:1 bid:1 buyout:1 hours:12 auction:3", "a field the verb does not take"},
        {"list guid:x bid:1 buyout:1 hours:12", "a non-numeric value"},
        {"list guid:-1 bid:1 buyout:1 hours:12", "a signed value"},
        {"list guid 1 bid:1 buyout:1 hours:12", "a field with no colon"},
        {"list guid:0 bid:1 buyout:1 hours:12", "a zero item guid"},
        {"buy auction:0", "a zero auction id"},
        {"buy", "a buy with no auction"},
        {"cancel auction:1 bid:1", "a cancel carrying a bid"},
        {"list guid:1 bid:99999999999 buyout:1 hours:12", "a value past uint32"},
    };
    for (Case const& c : cases)
    {
        AuctionRequest const r = ParseAuctionRequest(c.line);
        CheckVerb(c.why, r.verb, AuctionVerb::None);
        CheckText(c.why, r.error, AuctionRefusal::Malformed);
    }
}

void AListIsCheckedForSense()
{
    AuctionRequest const noBid = ParseAuctionRequest("list guid:1 bid:0 buyout:100 hours:12");
    CheckVerb("a zero starting bid is refused", noBid.verb, AuctionVerb::None);
    CheckText("by name", noBid.error, AuctionRefusal::NoStartBid);

    AuctionRequest const upsideDown = ParseAuctionRequest("list guid:1 bid:200 buyout:100 hours:12");
    CheckVerb("a buyout under the starting bid is refused", upsideDown.verb, AuctionVerb::None);
    CheckText("by name", upsideDown.error, AuctionRefusal::BuyoutBelowBid);

    AuctionRequest const equal = ParseAuctionRequest("list guid:1 bid:200 buyout:200 hours:12");
    CheckVerb("a buyout equal to the bid is fine", equal.verb, AuctionVerb::List);

    AuctionRequest const badHours = ParseAuctionRequest("list guid:1 bid:1 buyout:1 hours:36");
    CheckVerb("36 hours is refused", badHours.verb, AuctionVerb::None);
    CheckText("by name, not as malformed", badHours.error, AuctionRefusal::InvalidDuration);
}

// --------------------------------------------------------------- duration --

void OnlyThreeDurationsExist()
{
    CheckNumber("12 hours is 720 minutes on the wire", AuctionDurationMinutes(12), 720);
    CheckNumber("24 hours is 1440", AuctionDurationMinutes(24), 1440);
    CheckNumber("48 hours is 2880", AuctionDurationMinutes(48), 2880);
    CheckNumber("0 is nothing", AuctionDurationMinutes(0), 0);
    CheckNumber("1 is nothing", AuctionDurationMinutes(1), 0);
    CheckNumber("36 is nothing", AuctionDurationMinutes(36), 0);
    CheckNumber("72 is nothing", AuctionDurationMinutes(72), 0);
    CheckNumber("720 (minutes given as hours) is nothing", AuctionDurationMinutes(720), 0);
}

// -------------------------------------------------------------------- bid --

void TheBidRuleInTheCoresOrder()
{
    // A fresh auction: start 100, no bids, buyout 500, step 1 (5% of 0 is 0,
    // so the core's floor of 1 copper).
    CheckVerdict("below the start bid", AuctionBidAcceptable(99, 100, 0, 500, 1),
                 AuctionBidVerdict::NotAboveCurrent);
    CheckVerdict("exactly the start bid is fine on a fresh auction",
                 AuctionBidAcceptable(100, 100, 0, 500, 1), AuctionBidVerdict::Ok);

    // A standing bid of 200, step 10.
    CheckVerdict("equal to the standing bid", AuctionBidAcceptable(200, 100, 200, 500, 10),
                 AuctionBidVerdict::NotAboveCurrent);
    CheckVerdict("one copper over is short of the step",
                 AuctionBidAcceptable(201, 100, 200, 500, 10), AuctionBidVerdict::BelowIncrement);
    CheckVerdict("nine over is still short", AuctionBidAcceptable(209, 100, 200, 500, 10),
                 AuctionBidVerdict::BelowIncrement);
    CheckVerdict("the step exactly clears", AuctionBidAcceptable(210, 100, 200, 500, 10),
                 AuctionBidVerdict::Ok);

    // A buyout skips the increment test (AuctionHouseHandler.cpp:492).
    CheckVerdict("the buyout is always enough over a standing bid",
                 AuctionBidAcceptable(500, 100, 499, 500, 25), AuctionBidVerdict::Ok);
    CheckVerdict("but not when there is no buyout to skip to",
                 AuctionBidAcceptable(500, 100, 499, 0, 25), AuctionBidVerdict::BelowIncrement);

    // Near the cap the threshold must not wrap.
    CheckVerdict("a standing bid near the cap does not wrap into acceptance",
                 AuctionBidAcceptable(4294967295u, 100, 4294967290u, 0, 100),
                 AuctionBidVerdict::BelowIncrement);
}

void WhatABidCosts()
{
    CheckNumber("a first bid costs the price", AuctionBidCost(300, 200, false), 300);
    CheckNumber("raising your own bid costs the difference", AuctionBidCost(300, 200, true), 100);
    CheckNumber("a buyout over your own bid costs the difference too",
                AuctionBidCost(500, 200, true), 300);
    CheckNumber("a nonsensical raise below your own bid costs the price, never wraps",
                AuctionBidCost(100, 200, true), 100);
}

// -------------------------------------------------------------- retryable --

void WhichRefusalsMove()
{
    char const* const moving[] = {
        AuctionRefusal::NotInRange,
        AuctionRefusal::Dead,
        AuctionRefusal::InCombat,
        AuctionRefusal::Trading,
        AuctionRefusal::InFlight,
        AuctionRefusal::Stunned,
        AuctionRefusal::LoggingOut,
        AuctionRefusal::CannotAffordDeposit,
        AuctionRefusal::CannotAffordBuyout,
        AuctionRefusal::CannotAffordBid,
        AuctionRefusal::CannotAffordCut,
    };
    for (char const* reason : moving)
        Check(reason, AuctionRefusalRetryable(reason), true);

    char const* const standing[] = {
        AuctionRefusal::Malformed,
        AuctionRefusal::InvalidDuration,
        AuctionRefusal::NoStartBid,
        AuctionRefusal::ZeroBid,
        AuctionRefusal::BuyoutBelowBid,
        AuctionRefusal::PriceTooHigh,
        AuctionRefusal::BelowLevel,
        AuctionRefusal::ItemNotCarried,
        AuctionRefusal::ItemSoulbound,
        AuctionRefusal::ItemQuest,
        AuctionRefusal::ItemNotTradable,
        AuctionRefusal::ItemAlreadyListed,
        AuctionRefusal::BidTooLow,
        AuctionRefusal::NoBuyout,
        AuctionRefusal::AuctionNotFound,
        AuctionRefusal::WrongHouse,
        AuctionRefusal::OwnAuction,
        AuctionRefusal::NotOwnAuction,
        AuctionRefusal::CoreRefused,
        AuctionRefusal::NotReadBack,
    };
    for (char const* reason : standing)
        Check(reason, AuctionRefusalRetryable(reason), false);

    Check("an unknown reason is not retried", AuctionRefusalRetryable("something new"), false);
    Check("nor is an empty one", AuctionRefusalRetryable(""), false);
}

}  // namespace

int main()
{
    AListParsesEveryField();
    FieldsMayComeInAnyOrder();
    ABuyTakesOnlyTheAuction();
    ABidTakesAuctionAndBid();
    ACancelTakesOnlyTheAuction();
    WhatDoesNotParse();
    AListIsCheckedForSense();
    OnlyThreeDurationsExist();
    TheBidRuleInTheCoresOrder();
    WhatABidCosts();
    WhichRefusalsMove();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the auction decisions hold\n");
    return EXIT_SUCCESS;
}
