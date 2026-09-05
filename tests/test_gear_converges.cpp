/*
 * A gear choice that settles, and a drive that notices when it has been
 * overruled (#221).
 *
 * WHY THIS FILE EXISTS SEPARATELY FROM tests/test_gear.cpp. That one pins what
 * an item is WORTH: mail beats leather for a tank, stats beat two item levels
 * for a healer, a priest is never offered leather. This one pins what happens
 * over TIME, which is a different kind of claim and the one the family actually
 * lost days to.
 *
 * WHAT WAS MEASURED, because none of the numbers below are invented. On the dev
 * realm, twelve slots across four of the five characters filed 923 equip events
 * in the hours a flip happened. One of them, a level 29 protection warrior's
 * hands, alternated between the same two pairs of gloves 124 times - a level 23
 * RARE with 8 strength and 3 stamina, and a level 29 COMMON with 6 stamina and
 * 5 spirit, both 122 armour. Neither half was broken:
 *
 *   - this file's score prefers the rare (151.5 against 148.5) and its margin
 *     is one-way, so it can only ever move that slot TOWARDS the rare;
 *   - upstream's score multiplies an item's whole stat weight through by its
 *     item level, which makes the common win by 29/23, and its own 1.1x margin
 *     means it can only ever move that slot towards the COMMON.
 *
 * Two monotone rules in opposite directions on two independent triggers. Each
 * was right every time it fired, which is exactly why nothing looked wrong.
 *
 * SO THIS FILE ASSERTS TWO PROPERTIES AND NOT A LIST OF ARITHMETIC:
 *
 *   1. CONVERGENCE. GearCompare is antisymmetric - no two items can each be
 *      Better than the other - and a sweep that swaps therefore reaches a fixed
 *      point and stays there. Asserted over a grid rather than at a point,
 *      because a single example would pass on a rule that only happens to work
 *      at that example.
 *   2. THE DRIVE STANDS DOWN. If something else keeps undoing a swap, the drive
 *      gives up on that pair after a bounded number of attempts and says so,
 *      instead of arm-wrestling forever in silence.
 *
 * And one case that is not a property but is the visible cost: a rare whose
 * worth is partly in an on-equip effect, scoring above what is worn, must be
 * WORN rather than carried. That is a level 26 mage with a blue robe in his
 * bags and a green one on.
 *
 * Two files and nothing else, on the terms tests/test_professions.cpp set out.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::GearComparison;
using OverseerDecisions::GearCompare;
using OverseerDecisions::GearConfidence;
using OverseerDecisions::GearIncumbentPair;
using OverseerDecisions::GearIncumbentScore;
using OverseerDecisions::GearIntend;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearSlotMemory;
using OverseerDecisions::GearStat;
using OverseerDecisions::GearSwapIntent;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWearer;
using OverseerDecisions::GearWorn;

namespace
{

int failures = 0;

void Expect(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

char const* Name(GearComparison c)
{
    switch (c)
    {
        case GearComparison::Better: return "Better";
        case GearComparison::NotBetter: return "NotBetter";
        case GearComparison::Undecided: return "Undecided";
    }
    return "?";
}

void ExpectComparison(char const* what, GearComparison got, GearComparison want)
{
    if (got == want)
        return;
    std::printf("FAIL: %s - wanted %s, got %s\n", what, Name(want), Name(got));
    ++failures;
}

// The core's own ids, spelled out for the same reason tests/test_gear.cpp
// spells them out: a failure should read as a thing rather than a number.
int const CLASS_WEAPON = 2;
int const CLASS_ARMOUR = 4;
int const ARMOUR_CLOTH = 1;
int const ARMOUR_MAIL = 3;
int const INV_HANDS = 10;
int const INV_CHEST = 5;
int const MOD_AGILITY = 3;
int const MOD_STRENGTH = 4;
int const MOD_INTELLECT = 5;
int const MOD_SPIRIT = 6;
int const MOD_STAMINA = 7;

// A verdict with a chosen score and a chosen certainty, for the property tests.
// Built by hand rather than scored, because what is being asserted is the shape
// of the COMPARISON and not the arithmetic of the score - which
// tests/test_gear.cpp already owns.
GearVerdict Verdict(float score, GearConfidence confidence)
{
    GearVerdict verdict;
    verdict.wearable = true;
    verdict.score = score;
    verdict.confidence = confidence;
    verdict.judged = confidence == GearConfidence::Exact;
    return verdict;
}

GearIncumbentScore Incumbent(float score, GearConfidence confidence)
{
    return GearIncumbentScore{score, confidence};
}

// ---------------------------------------------------------------------------
// The real wearer and the two real pairs of gloves, off the live world.

GearWearer TheProtectionWarrior()
{
    GearWearer who;
    who.name = "the tank";
    who.level = 29;
    who.role = GearRole::Tank;
    // His own skill rows. No plate: a warrior learns it at 40 and he is 29.
    who.cloth = true;
    who.leather = true;
    who.mail = true;
    who.plate = false;
    who.shield = true;
    return who;
}

// Thorbia's Gauntlets: item level 23, RARE, 122 armour, 8 strength, 3 stamina.
GearItem TheRareGauntlets()
{
    GearItem item;
    item.name = "the rare gauntlets";
    item.itemClass = CLASS_ARMOUR;
    item.subClass = ARMOUR_MAIL;
    item.inventoryType = INV_HANDS;
    item.itemLevel = 23;
    item.requiredLevel = 18;
    item.quality = 3;
    item.armour = 122;
    item.stats.push_back(GearStat{MOD_STRENGTH, 8});
    item.stats.push_back(GearStat{MOD_STAMINA, 3});
    return item;
}

// Slayer's Gloves: item level 29, COMMON, the same 122 armour, 6 stamina and
// 5 spirit - and spirit is worth nothing at all to a tank.
GearItem TheCommonGloves()
{
    GearItem item;
    item.name = "the common gloves";
    item.itemClass = CLASS_ARMOUR;
    item.subClass = ARMOUR_MAIL;
    item.inventoryType = INV_HANDS;
    item.itemLevel = 29;
    item.requiredLevel = 24;
    item.quality = 2;
    item.armour = 122;
    item.stats.push_back(GearStat{MOD_STAMINA, 6});
    item.stats.push_back(GearStat{MOD_SPIRIT, 5});
    return item;
}

// ---------------------------------------------------------------------------

// SIX ITEM LEVELS AND A QUALITY TIER DO NOT DECIDE IT, THE STATS DO, and the
// answer is the same whichever of the two is already on. That second half is
// the whole point: an answer that changes with what is worn is an answer that
// oscillates.
void TheSameTwoGlovesGiveTheSameAnswerWhicheverIsOn()
{
    GearWearer const who = TheProtectionWarrior();
    GearVerdict const rare = GearScore(TheRareGauntlets(), who);
    GearVerdict const common = GearScore(TheCommonGloves(), who);

    Expect("both pairs are wearable by a warrior with mail", rare.wearable && common.wearable);
    Expect("both scores are exact - no effect, no random property",
           rare.confidence == GearConfidence::Exact &&
               common.confidence == GearConfidence::Exact);
    Expect("the rare scores higher despite six fewer item levels", rare.score > common.score);

    ExpectComparison("wearing the common, the rare is worth putting on",
                     GearCompare(rare, GearWorn(common)), GearComparison::Better);
    ExpectComparison("wearing the rare, the common is not worth putting on",
                     GearCompare(common, GearWorn(rare)), GearComparison::NotBetter);

    // AND ONCE IT IS ON IT STAYS ON. Same inputs, asked again and again: the
    // answer does not drift, so the drive that acts on it cannot either.
    for (int poll = 0; poll < 50; ++poll)
        ExpectComparison("and it is still not worth putting on, poll after poll",
                         GearCompare(common, GearWorn(rare)), GearComparison::NotBetter);
}

// A LEVEL 29 COMMON REALLY CAN BEAT A LEVEL 23 RARE, and this must keep saying
// so - the fix for a bias towards item level is not a bias towards quality.
// Same two slots, same wearer, and the only change is that the common's stats
// are ones a tank actually wants.
void TheCommonWinsWhenItsStatsAreTheOnesWanted()
{
    GearWearer const who = TheProtectionWarrior();

    GearItem better = TheCommonGloves();
    better.stats.clear();
    better.stats.push_back(GearStat{MOD_STAMINA, 12});
    better.stats.push_back(GearStat{MOD_AGILITY, 6});

    GearVerdict const rare = GearScore(TheRareGauntlets(), who);
    GearVerdict const common = GearScore(better, who);

    ExpectComparison("a common with the right stats displaces a rare",
                     GearCompare(common, GearWorn(rare)), GearComparison::Better);
    ExpectComparison("and the rare does not come back",
                     GearCompare(rare, GearWorn(common)), GearComparison::NotBetter);
}

// ANTISYMMETRY, OVER A GRID AND NOT AT A POINT. Two items can never each be
// better than the other, whatever their scores and whatever the file can vouch
// for about them. This is the property that makes a single writer converge, and
// asserting it here means a future change to the margin cannot quietly lose it.
void NoTwoItemsAreEachBetterThanTheOther()
{
    GearConfidence const kinds[] = {GearConfidence::Exact, GearConfidence::Floor,
                                    GearConfidence::Opinion};

    int pairs = 0;
    for (int a = 0; a <= 400; a += 7)
    {
        for (int b = 0; b <= 400; b += 7)
        {
            for (GearConfidence const ca : kinds)
            {
                for (GearConfidence const cb : kinds)
                {
                    float const sa = static_cast<float>(a) * 0.25f;
                    float const sb = static_cast<float>(b) * 0.25f;
                    GearComparison const forward =
                        GearCompare(Verdict(sa, ca), Incumbent(sb, cb));
                    GearComparison const backward =
                        GearCompare(Verdict(sb, cb), Incumbent(sa, ca));
                    ++pairs;
                    if (forward == GearComparison::Better &&
                        backward == GearComparison::Better)
                    {
                        std::printf("FAIL: %.2f and %.2f are each better than the other\n",
                                    static_cast<double>(sa), static_cast<double>(sb));
                        ++failures;
                        return;
                    }
                }
            }
        }
    }
    Expect("the grid was actually walked", pairs > 30000);
}

// AND AN ITEM IS NEVER BETTER THAN ITSELF, which is the degenerate case of the
// same property and the one a margin of zero would break.
void NothingIsAnUpgradeOverItself()
{
    for (int i = 0; i <= 500; i += 5)
    {
        float const score = static_cast<float>(i) * 0.5f;
        for (GearConfidence const c : {GearConfidence::Exact, GearConfidence::Floor})
            if (GearCompare(Verdict(score, c), Incumbent(score, c)) == GearComparison::Better)
            {
                std::printf("FAIL: %.2f is an upgrade over itself\n",
                            static_cast<double>(score));
                ++failures;
                return;
            }
    }
}

// A SWEEP REACHES A FIXED POINT. One slot, a bag of candidates, and the rule
// applied until nothing more moves - which must happen, and must then stay
// happened however long the drive keeps polling.
void ASweepSettlesAndStaysSettled()
{
    GearWearer const who = TheProtectionWarrior();

    std::vector<GearItem> bag;
    bag.push_back(TheCommonGloves());
    bag.push_back(TheRareGauntlets());
    {
        GearItem poor = TheRareGauntlets();
        poor.name = "worn-out gloves";
        poor.itemLevel = 12;
        poor.armour = 40;
        poor.stats.clear();
        poor.stats.push_back(GearStat{MOD_SPIRIT, 9});
        bag.push_back(poor);
    }
    {
        GearItem best = TheCommonGloves();
        best.name = "the good gloves";
        best.itemLevel = 26;
        best.armour = 150;
        best.stats.clear();
        best.stats.push_back(GearStat{MOD_STAMINA, 9});
        best.stats.push_back(GearStat{MOD_STRENGTH, 6});
        bag.push_back(best);
    }

    GearVerdict worn;  // an empty slot
    std::string on = "nothing";
    int swaps = 0;
    int passes = 0;
    bool moved = true;
    while (moved)
    {
        moved = false;
        ++passes;
        Expect("the sweep terminates rather than running forever", passes < 20);
        if (passes >= 20)
            return;
        for (GearItem const& item : bag)
        {
            GearVerdict const candidate = GearScore(item, who);
            if (GearCompare(candidate, GearWorn(worn)) != GearComparison::Better)
                continue;
            worn = candidate;
            on = item.name;
            ++swaps;
            moved = true;
        }
    }

    Expect("it settled on the best of them, not the newest or the shiniest",
           on == "the good gloves");
    Expect("and it got there in fewer swaps than there are items in the bag",
           swaps > 0 && swaps <= static_cast<int>(bag.size()));

    // THE PART THAT MATTERS: keep polling. Nothing more moves, ever.
    for (int poll = 0; poll < 200; ++poll)
        for (GearItem const& item : bag)
            Expect("nothing moves once it has settled",
                   GearCompare(GearScore(item, who), GearWorn(worn)) !=
                       GearComparison::Better);
}

// A FLOOR THAT ALREADY CLEARS THE MARGIN IS ENOUGH (#221). The measured case: a
// level 26 mage wearing a common chest worth 21.8 while carrying a RARE robe
// worth at least 25.8, whose remaining worth is in an on-equip spell this file
// does not price. The unread part can only add, so the answer is already known
// and the robe goes on.
void ARareWhoseWorthIsPartlyUnreadStillGetsWorn()
{
    GearWearer who;
    who.name = "the mage";
    who.level = 26;
    who.role = GearRole::Caster;
    who.cloth = true;

    GearItem common;
    common.name = "the padded armour";
    common.itemClass = CLASS_ARMOUR;
    common.subClass = ARMOUR_CLOTH;
    common.inventoryType = INV_CHEST;
    common.itemLevel = 21;
    common.requiredLevel = 16;
    common.quality = 2;
    common.armour = 35;
    common.stats.push_back(GearStat{MOD_INTELLECT, 6});
    common.stats.push_back(GearStat{MOD_SPIRIT, 4});

    GearItem rare;
    rare.name = "the robe";
    rare.itemClass = CLASS_ARMOUR;
    rare.subClass = ARMOUR_CLOTH;
    rare.inventoryType = INV_CHEST;
    rare.itemLevel = 22;
    rare.requiredLevel = 17;
    rare.quality = 3;
    rare.armour = 40;
    rare.stats.push_back(GearStat{MOD_INTELLECT, 7});
    rare.stats.push_back(GearStat{MOD_STAMINA, 5});
    rare.hasEffect = true;  // an on-equip spell, unread

    GearVerdict const wornVerdict = GearScore(common, who);
    GearVerdict const candidate = GearScore(rare, who);

    Expect("what is worn is exactly known", wornVerdict.confidence == GearConfidence::Exact);
    Expect("the robe's score is a floor, not an opinion",
           candidate.confidence == GearConfidence::Floor);
    Expect("this is still the old boolean's `not judged`", !candidate.judged);
    Expect("and the floor is already the higher number",
           candidate.score > wornVerdict.score);

    ExpectComparison("a floor that clears the margin is enough to wear it",
                     GearCompare(candidate, GearWorn(wornVerdict)), GearComparison::Better);

    // A FLOOR THAT DOES NOT CLEAR PROVES NOTHING EITHER WAY, and must not be
    // reported as a refusal - the unread part might well close the gap.
    ExpectComparison("but a floor below the line is undecided, not refused",
                     GearCompare(Verdict(10.f, GearConfidence::Floor),
                                 Incumbent(40.f, GearConfidence::Exact)),
                     GearComparison::Undecided);

    // AND WHEN WHAT IS WORN IS ITSELF ONLY A FLOOR, nothing above it can be
    // proved, however certain the candidate is.
    ExpectComparison("an exact candidate cannot beat an unknown quantity",
                     GearCompare(Verdict(400.f, GearConfidence::Exact),
                                 Incumbent(40.f, GearConfidence::Floor)),
                     GearComparison::Undecided);

    // An unknown role is not a floor and never was. It stays a refusal to act.
    ExpectComparison("an opinion still drives nothing",
                     GearCompare(Verdict(400.f, GearConfidence::Opinion),
                                 Incumbent(1.f, GearConfidence::Exact)),
                     GearComparison::Undecided);
}

// AN EMPTY SLOT IS EXACTLY ZERO, so the first item into one is an upgrade by
// construction - including one whose score is only a floor, which is a shield
// with an unresolved random property against a bare off hand.
void AnEmptySlotIsExactlyZeroAndNotAnUnknown()
{
    GearVerdict const nothing;  // wearable is false: an empty slot
    GearIncumbentScore const empty = GearWorn(nothing);
    Expect("an empty slot scores zero", empty.score == 0.f);
    Expect("and that zero is certain", empty.confidence == GearConfidence::Exact);

    ExpectComparison("so a floor goes into an empty slot",
                     GearCompare(Verdict(62.f, GearConfidence::Floor), empty),
                     GearComparison::Better);

    // Something the character can no longer wear is worth exactly zero too, and
    // that is certain rather than unknown - otherwise losing a proficiency
    // would freeze the slot forever.
    GearVerdict refused;
    refused.wearable = false;
    refused.judged = true;
    refused.confidence = GearConfidence::Exact;
    Expect("and so is a piece the character cannot wear any more",
           GearWorn(refused).score == 0.f &&
               GearWorn(refused).confidence == GearConfidence::Exact);
}

// A TWO-HANDER IS MEASURED AGAINST BOTH HANDS, and the pair is only exactly
// known when both halves are (#14, the Severing Axe).
void APairIsOnlyAsCertainAsItsWeakerHalf()
{
    GearIncumbentScore const exactMain = Incumbent(70.f, GearConfidence::Exact);
    GearIncumbentScore const exactOff = Incumbent(445.f, GearConfidence::Exact);
    GearIncumbentScore const floorOff = Incumbent(445.f, GearConfidence::Floor);
    GearIncumbentScore const opinionOff = Incumbent(445.f, GearConfidence::Opinion);

    Expect("the scores add", GearIncumbentPair(exactMain, exactOff).score == 515.f);
    Expect("two exact halves make an exact pair",
           GearIncumbentPair(exactMain, exactOff).confidence == GearConfidence::Exact);
    Expect("one floor makes the pair a floor",
           GearIncumbentPair(exactMain, floorOff).confidence == GearConfidence::Floor);
    Expect("and one opinion makes the pair an opinion",
           GearIncumbentPair(exactMain, opinionOff).confidence == GearConfidence::Opinion);

    // The Severing Axe itself: a green two-hander does not disarm a tank
    // holding a shield worth 445 armour, however good the axe is on its own.
    ExpectComparison("a two-hander has to beat the pair, not the main hand",
                     GearCompare(Verdict(300.f, GearConfidence::Exact),
                                 GearIncumbentPair(exactMain, exactOff)),
                     GearComparison::NotBetter);
}

// ---------------------------------------------------------------------------
// THE DRIVE STANDS DOWN.

unsigned const RARE_GAUNTLETS = 12994;
unsigned const COMMON_GLOVES = 14754;

// The ordinary case: a swap it has not made before happens, and what it did is
// remembered so the next pass can tell whether it stuck.
void AFreshSwapHappensAndIsRemembered()
{
    GearSlotMemory const nothingKnown;
    GearSwapIntent const intent =
        GearIntend(nothingKnown, RARE_GAUNTLETS, COMMON_GLOVES, true);

    Expect("a swap it has never tried happens", intent.swap);
    Expect("and it does not stand down on the first try", !intent.standDown);
    Expect("it remembers what it put on", intent.memory.chosen == RARE_GAUNTLETS);
    Expect("and what it took off", intent.memory.displaced == COMMON_GLOVES);
    Expect("with a clean budget", intent.memory.reversals == 0);

    // A comparison that said no is not this function's business and moves
    // nothing at all, including the memory.
    GearSwapIntent const declined =
        GearIntend(intent.memory, COMMON_GLOVES, RARE_GAUNTLETS, false);
    Expect("nothing happens when the comparison said no", !declined.swap);
    Expect("and nothing is said", !declined.standDown);
    Expect("and nothing is forgotten", declined.memory.chosen == RARE_GAUNTLETS &&
                                           declined.memory.reversals == 0);
}

// THE MEASURED LOOP, RUN FORWARD. Something else keeps putting the common
// gloves back on. The drive tries, is overruled, tries again - and gives up,
// out loud, after a bounded number of attempts. 124 equips in eleven hours
// becomes two equips and one line.
void SomethingElseKeepsUndoingItAndTheDriveGivesUp()
{
    GearSlotMemory memory;
    int swaps = 0;
    int standDowns = 0;

    for (int poll = 0; poll < 500; ++poll)
    {
        // Every poll looks the same, because the other writer undoes the swap
        // between polls: the rare is in the bags and the common is worn.
        GearSwapIntent const intent =
            GearIntend(memory, RARE_GAUNTLETS, COMMON_GLOVES, true);
        memory = intent.memory;
        if (intent.swap)
            ++swaps;
        if (intent.standDown)
            ++standDowns;
    }

    Expect("it stops swapping", swaps <= OverseerDecisions::GEAR_REVERSALS_ALLOWED);
    Expect("it does swap at least once first - a transient is not a war", swaps >= 1);
    Expect("it says so exactly once, not every five seconds forever", standDowns == 1);
    Expect("and the budget does not keep climbing",
           memory.reversals == OverseerDecisions::GEAR_REVERSALS_ALLOWED);
}

// A DIFFERENT PAIR IS A FRESH QUESTION. Standing down on one argument must not
// freeze the slot: a third pair of gloves, or the same pair over something new,
// gets the full budget again.
void GivingUpOnOnePairDoesNotFreezeTheSlot()
{
    GearSlotMemory memory;
    for (int poll = 0; poll < 20; ++poll)
        memory = GearIntend(memory, RARE_GAUNTLETS, COMMON_GLOVES, true).memory;
    Expect("it has given up on that pair",
           !GearIntend(memory, RARE_GAUNTLETS, COMMON_GLOVES, true).swap);

    unsigned const SOMETHING_BETTER = 12345;
    GearSwapIntent const fresh = GearIntend(memory, SOMETHING_BETTER, COMMON_GLOVES, true);
    Expect("a different candidate is a new question", fresh.swap);
    Expect("with a clean budget", fresh.memory.reversals == 0);
    Expect("and a new memory", fresh.memory.chosen == SOMETHING_BETTER);

    GearSwapIntent const overSomethingElse =
        GearIntend(memory, RARE_GAUNTLETS, SOMETHING_BETTER, true);
    Expect("and so is the same candidate over something else",
           overSomethingElse.swap && overSomethingElse.memory.reversals == 0);
}

// UNDER ONE WRITER THE GUARD NEVER FIRES, which is the other half of it being
// safe to have: after this drive's own swap, the candidate IS what is worn, so
// the same pair never comes round again and the budget is never spent.
void WithNobodyFightingBackTheGuardIsInert()
{
    GearSlotMemory memory;
    unsigned worn = COMMON_GLOVES;
    unsigned carried = RARE_GAUNTLETS;
    int swaps = 0;

    for (int poll = 0; poll < 200; ++poll)
    {
        // Once the rare is on, the comparison stops saying yes - which is what
        // TheSameTwoGlovesGiveTheSameAnswerWhicheverIsOn asserts about the real
        // scores. Modelled here as: only swap while the rare is still carried.
        bool const wanted = worn != RARE_GAUNTLETS;
        GearSwapIntent const intent = GearIntend(memory, carried, worn, wanted);
        memory = intent.memory;
        Expect("nothing to stand down about when nobody is fighting back",
               !intent.standDown);
        if (intent.swap)
        {
            unsigned const displaced = worn;
            worn = carried;
            carried = displaced;
            ++swaps;
        }
    }

    Expect("it swapped exactly once and then stopped", swaps == 1);
    Expect("and the rare is what is on", worn == RARE_GAUNTLETS);
    Expect("with the budget untouched", memory.reversals == 0);
}

// The drive may not stand down over an empty slot. Entry zero is "nothing was
// there", and a slot that keeps ending up empty is not an argument about an
// item - it is a slot to keep filling.
void AnEmptySlotIsNeverSomethingToGiveUpOn()
{
    GearSlotMemory memory;
    int swaps = 0;
    for (int poll = 0; poll < 50; ++poll)
    {
        GearSwapIntent const intent = GearIntend(memory, RARE_GAUNTLETS, 0, true);
        memory = intent.memory;
        Expect("filling an empty slot is never a stand-down", !intent.standDown);
        if (intent.swap)
            ++swaps;
    }
    Expect("and it keeps trying to fill it", swaps > OverseerDecisions::GEAR_REVERSALS_ALLOWED);
}

}  // namespace

int main()
{
    TheSameTwoGlovesGiveTheSameAnswerWhicheverIsOn();
    TheCommonWinsWhenItsStatsAreTheOnesWanted();
    NoTwoItemsAreEachBetterThanTheOther();
    NothingIsAnUpgradeOverItself();
    ASweepSettlesAndStaysSettled();
    ARareWhoseWorthIsPartlyUnreadStillGetsWorn();
    AnEmptySlotIsExactlyZeroAndNotAnUnknown();
    APairIsOnlyAsCertainAsItsWeakerHalf();
    AFreshSwapHappensAndIsRemembered();
    SomethingElseKeepsUndoingItAndTheDriveGivesUp();
    GivingUpOnOnePairDoesNotFreezeTheSlot();
    WithNobodyFightingBackTheGuardIsInert();
    AnEmptySlotIsNeverSomethingToGiveUpOn();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the gear choice converges, and the drive knows when it is overruled\n");
    return EXIT_SUCCESS;
}
