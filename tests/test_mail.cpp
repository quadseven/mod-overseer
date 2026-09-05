/*
 * The mail executor's pure half, decided without a world.
 *
 * WHAT IT IS FOR. kind='mail' drives five of the core's own WorldSession
 * handlers, and every one of them answers the CLIENT: a status packet on the
 * session, a void return, or - for a letter with no mailbox in reach, an empty
 * recipient, or text the client cannot render - nothing at all. A bot has no
 * client, so the module has to say before the call which wall a row is about to
 * hit. The parts of that which need nothing from the world live in
 * overseer_decisions and are pinned here:
 *
 *   - The grammar. Five verbs, key:value arguments in any order, each once,
 *     exactly the set the verb takes. A row short a field, or carrying one its
 *     verb does not take, is refused rather than guessed at, because a `send`
 *     that was meant as a `take-item` moves real property.
 *   - The text tail, which is what makes this grammar different from the
 *     auction one. A mail has two free-text fields and the row has one column,
 *     so `subject:` opens the tail and the first ` body:` after it splits.
 *     There is deliberately no escape for a literal " body:" inside a subject:
 *     that is stated in the header and pinned below rather than left for
 *     somebody to find out.
 *   - Postage, which is the sum HandleSendMail checks the purse against
 *     (MailHandler.cpp:163-175), including the overflow it refuses silently.
 *   - The delivery delay rule, which is the one piece of mail behaviour an
 *     operator has to plan around: an item to another account waits, and
 *     nothing else does (MailHandler.cpp:350, :362).
 *   - Which refusals are worth a plain retry. A wall that moves (fighting,
 *     dead, broke, no bag space, not yet at a mailbox, a letter still in
 *     transit) is; a wall that never will (soulbound, malformed, gone, cash on
 *     delivery) is not. The classification is keyed on the same literals DoMail
 *     writes into `detail`, so the test names them through the header rather
 *     than retyping them.
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

using OverseerDecisions::MailDeliveryDelaySeconds;
using OverseerDecisions::MailRefusalRetryable;
using OverseerDecisions::MailRequest;
using OverseerDecisions::MailTotalCost;
using OverseerDecisions::MailVerb;
using OverseerDecisions::ParseMailRequest;
namespace MailRefusal = OverseerDecisions::MailRefusal;

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

char const* VerbName(MailVerb verb)
{
    switch (verb)
    {
        case MailVerb::None:      return "none";
        case MailVerb::Send:      return "send";
        case MailVerb::TakeItem:  return "take-item";
        case MailVerb::TakeMoney: return "take-money";
        case MailVerb::Return:    return "return";
        case MailVerb::Delete:    return "delete";
    }
    return "?";
}

void CheckVerb(char const* what, MailVerb got, MailVerb want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: verb %s, wanted %s\n", what, VerbName(got), VerbName(want));
    ++failures;
}

// ---------------------------------------------------------------- grammar --

void ASendCarriesAnItemAndAMessage()
{
    MailRequest const r = ParseMailRequest("send item:1303004 subject:greens for you body:og make shirt");
    CheckVerb("a full send line parses", r.verb, MailVerb::Send);
    CheckText("and carries no error", r.error, "");
    CheckNumber("item guid", r.itemGuid, 1303004);
    Check("the item key was seen", r.hasItem, true);
    Check("no money key was seen", r.hasMoney, false);
    CheckNumber("and no money", r.money, 0);
    CheckText("subject", r.subject, "greens for you");
    CheckText("body", r.body, "og make shirt");
}

void ASendCanBeMoneyOnly()
{
    MailRequest const r = ParseMailRequest("send money:5000 subject:your cut");
    CheckVerb("money with no item parses", r.verb, MailVerb::Send);
    CheckNumber("money", r.money, 5000);
    Check("the money key was seen", r.hasMoney, true);
    Check("no item", r.hasItem, false);
    CheckText("subject", r.subject, "your cut");
    CheckText("and an empty body", r.body, "");
}

void ASendCanBeTextOnly()
{
    MailRequest const r = ParseMailRequest("send subject:stop leaving fish in my bags");
    CheckVerb("a letter with nothing enclosed parses", r.verb, MailVerb::Send);
    Check("no item", r.hasItem, false);
    Check("no money", r.hasMoney, false);
    CheckText("subject", r.subject, "stop leaving fish in my bags");
}

void ASendCanCarryBoth()
{
    MailRequest const r = ParseMailRequest("send money:120 item:44 subject:here");
    CheckVerb("an item and money in one letter parses", r.verb, MailVerb::Send);
    CheckNumber("item guid still lands", r.itemGuid, 44);
    CheckNumber("money still lands", r.money, 120);
}

void FieldsMayComeInAnyOrderButTheTextIsLast()
{
    MailRequest const r = ParseMailRequest("send money:7 item:42 subject:a");
    CheckVerb("order of the keys does not matter", r.verb, MailVerb::Send);
    CheckNumber("guid", r.itemGuid, 42);
    CheckNumber("money", r.money, 7);
}

void TheTakeVerbsTakeIdsOnly()
{
    MailRequest const item = ParseMailRequest("take-item mail:9001 item:1303004");
    CheckVerb("take-item parses", item.verb, MailVerb::TakeItem);
    CheckNumber("mail id", item.mailId, 9001);
    CheckNumber("attachment guid", item.itemGuid, 1303004);

    MailRequest const money = ParseMailRequest("take-money mail:9001");
    CheckVerb("take-money parses", money.verb, MailVerb::TakeMoney);
    CheckNumber("mail id", money.mailId, 9001);

    MailRequest const ret = ParseMailRequest("return mail:77");
    CheckVerb("return parses", ret.verb, MailVerb::Return);
    CheckNumber("mail id", ret.mailId, 77);

    MailRequest const del = ParseMailRequest("delete mail:77");
    CheckVerb("delete parses", del.verb, MailVerb::Delete);
    CheckNumber("mail id", del.mailId, 77);
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
        {"post subject:hello", "an unknown verb"},
        {"send", "a send with no subject"},
        {"send item:5", "a send that encloses an item and says nothing"},
        {"send item:5 item:6 subject:a", "a repeated field"},
        {"send mail:5 subject:a", "a field the verb does not take"},
        {"send item:x subject:a", "a non-numeric value"},
        {"send item:-1 subject:a", "a signed value"},
        {"send item 5 subject:a", "a field with no colon"},
        {"send item:0 subject:a", "a zero item guid"},
        {"send item:99999999999 subject:a", "a value past uint32"},
        {"take-item mail:0 item:1", "a zero mail id"},
        {"take-item mail:1", "a take-item short of its attachment"},
        {"take-money", "a take-money with no mail"},
        {"delete mail:1 item:2", "a delete carrying an item"},
        {"return mail:1 money:5", "a return carrying money"},
        {"delete mail:1 subject:bye", "text on a verb that posts nothing"},
    };
    for (Case const& c : cases)
    {
        MailRequest const r = ParseMailRequest(c.line);
        CheckVerb(c.why, r.verb, MailVerb::None);
        CheckText(c.why, r.error, MailRefusal::Malformed);
    }
}

// ------------------------------------------------------------- the text ---

void TheFirstBodyMarkerSplits()
{
    MailRequest const r = ParseMailRequest("send subject:one body:two body:three");
    CheckVerb("a second body marker is just text", r.verb, MailVerb::Send);
    CheckText("the subject stops at the first marker", r.subject, "one");
    CheckText("and everything after it is the body", r.body, "two body:three");
}

void ASubjectMayContainColonsAndKeywords()
{
    MailRequest const r = ParseMailRequest("send subject:og: bring money:soon please");
    CheckVerb("a colon in the text is text", r.verb, MailVerb::Send);
    CheckText("the whole tail is the subject", r.subject, "og: bring money:soon please");
    Check("and no money was enclosed", r.hasMoney, false);
}

void SubjectIsFoundOnlyAtAWordBoundary()
{
    MailRequest const r = ParseMailRequest("send subject:resubject: not a split point");
    CheckVerb("a later subject: inside the text does not re-split", r.verb, MailVerb::Send);
    CheckText("the subject is the whole tail", r.subject, "resubject: not a split point");
}

void TextIsTrimmed()
{
    MailRequest const r = ParseMailRequest("send subject:   spaced out    body:   and so is this   ");
    CheckText("the subject is trimmed", r.subject, "spaced out");
    CheckText("and so is the body", r.body, "and so is this");
}

void AnEmptySubjectIsRefusedByName()
{
    MailRequest const blank = ParseMailRequest("send item:5 subject:");
    CheckVerb("a subject with nothing in it is refused", blank.verb, MailVerb::None);
    CheckText("by name, not as malformed", blank.error, MailRefusal::NoSubject);

    MailRequest const spaces = ParseMailRequest("send item:5 subject:    body:only a body");
    CheckVerb("a subject of nothing but spaces is refused", spaces.verb, MailVerb::None);
    CheckText("by name", spaces.error, MailRefusal::NoSubject);
}

void TheClientsOwnLengthsAreEnforced()
{
    std::string const sixtyFour(OverseerDecisions::MAIL_SUBJECT_MAX, 'a');
    MailRequest const atLimit = ParseMailRequest("send subject:" + sixtyFour);
    CheckVerb("a subject at the client limit is fine", atLimit.verb, MailVerb::Send);
    CheckNumber("and arrives whole", atLimit.subject.size(), OverseerDecisions::MAIL_SUBJECT_MAX);

    MailRequest const overLimit = ParseMailRequest("send subject:" + sixtyFour + "a");
    CheckVerb("one character more is refused", overLimit.verb, MailVerb::None);
    CheckText("by name", overLimit.error, MailRefusal::SubjectTooLong);

    std::string const body(OverseerDecisions::MAIL_BODY_MAX + 1, 'b');
    MailRequest const longBody = ParseMailRequest("send subject:hi body:" + body);
    CheckVerb("an over-long body is refused", longBody.verb, MailVerb::None);
    CheckText("by name", longBody.error, MailRefusal::BodyTooLong);
}

void TheSequenceThatCrashesTheClientIsRefused()
{
    MailRequest const inSubject = ParseMailRequest("send subject:look at this | | thing");
    CheckVerb("the crash sequence in a subject is refused", inSubject.verb, MailVerb::None);
    CheckText("by name", inSubject.error, MailRefusal::TextNotRenderable);

    // The core's own guard reads `body` before `body` has been read off the
    // wire (MailHandler.cpp:76-80), so upstream this one gets through and is
    // stored. Refusing it here is a deliberate difference from the handler.
    MailRequest const inBody = ParseMailRequest("send subject:hi body:and | | here");
    CheckVerb("and in a body, which the core's own check misses", inBody.verb, MailVerb::None);
    CheckText("by name", inBody.error, MailRefusal::TextNotRenderable);
}

// ------------------------------------------------------- money and dues ---

void ZeroMoneyIsNotNoMoney()
{
    MailRequest const zero = ParseMailRequest("send money:0 subject:nothing");
    CheckVerb("an explicit zero is refused", zero.verb, MailVerb::None);
    CheckText("by name, so it is not read as a plain letter", zero.error, MailRefusal::ZeroMoney);

    MailRequest const absent = ParseMailRequest("send subject:nothing");
    CheckVerb("while omitting the key is a plain letter", absent.verb, MailVerb::Send);
}

void CashOnDeliveryIsRefusedByName()
{
    MailRequest const r = ParseMailRequest("send item:5 cod:500 subject:pay up");
    CheckVerb("a cod field is refused", r.verb, MailVerb::None);
    CheckText("by its own name and not as malformed", r.error, MailRefusal::CodNotSupported);

    MailRequest const nonsense = ParseMailRequest("send item:5 cod:notanumber subject:pay up");
    CheckVerb("even when the value is nonsense", nonsense.verb, MailVerb::None);
    CheckText("the reason is still the unsupported field", nonsense.error,
              MailRefusal::CodNotSupported);
}

void WhatALetterCostsToPost()
{
    uint32_t cost = 0;

    Check("a letter with nothing enclosed costs postage", MailTotalCost(0, false, cost), true);
    CheckNumber("which is 30 copper", cost, 30);

    Check("a letter with one item costs the same postage", MailTotalCost(0, true, cost), true);
    CheckNumber("30 copper", cost, 30);

    Check("enclosed money is on top of the postage", MailTotalCost(5000, true, cost), true);
    CheckNumber("5030 copper", cost, 5030);

    // MailHandler.cpp:168-172. The handler tests the wrapped sum and refuses
    // without a word; here the sum is done wide and the refusal has a name.
    cost = 12345;
    Check("a sum past the cap does not wrap", MailTotalCost(4294967295u, true, cost), false);
    CheckNumber("and the caller's cost is left alone to say so", cost, 12345);

    Check("one copper short of the cap still fits", MailTotalCost(4294967265u, false, cost), true);
    CheckNumber("exactly at the cap", cost, 4294967295u);
}

// ------------------------------------------------------------ the delay ---

void OnlyACrossAccountItemWaits()
{
    uint32_t const hour = 3600;

    CheckNumber("an item to another account waits the configured delay",
                MailDeliveryDelaySeconds(true, false, hour), hour);
    CheckNumber("the same item inside one account does not",
                MailDeliveryDelaySeconds(true, true, hour), 0);
    CheckNumber("money to another account does not wait",
                MailDeliveryDelaySeconds(false, false, hour), 0);
    CheckNumber("nor does a letter with nothing in it",
                MailDeliveryDelaySeconds(false, true, hour), 0);
    CheckNumber("a realm that configured no delay has none",
                MailDeliveryDelaySeconds(true, false, 0), 0);
}

// -------------------------------------------------------------- retryable --

void WhichRefusalsMove()
{
    char const* const moving[] = {
        MailRefusal::NotInWorld,
        MailRefusal::Dead,
        MailRefusal::InFlight,
        MailRefusal::Stunned,
        MailRefusal::LoggingOut,
        MailRefusal::InCombat,
        MailRefusal::Trading,
        MailRefusal::NoMailbox,
        MailRefusal::RecipientOffline,
        MailRefusal::RecipientFull,
        MailRefusal::CannotAffordPost,
        MailRefusal::MailNotDelivered,
        MailRefusal::NoRoom,
        MailRefusal::TooMuchGold,
    };
    for (char const* reason : moving)
        Check(reason, MailRefusalRetryable(reason), true);

    char const* const standing[] = {
        MailRefusal::Malformed,
        MailRefusal::NoSubject,
        MailRefusal::SubjectTooLong,
        MailRefusal::BodyTooLong,
        MailRefusal::TextNotRenderable,
        MailRefusal::ZeroMoney,
        MailRefusal::MoneyTooHigh,
        MailRefusal::CodNotSupported,
        MailRefusal::NoSession,
        MailRefusal::BelowLevel,
        MailRefusal::NoRecipient,
        MailRefusal::RecipientIsSelf,
        MailRefusal::WrongTeam,
        MailRefusal::ItemNotCarried,
        MailRefusal::ItemNoTemplate,
        MailRefusal::ItemNotEmptyBag,
        MailRefusal::ItemSoulbound,
        MailRefusal::ItemNotTradable,
        MailRefusal::ItemConjured,
        MailRefusal::ItemQuest,
        MailRefusal::ItemBeingLooted,
        MailRefusal::ItemRefundable,
        MailRefusal::MailNotFound,
        MailRefusal::MailDeleted,
        MailRefusal::MailIsCod,
        MailRefusal::MailFromSystem,
        MailRefusal::MailAlreadyReturned,
        MailRefusal::MailStillHasItems,
        MailRefusal::MailStillHasMoney,
        MailRefusal::AttachmentNotInMail,
        MailRefusal::AttachmentMissing,
        MailRefusal::NoMoneyInMail,
        MailRefusal::CoreRefused,
        MailRefusal::NotReadBack,
    };
    for (char const* reason : standing)
        Check(reason, MailRefusalRetryable(reason), false);

    Check("an unknown reason is not retried", MailRefusalRetryable("something new"), false);
    Check("nor is an empty one", MailRefusalRetryable(""), false);
}

}  // namespace

int main()
{
    ASendCarriesAnItemAndAMessage();
    ASendCanBeMoneyOnly();
    ASendCanBeTextOnly();
    ASendCanCarryBoth();
    FieldsMayComeInAnyOrderButTheTextIsLast();
    TheTakeVerbsTakeIdsOnly();
    WhatDoesNotParse();
    TheFirstBodyMarkerSplits();
    ASubjectMayContainColonsAndKeywords();
    SubjectIsFoundOnlyAtAWordBoundary();
    TextIsTrimmed();
    AnEmptySubjectIsRefusedByName();
    TheClientsOwnLengthsAreEnforced();
    TheSequenceThatCrashesTheClientIsRefused();
    ZeroMoneyIsNotNoMoney();
    CashOnDeliveryIsRefusedByName();
    WhatALetterCostsToPost();
    OnlyACrossAccountItemWaits();
    WhichRefusalsMove();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the mail decisions hold\n");
    return EXIT_SUCCESS;
}
