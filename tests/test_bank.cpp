/*
 * The bank row, decided without a world.
 *
 * WHAT IT IS FOR. kind='bank' moves one named item across a banker's counter,
 * or buys the next bank bag slot. Everything the executor does after parsing
 * needs a Player, a Creature and the core's own bank handlers, and none of
 * that can be compiled here. What CAN be pinned is the half that needs no
 * world: what the text of a row is allowed to say, and which of several
 * bankers in reach gets the item.
 *
 * WHAT IS PINNED, and why each is worth a case:
 *
 *   - GUID ONLY. give and trade accept `entry:<id>` as a convenience; a bank
 *     move must not, because the same entry can sit on both sides of the
 *     counter at once and an `entry` withdraw would then have two right
 *     answers. A row using the give grammar is refused, not guessed at.
 *   - A GUID OF 0 IS REFUSED. 0 is what every "not found" path in the core
 *     returns, so a row asking for guid 0 would be answered by whichever item
 *     the lookup found first.
 *   - `buy slot` TAKES NOTHING ELSE. There is exactly one slot to buy next
 *     and the core decides its price, so an argument is a misunderstanding
 *     worth saying rather than ignoring.
 *   - THE BANKER CHOICE IS DETERMINISTIC. Interactable before not, nearest
 *     before farther, lowest id on a tie, and nothing at all when the only
 *     bankers in reach are ones the character may not talk to.
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

using OverseerDecisions::BankerCandidate;
using OverseerDecisions::BankRequest;
using OverseerDecisions::BankVerb;
using OverseerDecisions::NearestBanker;
using OverseerDecisions::ParseBankRequest;

namespace
{

int failures = 0;

char const* Name(BankVerb verb)
{
    switch (verb)
    {
        case BankVerb::None:     return "None";
        case BankVerb::Deposit:  return "Deposit";
        case BankVerb::Withdraw: return "Withdraw";
        case BankVerb::BuySlot:  return "BuySlot";
    }
    return "?";
}

void CheckParse(char const* text, BankVerb wantVerb, uint32_t wantGuid)
{
    BankRequest const got = ParseBankRequest(text);
    if (got.verb == wantVerb && got.itemGuid == wantGuid &&
        (wantVerb == BankVerb::None) == !got.error.empty())
        return;
    std::printf("FAIL parse '%s': got %s guid %u error '%s', wanted %s guid %u\n", text,
                Name(got.verb), got.itemGuid, got.error.c_str(), Name(wantVerb), wantGuid);
    ++failures;
}

void CheckRefused(char const* text, char const* wantError)
{
    BankRequest const got = ParseBankRequest(text);
    if (got.verb == BankVerb::None && got.error == wantError)
        return;
    std::printf("FAIL refuse '%s': got %s error '%s', wanted '%s'\n", text, Name(got.verb),
                got.error.c_str(), wantError);
    ++failures;
}

void CheckPick(char const* what, std::vector<BankerCandidate> const& candidates, uint32_t want)
{
    uint32_t const got = NearestBanker(candidates);
    if (got == want)
        return;
    std::printf("FAIL %s: picked %u, wanted %u\n", what, got, want);
    ++failures;
}

// The three verbs, in the exact form the sender writes them.
void TheThreeVerbsParse()
{
    CheckParse("deposit guid:1303004", BankVerb::Deposit, 1303004);
    CheckParse("withdraw guid:7", BankVerb::Withdraw, 7);
    CheckParse("buy slot", BankVerb::BuySlot, 0);
}

// A row typed by hand carries whatever blanks the hand put there.
void BlanksDoNotMatter()
{
    CheckParse("  deposit   guid:12  ", BankVerb::Deposit, 12);
    CheckParse("\twithdraw\tguid:12\n", BankVerb::Withdraw, 12);
    CheckParse("buy   slot", BankVerb::BuySlot, 0);
    CheckParse("deposit guid:4294967295", BankVerb::Deposit, 4294967295u);
}

// What is refused, and with which words. The literals are what an operator
// reads in `detail`, so they are pinned, not just the fact of refusal.
void TheWrongTextIsRefusedByName()
{
    CheckRefused("", "malformed bank: want deposit guid:<n>, withdraw guid:<n>, or buy slot");
    CheckRefused("   ", "malformed bank: want deposit guid:<n>, withdraw guid:<n>, or buy slot");
    CheckRefused("sell guid:12", "malformed bank: unknown verb (want deposit, withdraw, or buy slot)");
    CheckRefused("Deposit guid:12", "malformed bank: unknown verb (want deposit, withdraw, or buy slot)");
    CheckRefused("deposit", "malformed bank: want deposit guid:<item_instance.guid>");
    CheckRefused("withdraw", "malformed bank: want withdraw guid:<item_instance.guid>");
    CheckRefused("deposit guid:12 guid:13", "malformed bank: want deposit guid:<item_instance.guid>");
    CheckRefused("buy", "malformed bank: buy takes exactly `slot`");
    CheckRefused("buy slot 2", "malformed bank: buy takes exactly `slot`");
    CheckRefused("buy bag", "malformed bank: buy takes exactly `slot`");
}

// The give grammar is not the bank grammar, and 0 is not an item.
void OnlyANonZeroGuidNamesAnItem()
{
    char const* const notAnItem = "malformed bank: item must be guid:<item_instance.guid>, not 0";
    CheckRefused("deposit entry:4306", notAnItem);
    CheckRefused("deposit guid:0", notAnItem);
    CheckRefused("deposit guid:", notAnItem);
    CheckRefused("deposit guid:12a", notAnItem);
    CheckRefused("deposit 12", notAnItem);
    CheckRefused("withdraw guid:-1", notAnItem);
    CheckRefused("withdraw guid:4294967296", notAnItem);   // one past uint32
    CheckRefused("withdraw guid:99999999999999999999", notAnItem);
}

// Which banker. Distances are yards, ids are guid counters, nothing else.
void TheNearestInteractableBankerIsChosen()
{
    CheckPick("nobody in reach", {}, 0);
    CheckPick("one banker", {{41, 3.2f, true}}, 41);
    CheckPick("the nearer of two", {{41, 3.2f, true}, {42, 1.1f, true}}, 42);
    CheckPick("an interactable one beats a nearer one that is not",
              {{41, 3.2f, true}, {42, 1.1f, false}}, 41);
    CheckPick("a tie goes to the lowest id, whichever came first",
              {{43, 2.0f, true}, {41, 2.0f, true}, {42, 2.0f, true}}, 41);
    CheckPick("bankers nobody may talk to are no banker at all",
              {{41, 3.2f, false}, {42, 1.1f, false}}, 0);
    CheckPick("an id of 0 is not a creature", {{0, 0.5f, true}, {41, 3.2f, true}}, 41);
}

}  // namespace

int main()
{
    TheThreeVerbsParse();
    BlanksDoNotMatter();
    TheWrongTextIsRefusedByName();
    OnlyANonZeroGuidNamesAnItem();
    TheNearestInteractableBankerIsChosen();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the bank row holds\n");
    return EXIT_SUCCESS;
}
