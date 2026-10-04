/*
 * overseer_auction_history: one row per auction that closes (wow-overseer#536).
 *
 * The market engine prices from observed sales. A sold auction's price is the
 * winning bid (the core sets bid to the buyout on a buyout); an expired one
 * was paid by nobody, whatever its starting bid says. Each side is one of
 * random bot, guild member, family, player or unknown.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

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
}  // namespace

int main()
{
    Check("sold word", std::string(AuctionOutcomeWord(AuctionOutcome::Sold)) == "sold");
    Check("expired word", std::string(AuctionOutcomeWord(AuctionOutcome::Expired)) == "expired");

    Check("sold pays the bid", AuctionPricePaid(AuctionOutcome::Sold, 1250) == 1250);
    Check("expired pays nothing", AuctionPricePaid(AuctionOutcome::Expired, 1250) == 0);

    Check("no guid is unknown", ClassifyAuctionParty(false, true, true, true) == AuctionPartyKind::Unknown);
    Check("roster is family", ClassifyAuctionParty(true, true, true, true) == AuctionPartyKind::Family);
    Check("guild outranks bot", ClassifyAuctionParty(true, false, true, true) == AuctionPartyKind::GuildMember);
    Check("bot", ClassifyAuctionParty(true, false, false, true) == AuctionPartyKind::RandomBot);
    Check("anyone else is a player", ClassifyAuctionParty(true, false, false, false) == AuctionPartyKind::Player);

    Check("word bot", std::string(AuctionPartyWord(AuctionPartyKind::RandomBot)) == "random_bot");
    Check("word guild", std::string(AuctionPartyWord(AuctionPartyKind::GuildMember)) == "guild_member");
    Check("word family", std::string(AuctionPartyWord(AuctionPartyKind::Family)) == "family");
    Check("word player", std::string(AuctionPartyWord(AuctionPartyKind::Player)) == "player");
    Check("word unknown", std::string(AuctionPartyWord(AuctionPartyKind::Unknown)) == "unknown");

    AuctionHistoryRow sold;
    sold.house = 7;
    sold.itemEntry = 2589;
    sold.itemCount = 20;
    sold.bid = 900;
    sold.buyout = 900;
    sold.outcome = AuctionOutcome::Sold;
    sold.sellerGuid = 11;
    sold.sellerKind = AuctionPartyKind::RandomBot;
    sold.buyerGuid = 22;
    sold.buyerKind = AuctionPartyKind::Family;
    Check("sold insert",
          AuctionHistoryInsertSql(sold) ==
              "INSERT INTO overseer_auction_history (house, item_entry, item_count, bid, buyout, "
              "price_paid, outcome, seller_guid, seller_kind, buyer_guid, buyer_kind) VALUES "
              "(7, 2589, 20, 900, 900, 900, 'sold', 11, 'random_bot', 22, 'family')");

    AuctionHistoryRow lapsed;
    lapsed.house = 2;
    lapsed.itemEntry = 118;
    lapsed.itemCount = 1;
    lapsed.bid = 50;
    lapsed.buyout = 0;
    lapsed.outcome = AuctionOutcome::Expired;
    lapsed.sellerGuid = 5;
    lapsed.sellerKind = AuctionPartyKind::Player;
    Check("expired insert",
          AuctionHistoryInsertSql(lapsed) ==
              "INSERT INTO overseer_auction_history (house, item_entry, item_count, bid, buyout, "
              "price_paid, outcome, seller_guid, seller_kind, buyer_guid, buyer_kind) VALUES "
              "(2, 118, 1, 50, 0, 0, 'expired', 5, 'player', 0, 'unknown')");

    if (failures)
        return 1;
    std::puts("ok");
    return 0;
}
