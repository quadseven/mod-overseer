/*
 * A quest item its holder is done with may be sold, and one nothing will buy
 * may be destroyed (#614). Exercised without a world.
 *
 * WHAT IS PINNED HERE:
 *
 *   - QuestItemStillNeeded keeps a stack an open quest names, and a starter
 *     for a quest the holder has not been rewarded for or could take again.
 *     Everything else is released, including a starter whose quest the core
 *     does not know.
 *   - SellQuestRefusal gates ITEM_CLASS_QUEST (12) only. Before #614 every
 *     class-12 stack was refused; a released one now sells.
 *   - The destroy grammar is `destroy guid:<n> count:<n>` with optional
 *     `allow:bound` and `allow:quality`, and nothing else.
 *   - DestroyRefusal refuses equipped items, quest-held items, priced items,
 *     soulbound gear and above-Uncommon quality unless allowed, and any
 *     count that is not exactly the stack.
 *   - Each new refusal literal is classified `never`, because the same row
 *     meets it everywhere.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DestroyFacts;
using OverseerDecisions::DestroyRefusal;
using OverseerDecisions::DestroySpec;
using OverseerDecisions::IsDestroyRow;
using OverseerDecisions::ParseDestroySpec;
using OverseerDecisions::QuestItemHold;
using OverseerDecisions::QuestItemHolderFacts;
using OverseerDecisions::QuestItemHoldWord;
using OverseerDecisions::QuestItemStillNeeded;
using OverseerDecisions::SellQuestRefusal;
using OverseerDecisions::SellRefusalRetry;
using OverseerDecisions::SellRetry;
namespace R = OverseerDecisions::DestroyRefusalText;

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

void CheckWord(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// Un'Goro Soil, as the entry the test uses; any number would do.
constexpr uint32_t SOIL = 11018;
constexpr uint32_t QUEST_CLASS = 12;

// ---- is the stack still needed ---------------------------------------------

void ALeftoverFromAFinishedQuestIsReleased()
{
    QuestItemHolderFacts facts;
    facts.activeQuestItems = {2589, 4306};
    Check("no open quest names it", QuestItemStillNeeded(SOIL, facts) == QuestItemHold::Released,
          true);
    CheckWord("and the word says so", QuestItemHoldWord(QuestItemStillNeeded(SOIL, facts)),
              "released");
}

void AnOpenQuestThatNamesItKeepsIt()
{
    QuestItemHolderFacts facts;
    facts.activeQuestItems = {2589, SOIL};
    Check("an open quest names it", QuestItemStillNeeded(SOIL, facts) == QuestItemHold::ActiveQuest,
          true);
    CheckWord("word", QuestItemHoldWord(QuestItemHold::ActiveQuest), "active quest");
}

void AnEmptySlotInTheQuestListHoldsNoRealItem()
{
    QuestItemHolderFacts facts;
    facts.activeQuestItems = {0};
    Check("a real entry is not held by an empty objective",
          QuestItemStillNeeded(SOIL, facts) == QuestItemHold::Released, true);
    // The rule has no zero guard of its own; a leaked zero fails closed.
    Check("a zero entry against a leaked zero is held",
          QuestItemStillNeeded(0, facts) == QuestItemHold::ActiveQuest, true);
}

void AStarterForAQuestNotYetDoneIsKept()
{
    QuestItemHolderFacts facts;
    facts.startQuest = 4141;
    facts.startQuestExists = true;
    facts.startQuestRewarded = false;
    Check("starter not yet rewarded",
          QuestItemStillNeeded(SOIL, facts) == QuestItemHold::AvailableStarter, true);
    CheckWord("word", QuestItemHoldWord(QuestItemHold::AvailableStarter), "available starter");
}

void AStarterForADoneQuestIsReleased()
{
    QuestItemHolderFacts facts;
    facts.startQuest = 4141;
    facts.startQuestExists = true;
    facts.startQuestRewarded = true;
    facts.startQuestTakeable = false;
    Check("starter already rewarded", QuestItemStillNeeded(SOIL, facts) == QuestItemHold::Released,
          true);
}

void AStarterForARepeatableQuestStillOnOfferIsKept()
{
    QuestItemHolderFacts facts;
    facts.startQuest = 4141;
    facts.startQuestExists = true;
    facts.startQuestRewarded = true;
    facts.startQuestTakeable = true;
    Check("rewarded but takeable again",
          QuestItemStillNeeded(SOIL, facts) == QuestItemHold::AvailableStarter, true);
}

void AStarterForAQuestTheCoreDoesNotKnowIsReleased()
{
    QuestItemHolderFacts facts;
    facts.startQuest = 999999;
    facts.startQuestExists = false;
    Check("no template, nothing to start",
          QuestItemStillNeeded(SOIL, facts) == QuestItemHold::Released, true);
}

// ---- the sale gate ---------------------------------------------------------

void AReleasedQuestItemSells()
{
    CheckWord("class 12, released", SellQuestRefusal(QUEST_CLASS, QuestItemHold::Released), "");
}

void AHeldQuestItemIsStillRefused()
{
    CheckWord("class 12, open quest", SellQuestRefusal(QUEST_CLASS, QuestItemHold::ActiveQuest),
              "item is a quest item");
    CheckWord("class 12, starter", SellQuestRefusal(QUEST_CLASS, QuestItemHold::AvailableStarter),
              "item is a quest item");
}

void OtherClassesAreUntouchedByTheGate()
{
    CheckWord("class 15 is not gated", SellQuestRefusal(15, QuestItemHold::ActiveQuest), "");
}

// ---- the destroy grammar ---------------------------------------------------

void ADestroyRowIsRoutedOnItsFirstWord()
{
    Check("destroy row", IsDestroyRow("destroy guid:5 count:3"), true);
    Check("malformed destroy still routed", IsDestroyRow("destroy"), true);
    Check("a sale is not a destroy", IsDestroyRow("guid:5 count:3"), false);
    Check("empty", IsDestroyRow(""), false);
}

void TheDestroyGrammar()
{
    DestroySpec spec = ParseDestroySpec("destroy guid:494263 count:99");
    Check("guid and count", spec.valid, true);
    Check("guid read", spec.guid == 494263, true);
    Check("count read", spec.count == 99, true);
    Check("nothing allowed by default", spec.allowBound || spec.allowQuality, false);

    spec = ParseDestroySpec("destroy guid:7 count:1 allow:bound allow:quality");
    Check("both allowances", spec.valid && spec.allowBound && spec.allowQuality, true);

    Check("count is required", ParseDestroySpec("destroy guid:7").valid, false);
    Check("count 0 refused", ParseDestroySpec("destroy guid:7 count:0").valid, false);
    Check("guid 0 refused", ParseDestroySpec("destroy guid:0 count:1").valid, false);
    Check("entry form refused", ParseDestroySpec("destroy entry:7 count:1").valid, false);
    Check("unknown allowance refused", ParseDestroySpec("destroy guid:7 count:1 allow:all").valid,
          false);
    Check("repeated allowance refused",
          ParseDestroySpec("destroy guid:7 count:1 allow:bound allow:bound").valid, false);
    Check("a sale row is not a destroy", ParseDestroySpec("guid:7 count:1").valid, false);
}

// ---- the destroy walls -----------------------------------------------------

DestroySpec Spec(uint32_t count, bool bound = false, bool quality = false)
{
    DestroySpec spec;
    spec.valid = true;
    spec.guid = 7;
    spec.count = count;
    spec.allowBound = bound;
    spec.allowQuality = quality;
    return spec;
}

DestroyFacts Leftover(uint32_t stack)
{
    DestroyFacts facts;
    facts.itemClass = QUEST_CLASS;
    facts.quality = 1;
    facts.sellPrice = 0;
    facts.stack = stack;
    facts.soulbound = true;  // quest items bind on pickup; that is not gear
    return facts;
}

void AnUnpricedReleasedQuestLeftoverIsDestroyed()
{
    CheckWord("the whole case", DestroyRefusal(Spec(99), Leftover(99)), "");
}

void AQuestHeldStackIsNeverDestroyed()
{
    DestroyFacts facts = Leftover(99);
    facts.questHold = QuestItemHold::ActiveQuest;
    CheckWord("open quest", DestroyRefusal(Spec(99), facts), R::Quest);
    facts.questHold = QuestItemHold::AvailableStarter;
    CheckWord("starter", DestroyRefusal(Spec(99, true, true), facts), R::Quest);
}

void APricedItemIsSoldNotDestroyed()
{
    DestroyFacts facts = Leftover(10);
    facts.sellPrice = 25;
    CheckWord("has a price", DestroyRefusal(Spec(10), facts), R::HasPrice);
}

void AnEquippedItemIsNeverDestroyed()
{
    DestroyFacts facts = Leftover(1);
    facts.equipped = true;
    CheckWord("equipped", DestroyRefusal(Spec(1, true, true), facts), R::Equipped);
}

void SoulboundGearNeedsTheRowToSaySo()
{
    DestroyFacts facts = Leftover(1);
    facts.itemClass = 4;  // armour
    CheckWord("bound armour refused", DestroyRefusal(Spec(1), facts), R::BoundGear);
    CheckWord("bound armour allowed", DestroyRefusal(Spec(1, true), facts), "");
    facts.itemClass = 2;  // weapon
    CheckWord("bound weapon refused", DestroyRefusal(Spec(1), facts), R::BoundGear);
    facts.soulbound = false;
    CheckWord("unbound weapon", DestroyRefusal(Spec(1), facts), "");
}

void AboveUncommonNeedsTheRowToSaySo()
{
    DestroyFacts facts = Leftover(1);
    facts.quality = 2;
    CheckWord("uncommon is fine", DestroyRefusal(Spec(1), facts), "");
    facts.quality = 3;
    CheckWord("rare refused", DestroyRefusal(Spec(1), facts), R::AboveUncommon);
    CheckWord("rare allowed", DestroyRefusal(Spec(1, false, true), facts), "");
}

void TheCountMustBeTheWholeStack()
{
    CheckWord("stack grew", DestroyRefusal(Spec(10), Leftover(12)), R::StackLarger);
    CheckWord("stack shrank", DestroyRefusal(Spec(10), Leftover(8)), R::CountExceeds);
}

void TheCoresOwnWallsAreNamed()
{
    DestroyFacts facts = Leftover(1);
    facts.noUserDestroy = true;
    CheckWord("no user destroy", DestroyRefusal(Spec(1, true, true), facts), R::NoDestroy);
    facts = Leftover(1);
    facts.nonEmptyBag = true;
    CheckWord("non-empty bag", DestroyRefusal(Spec(1), facts), R::NonEmptyBag);
    facts = Leftover(1);
    facts.beingLooted = true;
    CheckWord("being looted", DestroyRefusal(Spec(1), facts), R::BeingLooted);
    CheckWord("malformed", DestroyRefusal(DestroySpec{}, Leftover(1)), R::Malformed);
}

void TheNewLiteralsAreNeverRetried()
{
    char const* const never[] = {R::Malformed, R::Equipped,     R::HasPrice,   R::NoDestroy,
                                 R::BoundGear, R::AboveUncommon, R::StackLarger, R::Quest,
                                 R::CountExceeds};
    for (char const* literal : never)
    {
        std::string const what = std::string("never: ") + literal;
        Check(what.c_str(), SellRefusalRetry(literal) == SellRetry::Never, true);
    }
    Check("a transient stays later",
          SellRefusalRetry("the core refused the destroy") == SellRetry::Later, true);
}

}  // namespace

int main()
{
    ALeftoverFromAFinishedQuestIsReleased();
    AnOpenQuestThatNamesItKeepsIt();
    AnEmptySlotInTheQuestListHoldsNoRealItem();
    AStarterForAQuestNotYetDoneIsKept();
    AStarterForADoneQuestIsReleased();
    AStarterForARepeatableQuestStillOnOfferIsKept();
    AStarterForAQuestTheCoreDoesNotKnowIsReleased();
    AReleasedQuestItemSells();
    AHeldQuestItemIsStillRefused();
    OtherClassesAreUntouchedByTheGate();
    ADestroyRowIsRoutedOnItsFirstWord();
    TheDestroyGrammar();
    AnUnpricedReleasedQuestLeftoverIsDestroyed();
    AQuestHeldStackIsNeverDestroyed();
    APricedItemIsSoldNotDestroyed();
    AnEquippedItemIsNeverDestroyed();
    SoulboundGearNeedsTheRowToSaySo();
    AboveUncommonNeedsTheRowToSaySo();
    TheCountMustBeTheWholeStack();
    TheCoresOwnWallsAreNamed();
    TheNewLiteralsAreNeverRetried();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("quest item release: all checks passed\n");
    return 0;
}
