/*
 * trade_wtb: WTB (Want to Buy) candidate selection with cooldown ledger and offer log.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

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

void CheckEq(char const* what, std::string const& a, std::string const& b)
{
    if (a == b)
        return;
    std::printf("FAIL %s: got '%s', expected '%s'\n", what, a.c_str(), b.c_str());
    ++failures;
}

void CheckEq(char const* what, uint32_t a, uint32_t b)
{
    if (a == b)
        return;
    std::printf("FAIL %s: got %u, expected %u\n", what, a, b);
    ++failures;
}

void CheckEq(char const* what, WtbOfferResult a, WtbOfferResult b)
{
    if (a == b)
        return;
    std::printf("FAIL %s: got %d, expected %d\n", what, static_cast<int>(a), static_cast<int>(b));
    ++failures;
}

}  // namespace

int main()
{
    // Test 1: WTB candidate yielded when family wants material not on AH under budget
    {
        // Family wants item entry 2589 (Linen Cloth)
        FamilyWants wants;
        wants.wantList = {2589, 2592};  // Linen Cloth, Wool Cloth

        // AH has no Linen Cloth (item 2589), but has Wool Cloth (item 2592) at high price
        // We simulate this by checking budget allows for 2589 but not for 2592
        uint32_t money = 10000;      // 1g
        uint32_t reserve = 1000;     // 10s reserve
        uint32_t sharePercent = 50;  // 50% share
        uint32_t priceLinen = 500;   // 5s per linen
        uint32_t priceWool = 5000;   // 50s per wool (over budget)

        // Linen Cloth (2589) is wanted, not on AH, and under budget -> WTB candidate
        bool linenIsCandidate = WtbCandidate(wants, 2589, 0, money, reserve, sharePercent, priceLinen);
        Check("linen cloth is WTB candidate (wanted, not on AH, under budget)", linenIsCandidate);

        // Wool Cloth (2592) is wanted but on AH at price over budget -> not a candidate
        bool woolIsCandidate = WtbCandidate(wants, 2592, 5000, money, reserve, sharePercent, priceWool);
        Check("wool cloth not WTB candidate (on AH over budget)", !woolIsCandidate);

        // Silk Cloth (4306) not wanted -> not a candidate
        bool silkIsCandidate = WtbCandidate(wants, 4306, 0, money, reserve, sharePercent, 1000);
        Check("silk cloth not WTB candidate (not wanted)", !silkIsCandidate);

        // Linen Cloth but over budget -> not a candidate
        bool linenOverBudget = WtbCandidate(wants, 2589, 0, money, reserve, sharePercent, 10000);
        Check("linen cloth not WTB candidate (over budget)", !linenOverBudget);
    }

    // Test 2: 10-minute per-item cooldown ledger enforced
    {
        WtbCooldownLedger ledger;
        time_t now = 1000000;

        // First offer for item 2589 at time 1000000
        bool firstAllowed = WtbCooldownCheck(ledger, 2589, now, 600);  // 600s = 10 min
        Check("first offer allowed", firstAllowed);

        // Record the offer
        WtbCooldownRecord(ledger, 2589, now);

        // Second offer for same item within 10 minutes (at 1000500 = 500s later) -> denied
        bool secondDenied = WtbCooldownCheck(ledger, 2589, 1000500, 600);
        Check("second offer within 10 min denied", !secondDenied);

        // Third offer after 10 minutes (at 1000601 = 601s later) -> allowed
        bool thirdAllowed = WtbCooldownCheck(ledger, 2589, 1000601, 600);
        Check("third offer after 10 min allowed", thirdAllowed);

        // Different item not affected by cooldown
        bool otherItemAllowed = WtbCooldownCheck(ledger, 2592, 1000500, 600);
        Check("different item not affected by cooldown", otherItemAllowed);
    }

    // Test 3: Offer log records accepted/declined offers with reasons
    {
        WtbOfferLog log;
        time_t now = 2000000;

        // Record an accepted offer
        WtbOfferLogRecord(log, 2589, "member1", WtbOfferResult::Accepted, "budget_ok", now);
        CheckEq("log size after one accepted", log.entries.size(), 1u);
        CheckEq("entry item", log.entries[0].itemEntry, 2589u);
        CheckEq("entry member", log.entries[0].member, std::string("member1"));
        CheckEq("entry result", log.entries[0].result, WtbOfferResult::Accepted);
        CheckEq("entry reason", log.entries[0].reason, std::string("budget_ok"));
        CheckEq("entry time", log.entries[0].time, now);

        // Record a declined offer
        WtbOfferLogRecord(log, 2592, "member2", WtbOfferResult::Declined, "over_budget", now + 100);
        CheckEq("log size after declined", log.entries.size(), 2u);
        CheckEq("declined entry result", log.entries[1].result, WtbOfferResult::Declined);
        CheckEq("declined entry reason", log.entries[1].reason, std::string("over_budget"));

        // Record another accepted offer
        WtbOfferLogRecord(log, 2589, "member3", WtbOfferResult::Accepted, "budget_ok", now + 200);
        CheckEq("log size after three entries", log.entries.size(), 3u);
    }

    if (failures)
        return 1;
    std::puts("ok");
    return 0;
}