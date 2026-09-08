/*
 * Conjuring the family's food and water, and the loop that decides when to stop.
 *
 * mod-overseer#147. The roster carried zero items of food and zero of drink
 * across all five characters, measured with the game's own classification
 * (item_template.spellcategory_1 of 11 and 59) rather than with a list of
 * remembered item ids. The party leader is a mage who has known how to conjure
 * both since level 6 and level 4 and had never once done either, because
 * upstream registers the triggers ("no food", "no drink") and registers the
 * actions ("conjure food", "conjure water") and never pushes a TriggerNode
 * connecting any of them. patches/mod-playerbots/0013 is that missing wire.
 *
 * This file is the other half: a party's worth of stock is a decision about a
 * party, and a mage's own appetite cannot take it. What is pinned here is how
 * much to make, how many casts that is, when to cast again, when to stop
 * casting because nothing is appearing, and what the row is allowed to claim
 * afterwards.
 *
 * THE NUMBERS ARE MEASURED, NOT INVENTED. Two units per three second cast and a
 * stack of twenty are read off Spell.dbc and item_template at the pinned build.
 * Where a test depends on one of them it says so, so a failure reads as the
 * rule and not as an arbitrary constant.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ClassRestoresManaByDrinking;
using OverseerDecisions::CONJURE_UNITS_DEFAULT;
using OverseerDecisions::CONJURE_UNITS_MAX;
using OverseerDecisions::ConjureNextStep;
using OverseerDecisions::ConjureOutcome;
using OverseerDecisions::ConjureOutcomeWord;
using OverseerDecisions::ConjurePlan;
using OverseerDecisions::ConjureProgress;
using OverseerDecisions::ConjureReadBack;
using OverseerDecisions::ConjureRefusalRetry;
using OverseerDecisions::ConjureRequest;
using OverseerDecisions::ConjureStep;
using OverseerDecisions::ConjureStepWord;
using OverseerDecisions::ConjureVerifyWindowMs;
using OverseerDecisions::ConjureWhat;
using OverseerDecisions::ConjureWhatWord;
using OverseerDecisions::CONSUMABLE_CATEGORY_DRINK;
using OverseerDecisions::CONSUMABLE_CATEGORY_FOOD;
using OverseerDecisions::ParseConjureRequest;
using OverseerDecisions::PlanConjure;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

namespace ConjureRefusal = OverseerDecisions::ConjureRefusal;

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

void CheckNum(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, unsigned(got), unsigned(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

void CheckStep(char const* what, ConjureStep got, ConjureStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, ConjureStepWord(got),
                ConjureStepWord(want));
    ++failures;
}

void CheckOutcome(char const* what, ConjureOutcome got, ConjureOutcome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, ConjureOutcomeWord(got),
                ConjureOutcomeWord(want));
    ++failures;
}

void CheckRetry(char const* what, std::string const& detail, TownRetry want)
{
    TownRetry const got = ConjureRefusalRetry(detail);
    if (got == want)
        return;
    std::printf("FAIL %s (%s): got %s, wanted %s\n", what, detail.c_str(),
                TownRetryWord(got), TownRetryWord(want));
    ++failures;
}

// What one cast of the ranks this family actually knows produces, off Spell.dbc
// at the pinned build: Conjure Food ranks 1 to 3 and Conjure Water ranks 1 to 3
// all create two items. Named so a failure reads as the measurement.
constexpr uint32_t PER_CAST_AT_THIS_LEVEL = 2;

// item_template.stackable on every Conjured * row.
constexpr uint32_t STACK = 20;

// ------------------------------------------------------------------ parsing --

void TheGrammarIsOneOfTwoWordsAndNothingElse()
{
    ConjureRequest const food = ParseConjureRequest("food");
    Check("food parses", food.what == ConjureWhat::Food, true);
    Check("food carries no error", food.error.empty(), true);
    Check("food is not capped", food.capped, false);
    CheckNum("food defaults to one stack", food.upTo, CONJURE_UNITS_DEFAULT);

    ConjureRequest const water = ParseConjureRequest("water");
    Check("water parses", water.what == ConjureWhat::Water, true);
    CheckNum("water defaults to one stack", water.upTo, CONJURE_UNITS_DEFAULT);

    ConjureRequest const spaced = ParseConjureRequest("   water   ");
    Check("leading and trailing blanks are tolerated", spaced.what == ConjureWhat::Water,
          true);

    ConjureRequest const tabbed = ParseConjureRequest("food\tup_to:40");
    Check("a tab separates words", tabbed.what == ConjureWhat::Food, true);
    CheckNum("and the target comes through", tabbed.upTo, 40);
    Check("and it reads as capped", tabbed.capped, true);
}

// AN EMPTY COMMAND IS NOT A CONJURE, which is the one place this grammar
// deliberately differs from the hearth's. A hearthstone can do exactly one
// thing, so an empty row can only have meant that thing. This verb can do two,
// and an empty row that guessed would be this module choosing what a family
// eats.
void NothingAtAllIsNotAConjure()
{
    ConjureRequest const empty = ParseConjureRequest("");
    Check("empty is refused", empty.what == ConjureWhat::None, true);
    CheckWord("and it is named malformed", empty.error.c_str(), ConjureRefusal::Malformed);

    ConjureRequest const blanks = ParseConjureRequest("   \t  ");
    Check("whitespace is refused too", blanks.what == ConjureWhat::None, true);
}

void EveryOtherFormIsRefusedRatherThanGuessedAt()
{
    char const* const bad[] = {
        "drink",              // the game's word for the category, not this verb's
        "Food",               // literal lower case, like every other grammar here
        "food water",         // two things in one row
        "food 40",            // a bare number is not a target
        "food up_to",         // a key with no value
        "food up_to:",        // and a key with an empty value
        "food up_to:x",       // and a value that is not digits
        "food count:20",      // the buy grammar's key, which means something else
        "up_to:20",           // a target with nothing to make
        "food up_to:20 up_to:20",  // said twice, and the two could have disagreed
        "food up_to:0",       // a row that asks for nothing
        "food up_to:101",     // over the ceiling, which is nearly always a typo
        "food up_to:20 now",  // a third word
    };
    for (char const* line : bad)
    {
        ConjureRequest const request = ParseConjureRequest(line);
        Check(line, request.what == ConjureWhat::None, true);
        Check("a refused parse names itself", request.error.empty(), false);
    }
}

void TheCeilingIsInclusive()
{
    ConjureRequest const atLimit =
        ParseConjureRequest("water up_to:" + std::to_string(CONJURE_UNITS_MAX));
    Check("the ceiling itself is allowed", atLimit.what == ConjureWhat::Water, true);
    CheckNum("and comes through whole", atLimit.upTo, CONJURE_UNITS_MAX);
    CheckNum("which is five stacks", CONJURE_UNITS_MAX, 5 * STACK);
}

void TheWordsAreTheWordsARowCarries()
{
    CheckWord("food", ConjureWhatWord(ConjureWhat::Food), "food");
    CheckWord("water", ConjureWhatWord(ConjureWhat::Water), "water");
    CheckWord("none", ConjureWhatWord(ConjureWhat::None), "none");
    CheckWord("cast", ConjureStepWord(ConjureStep::Cast), "cast");
    CheckWord("wait", ConjureStepWord(ConjureStep::Wait), "wait");
    CheckWord("done", ConjureStepWord(ConjureStep::Done), "done");
    CheckWord("gave up", ConjureStepWord(ConjureStep::GaveUp), "gave up");
    CheckWord("filled", ConjureOutcomeWord(ConjureOutcome::Filled), "filled");
    CheckWord("short", ConjureOutcomeWord(ConjureOutcome::Short), "short");
    CheckWord("nothing", ConjureOutcomeWord(ConjureOutcome::Nothing), "nothing");
    CheckWord("unreadable", ConjureOutcomeWord(ConjureOutcome::Unreadable), "unreadable");
}

// ------------------------------------------------------------- who drinks --

// THE ROSTER, BY CLASS ID out of the characters table, because "a warrior does
// not need water" is the rule and these five are what it is for.
void ARogueAndAWarriorGetNothingOutOfADrink()
{
    Check("Grug the warrior does not drink", ClassRestoresManaByDrinking(1), false);
    Check("Bork the rogue does not drink", ClassRestoresManaByDrinking(4), false);
    Check("a death knight does not drink", ClassRestoresManaByDrinking(6), false);

    Check("Grog the paladin drinks", ClassRestoresManaByDrinking(2), true);
    Check("Ugga the priest drinks", ClassRestoresManaByDrinking(5), true);
    Check("Og the mage drinks", ClassRestoresManaByDrinking(8), true);

    // The one that is easy to get wrong by carrying a later expansion's rule
    // across: in this one a hunter's shots cost mana.
    Check("a hunter drinks in this expansion", ClassRestoresManaByDrinking(3), true);

    Check("a shaman drinks", ClassRestoresManaByDrinking(7), true);
    Check("a warlock drinks", ClassRestoresManaByDrinking(9), true);
    Check("a druid drinks", ClassRestoresManaByDrinking(11), true);

    // Provisioning a class nobody has heard of is the case where doing nothing
    // is right.
    Check("class 0 is not a class", ClassRestoresManaByDrinking(0), false);
    Check("class 10 does not exist here", ClassRestoresManaByDrinking(10), false);
    Check("class 12 does not exist here", ClassRestoresManaByDrinking(12), false);
    Check("a wild id is not a drinker", ClassRestoresManaByDrinking(4000000000u), false);

    // Food is for everybody, which is why there is no matching question. Pinned
    // as the two categories rather than as prose so the pair cannot drift.
    CheckNum("food is spell category 11", CONSUMABLE_CATEGORY_FOOD, 11);
    CheckNum("drink is spell category 59", CONSUMABLE_CATEGORY_DRINK, 59);
}

// ------------------------------------------------------------- the plan --

void AStackIsTenCastsAtThisLevel()
{
    ConjurePlan const plan = PlanConjure(0, STACK, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("a full stack from empty", plan.casts, 10);
    CheckNum("aiming at the stack", plan.wanted, STACK);
    CheckNum("carrying none", plan.carried, 0);
    Check("there is something to do", plan.nothingToDo, false);
    Check("and the bags are not the limit", plan.roomLimited, false);
}

void OnlyTheShortfallIsCast()
{
    ConjurePlan const plan = PlanConjure(14, STACK, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("six short is three casts", plan.casts, 3);
    CheckNum("and the target does not move", plan.wanted, STACK);
}

// ROUNDING UP, AND WHY. Rounding down would leave a remainder no number of
// casts could ever reach, so the row would report `short` for ever against a
// target it was one unit away from.
void AnOddShortfallRoundsUp()
{
    ConjurePlan const plan = PlanConjure(0, 21, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("21 units at two a cast is eleven casts", plan.casts, 11);

    ConjurePlan const one = PlanConjure(19, STACK, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("one unit short is still a whole cast", one.casts, 1);
}

void EnoughAlreadyIsNoCastsAtAll()
{
    ConjurePlan const exact = PlanConjure(STACK, STACK, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("at the target, nothing to do", exact.casts, 0);
    Check("and it says so", exact.nothingToDo, true);

    ConjurePlan const over = PlanConjure(40, STACK, PER_CAST_AT_THIS_LEVEL, 100);
    CheckNum("above the target, nothing to do", over.casts, 0);
    Check("and it says so", over.nothingToDo, true);
    CheckNum("and the plan reports what is carried, not what was asked", over.wanted, 40);
}

// THE BAGS CLAMP THE TARGET RATHER THAN REFUSING IT. Six units is worth having.
// A refusal here would leave a party with nothing because a bag was nearly
// full, which is the outcome this whole verb exists to stop.
void ABagWithLittleRoomLowersTheTargetAndSaysSo()
{
    ConjurePlan const plan = PlanConjure(0, STACK, PER_CAST_AT_THIS_LEVEL, 6);
    Check("the bags set the target", plan.roomLimited, true);
    CheckNum("which is what fits", plan.wanted, 6);
    CheckNum("three casts, not ten", plan.casts, 3);
    Check("and there is still something to do", plan.nothingToDo, false);
}

void ABagWithNoRoomIsNothingToDoRatherThanAPlanOfZero()
{
    ConjurePlan const plan = PlanConjure(4, STACK, PER_CAST_AT_THIS_LEVEL, 0);
    Check("no room is nothing to do", plan.nothingToDo, true);
    CheckNum("and no casts", plan.casts, 0);
    Check("and the bags are named as the reason", plan.roomLimited, true);
}

// A spell whose effect this module could not read. The answer is zero casts,
// not a division by zero and not a loop with no end.
void ASpellThatMakesNothingIsNotDividedBy()
{
    ConjurePlan const plan = PlanConjure(0, STACK, 0, 100);
    CheckNum("no casts", plan.casts, 0);
    Check("and it is not reported as already stocked", plan.nothingToDo, false);
    CheckNum("per cast comes back as it went in", plan.perCast, 0);
}

// The level 60 ranks create ten a cast rather than two. Nothing in the rule
// knows that number, which is the point of reading it off the spell.
void AHigherRankNeedsFewerCasts()
{
    ConjurePlan const plan = PlanConjure(0, STACK, 10, 100);
    CheckNum("ten a cast fills a stack in two", plan.casts, 2);
}

// ---------------------------------------------------------------- the loop --

ConjureProgress Running(uint32_t carried, uint32_t spent)
{
    ConjureProgress p;
    p.carried = carried;
    p.wanted = STACK;
    p.castsSpent = spent;
    p.castsAllowed = 12;  // ten casts plus a little slack
    p.idlePolls = 0;
    p.idleLimit = 3;
    p.castInFlight = false;
    return p;
}

void AFreshRowCasts()
{
    CheckStep("nothing carried, nothing spent", ConjureNextStep(Running(0, 0)),
              ConjureStep::Cast);
    CheckStep("part way through", ConjureNextStep(Running(8, 4)), ConjureStep::Cast);
}

// DONE IS ASKED BEFORE THE CAST IN FLIGHT. The cast that is finishing right now
// is the one that reached the target; parking for another poll would learn
// nothing and cost three more seconds of a character standing still.
void ReachingTheTargetEndsItEvenMidCast()
{
    ConjureProgress p = Running(STACK, 10);
    p.castInFlight = true;
    CheckStep("at the target while casting", ConjureNextStep(p), ConjureStep::Done);

    ConjureProgress over = Running(STACK + 4, 10);
    CheckStep("above the target", ConjureNextStep(over), ConjureStep::Done);
}

// A CAST IN FLIGHT BEATS BOTH GIVE-UP TESTS, because a cast in flight is
// progress. Charging a three second cast against a budget counted in polls is
// how a loop that is working gets killed for being slow.
void ACastInFlightIsWaitedForEvenWithTheBudgetSpent()
{
    ConjureProgress p = Running(6, 12);
    p.castInFlight = true;
    CheckStep("budget spent but still casting", ConjureNextStep(p), ConjureStep::Wait);

    ConjureProgress idle = Running(6, 4);
    idle.idlePolls = 9;
    idle.castInFlight = true;
    CheckStep("idle count high but still casting", ConjureNextStep(idle),
              ConjureStep::Wait);
}

void TheBudgetEndsARowThatIsNotGettingThere()
{
    CheckStep("every cast spent, still short", ConjureNextStep(Running(12, 12)),
              ConjureStep::GaveUp);
    ConjureProgress beyond = Running(12, 40);
    CheckStep("and past it", ConjureNextStep(beyond), ConjureStep::GaveUp);
}

// THE TEST THAT MATTERS. Out of mana, interrupted by a fight, bags filled by
// something else, a rank whose product this character may not use: from the
// queue all four look the same, which is a cast that goes out and produces
// nothing. AGENTS.md's rule is that the world has to be read back, and this is
// that rule as a loop.
void CastsThatProduceNothingEndTheRow()
{
    ConjureProgress p = Running(0, 3);
    p.idlePolls = 3;
    CheckStep("three casts, nothing appeared", ConjureNextStep(p), ConjureStep::GaveUp);

    ConjureProgress nearly = Running(0, 2);
    nearly.idlePolls = 2;
    CheckStep("two is still under the limit", ConjureNextStep(nearly), ConjureStep::Cast);
}

void AnIdleLimitOfZeroTurnsTheTestOffRatherThanGivingUpFirstThing()
{
    ConjureProgress p = Running(0, 0);
    p.idleLimit = 0;
    p.idlePolls = 0;
    CheckStep("disabled, not tripped", ConjureNextStep(p), ConjureStep::Cast);

    ConjureProgress spent = Running(0, 0);
    spent.idleLimit = 0;
    spent.idlePolls = 99;
    CheckStep("still disabled with a high count", ConjureNextStep(spent),
              ConjureStep::Cast);
}

// A target of zero has nothing to be reached, and answering Done to it would
// end a row on a comparison that means nothing. The parse refuses `up_to:0`, so
// this is only reachable through a request built by hand.
void ATargetOfZeroIsNotSilentlyDone()
{
    ConjureProgress p = Running(0, 0);
    p.wanted = 0;
    CheckStep("no target is not a finished row", ConjureNextStep(p), ConjureStep::Cast);
}

// ------------------------------------------------------------- the verdict --

void TheBagsDecideWhatTheRowMayClaim()
{
    CheckOutcome("at the target", ConjureReadBack(true, 0, STACK, STACK),
                 ConjureOutcome::Filled);
    CheckOutcome("past the target", ConjureReadBack(true, 0, STACK + 2, STACK),
                 ConjureOutcome::Filled);
    CheckOutcome("some but not all", ConjureReadBack(true, 0, 8, STACK),
                 ConjureOutcome::Short);
    CheckOutcome("one unit is still some", ConjureReadBack(true, 0, 1, STACK),
                 ConjureOutcome::Short);
    CheckOutcome("not one thing appeared", ConjureReadBack(true, 0, 0, STACK),
                 ConjureOutcome::Nothing);
    CheckOutcome("started with some and gained none",
                 ConjureReadBack(true, 6, 6, STACK), ConjureOutcome::Nothing);
}

// THE CHARACTER EATS WHILE THE ROW RUNS. It is a bot with a strategy that eats
// on low health, so the count can fall. Nothing was gained, which is what the
// word means, and reporting it as a negative or as `short` would both be wrong.
void EatingTheStackMidRowIsNothingGainedAndNotAnError()
{
    CheckOutcome("fewer than before", ConjureReadBack(true, 10, 4, STACK),
                 ConjureOutcome::Nothing);
}

// An honest zero is a reading. Unreadable is only for a count that could not be
// taken at all, and it must never be reported as any of the other three.
void UnreadableIsAboutTheReadingAndNotAboutTheResult()
{
    CheckOutcome("no template, no count", ConjureReadBack(false, 0, 0, STACK),
                 ConjureOutcome::Unreadable);
    CheckOutcome("unreadable even when the numbers would say filled",
                 ConjureReadBack(false, 0, STACK, STACK), ConjureOutcome::Unreadable);
    CheckOutcome("a row with no target cannot be judged",
                 ConjureReadBack(true, 0, 5, 0), ConjureOutcome::Unreadable);
}

// ---------------------------------------------------------------- the window --

void TheWindowIsPerCastAndMultiplied()
{
    // Ten casts of three seconds, with a second of margin each: a hearth's
    // single-cast window would judge this one after three seconds and always
    // answer `nothing`.
    CheckNum("ten three second casts", ConjureVerifyWindowMs(3000, 10, 1000, 6000), 40000);
    CheckNum("one cast", ConjureVerifyWindowMs(3000, 1, 1000, 6000), 6000);
    CheckNum("two casts clear the floor", ConjureVerifyWindowMs(3000, 2, 1000, 6000),
             8000);
}

void AZeroCastTimeCannotProduceAWindowThatJudgesInstantly()
{
    CheckNum("no cast time at all", ConjureVerifyWindowMs(0, 1, 0, 6000), 6000);
    CheckNum("and no margin either", ConjureVerifyWindowMs(0, 0, 0, 6000), 6000);
    // A count of zero is treated as one rather than as no window at all: a row
    // that got here with no casts planned still has to be answered.
    CheckNum("zero casts still gets one cast's window",
             ConjureVerifyWindowMs(3000, 0, 1000, 100), 4000);
}

void NonsenseInputsSaturateRatherThanWrap()
{
    uint32_t const huge = 0xFFFFFFFFu;
    CheckNum("a nonsense cast time", ConjureVerifyWindowMs(huge, 10, 1000, 6000), huge);
    CheckNum("a nonsense cast count", ConjureVerifyWindowMs(3000, huge, 1000, 6000), huge);
    CheckNum("a nonsense margin", ConjureVerifyWindowMs(3000, 10, huge, 6000), huge);
    CheckNum("a nonsense floor", ConjureVerifyWindowMs(1, 1, 1, huge), huge);
}

// ---------------------------------------------------------------- retrying --

void TheRowAndTheSpellbookAreNeverWorthAskingAgain()
{
    CheckRetry("malformed", ConjureRefusal::Malformed, TownRetry::Never);
    CheckRetry("knows no such spell", ConjureRefusal::CannotConjure, TownRetry::Never);
    CheckRetry("core has no spell info", ConjureRefusal::NoSpellInfo, TownRetry::Never);
    CheckRetry("item has no template", ConjureRefusal::NoItemTemplate, TownRetry::Never);
    CheckRetry("may not use the product", ConjureRefusal::CannotUseItem,
               TownRetry::Never);
    CheckRetry("the spell creates nothing", ConjureRefusal::MakesNothing,
               TownRetry::Never);
}

void EverythingTheCharacterIsDoingEndsOnItsOwn()
{
    CheckRetry("no session", ConjureRefusal::NoSession, TownRetry::Later);
    CheckRetry("not in world", ConjureRefusal::NotInWorld, TownRetry::Later);
    CheckRetry("dead", ConjureRefusal::Dead, TownRetry::Later);
    CheckRetry("in combat", ConjureRefusal::InCombat, TownRetry::Later);
    CheckRetry("in flight", ConjureRefusal::InFlight, TownRetry::Later);
    CheckRetry("stunned", ConjureRefusal::Stunned, TownRetry::Later);
    CheckRetry("logging out", ConjureRefusal::LoggingOut, TownRetry::Later);
    CheckRetry("trading", ConjureRefusal::Trading, TownRetry::Later);
    CheckRetry("already casting", ConjureRefusal::AlreadyCasting, TownRetry::Later);
    CheckRetry("no bot ai", ConjureRefusal::NoBotAI, TownRetry::Later);
    CheckRetry("no room", ConjureRefusal::NoRoom, TownRetry::Later);
    CheckRetry("enough already", ConjureRefusal::EnoughAlready, TownRetry::Later);
    CheckRetry("the casts produced nothing", ConjureRefusal::NothingAppeared,
               TownRetry::Later);
}

// NO REFUSAL HERE IS EVER `Elsewhere`, and it is checked rather than left as an
// empty list somebody later reads as an oversight. Every other errand in the
// town trip needs an NPC and can therefore be fixed by walking. A conjure needs
// nobody, which is most of why it is worth having for a family that cannot
// reliably walk anywhere.
void WalkingSomewhereElseFixesNoConjure()
{
    char const* const every[] = {
        ConjureRefusal::Malformed,      ConjureRefusal::CannotConjure,
        ConjureRefusal::NoSpellInfo,    ConjureRefusal::NoItemTemplate,
        ConjureRefusal::CannotUseItem,  ConjureRefusal::MakesNothing,
        ConjureRefusal::NoSession,
        ConjureRefusal::NotInWorld,     ConjureRefusal::Dead,
        ConjureRefusal::InCombat,       ConjureRefusal::InFlight,
        ConjureRefusal::Stunned,        ConjureRefusal::LoggingOut,
        ConjureRefusal::Trading,        ConjureRefusal::AlreadyCasting,
        ConjureRefusal::NoBotAI,        ConjureRefusal::NoRoom,
        ConjureRefusal::EnoughAlready,  ConjureRefusal::NothingAppeared,
    };
    for (char const* literal : every)
    {
        if (ConjureRefusalRetry(literal) == TownRetry::Elsewhere)
        {
            std::printf("FAIL %s is classed elsewhere; a conjure needs no NPC\n", literal);
            ++failures;
        }
        // The literals go straight into an UPDATE, so none of them may carry a
        // quote. Checked here because this is the one place that names all of
        // them.
        if (std::string(literal).find('\'') != std::string::npos ||
            std::string(literal).find('"') != std::string::npos)
        {
            std::printf("FAIL refusal literal carries a quote: %s\n", literal);
            ++failures;
        }
    }
}

void AnUnknownRefusalIsWorthOneMoreTry()
{
    CheckRetry("never heard of it", "the sky turned green", TownRetry::Later);
    CheckRetry("empty", "", TownRetry::Later);
}

}  // namespace

int main()
{
    TheGrammarIsOneOfTwoWordsAndNothingElse();
    NothingAtAllIsNotAConjure();
    EveryOtherFormIsRefusedRatherThanGuessedAt();
    TheCeilingIsInclusive();
    TheWordsAreTheWordsARowCarries();

    ARogueAndAWarriorGetNothingOutOfADrink();

    AStackIsTenCastsAtThisLevel();
    OnlyTheShortfallIsCast();
    AnOddShortfallRoundsUp();
    EnoughAlreadyIsNoCastsAtAll();
    ABagWithLittleRoomLowersTheTargetAndSaysSo();
    ABagWithNoRoomIsNothingToDoRatherThanAPlanOfZero();
    ASpellThatMakesNothingIsNotDividedBy();
    AHigherRankNeedsFewerCasts();

    AFreshRowCasts();
    ReachingTheTargetEndsItEvenMidCast();
    ACastInFlightIsWaitedForEvenWithTheBudgetSpent();
    TheBudgetEndsARowThatIsNotGettingThere();
    CastsThatProduceNothingEndTheRow();
    AnIdleLimitOfZeroTurnsTheTestOffRatherThanGivingUpFirstThing();
    ATargetOfZeroIsNotSilentlyDone();

    TheBagsDecideWhatTheRowMayClaim();
    EatingTheStackMidRowIsNothingGainedAndNotAnError();
    UnreadableIsAboutTheReadingAndNotAboutTheResult();

    TheWindowIsPerCastAndMultiplied();
    AZeroCastTimeCannotProduceAWindowThatJudgesInstantly();
    NonsenseInputsSaturateRatherThanWrap();

    TheRowAndTheSpellbookAreNeverWorthAskingAgain();
    EverythingTheCharacterIsDoingEndsOnItsOwn();
    WalkingSomewhereElseFixesNoConjure();
    AnUnknownRefusalIsWorthOneMoreTry();

    if (failures)
    {
        std::printf("%d conjure check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the conjure decisions hold\n");
    return EXIT_SUCCESS;
}
