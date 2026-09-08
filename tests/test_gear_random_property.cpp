/*
 * What a randomly enchanted item is actually worth (#340).
 *
 * WHY THIS TEST EXISTS, MEASURED. The gear sweep equipped nothing at all: zero
 * "puts on" lines in a worldserver log against fifty-three "cannot be settled"
 * and three "the server refused the swap". The cause was not the scoring. It
 * was that nineteen of the family's worn pieces carry a random property - a
 * "Scouting Tunic" whose "+10 agility" is rolled onto the individual item and
 * appears nowhere in its template - and NOTHING TRIED TO RESOLVE ONE. The
 * adapter set `unresolvedRandomProperty` from the mere presence of a property
 * id, so every one of those slots was scored as its armour and its item level
 * and nothing else, the worn side of every comparison was a Floor, and
 * GearCompare correctly refused to prove anything above a floor. Twenty-three
 * of thirty-two contested slots were permanently undecided, and five of them
 * held something measurably worse than what was sitting in the bags.
 *
 * WHERE THE NUMBERS IN THIS FILE COME FROM. Every effect below was read out of
 * the live realm: the item's own enchantment slots out of `item_instance`,
 * decoded against the game's own SpellItemEnchantment data. Nothing here is
 * invented, for the reason tests/test_gear.cpp gives at length - a test written
 * from imagined numbers would have passed on the day the family was wrong.
 *
 * WHAT IT PINS, in order of how much it would cost to get wrong again:
 *
 *   1. A STAT EFFECT BECOMES A STAT. The whole defect, and the whole fix.
 *   2. A PROPERTY THAT IS NOT ALL STATS IS STILL NOT ALL READ. An on-equip
 *      spell or a resistance rolled onto an item is real worth this file does
 *      not price, so the answer stays a floor - widening the gate instead
 *      would be the confident-but-wrong move #221 already paid for once.
 *   3. THE READABLE HALF IS PRICED ANYWAY. A floor with the stats folded in is
 *      a HIGHER floor, and a higher floor settles more comparisons. It must
 *      never come back empty just because one effect was unreadable.
 *   4. IT ACTUALLY UNBLOCKS THE SLOT. The real chest a real character was
 *      wearing, against the real chest in the bags, both resolved: the swap
 *      that could not be settled for a whole day becomes Better.
 *
 * Two files and nothing else, on the terms tests/test_professions.cpp set out.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::GearCompare;
using OverseerDecisions::GearComparison;
using OverseerDecisions::GearConfidence;
using OverseerDecisions::GearEnchantEffect;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearReadRandomProperty;
using OverseerDecisions::GearResolvedProperty;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearStat;
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

// The core's own ids, spelled out so a failure reads as a thing rather than a
// number - the same courtesy tests/test_gear.cpp extends.
int const CLASS_ARMOUR = 4;
int const ARMOUR_LEATHER = 2;
int const ARMOUR_MAIL = 3;
int const INV_CHEST = 5;
int const MOD_AGILITY = 3;
int const MOD_STRENGTH = 4;
int const MOD_SPIRIT = 6;
int const MOD_STAMINA = 7;

// ItemEnchantmentType, from the core's own DBCEnums.h.
int const TYPE_EQUIP_SPELL = 3;
int const TYPE_RESISTANCE = 4;
int const TYPE_STAT = 5;

GearEnchantEffect Effect(int type, int amount, int stat)
{
    GearEnchantEffect effect;
    effect.type = type;
    effect.amount = amount;
    effect.stat = stat;
    return effect;
}

bool Carries(GearResolvedProperty const& resolved, int type, int value)
{
    for (GearStat const& stat : resolved.stats)
        if (stat.type == type && stat.value == value)
            return true;
    return false;
}

// ------------------------------------------------------------------------

// Scouting Tunic, worn by the family's rogue: one enchantment, one effect,
// +10 agility, and a template that carries no stats at all.
void AStatRolledOntoAnItemIsAStat()
{
    GearResolvedProperty const resolved =
        GearReadRandomProperty({Effect(TYPE_STAT, 10, MOD_AGILITY)}, true);

    Expect("the agility a Scouting Tunic actually has is read",
           Carries(resolved, MOD_AGILITY, 10));
    Expect("and having read all of it, nothing is left unresolved",
           !resolved.unresolved);
    Expect("and it is the only thing claimed", resolved.stats.size() == 1);
}

// Robust Helm, worn by the family's paladin: two enchantments, two stats.
void EveryEffectOnEveryEnchantmentIsRead()
{
    GearResolvedProperty const resolved = GearReadRandomProperty(
        {Effect(TYPE_STAT, 8, MOD_AGILITY), Effect(TYPE_STAT, 8, MOD_STAMINA)}, true);

    Expect("both halves of a Robust Helm are read",
           Carries(resolved, MOD_AGILITY, 8) && Carries(resolved, MOD_STAMINA, 8));
    Expect("and the answer is whole", !resolved.unresolved);
}

// Ivycloth Cloak and Buccaneer's Vest, both carried by the family: their whole
// property is an on-equip spell, and this file prices no spell.
void AnOnEquipSpellRolledOntoAnItemIsStillNotRead()
{
    GearResolvedProperty const resolved =
        GearReadRandomProperty({Effect(TYPE_EQUIP_SPELL, 0, 13596)}, true);

    Expect("a spell is not turned into a stat", resolved.stats.empty());
    Expect("and the score says so rather than claiming to be whole",
           resolved.unresolved);
}

// Jacinth Circle, carried by the family's warrior: +8 fire resistance, which is
// a real thing the score does not weigh.
void AResistanceIsWorthSomethingThisFileCannotSay()
{
    GearResolvedProperty const resolved =
        GearReadRandomProperty({Effect(TYPE_RESISTANCE, 8, 2)}, true);

    Expect("a resistance is not filed as a stat", resolved.stats.empty());
    Expect("and the answer is a floor", resolved.unresolved);
}

// THE HIGHER FLOOR. A property that is part stat and part spell gives up the
// spell and keeps the stat, because a floor that has read more of the item is
// a floor that settles more comparisons.
void TheHalfThatCanBeReadIsPricedAnyway()
{
    GearResolvedProperty const resolved = GearReadRandomProperty(
        {Effect(TYPE_STAT, 5, MOD_SPIRIT), Effect(TYPE_EQUIP_SPELL, 0, 7701)}, true);

    Expect("the spirit is still counted", Carries(resolved, MOD_SPIRIT, 5));
    Expect("and the spell still makes the whole thing a floor", resolved.unresolved);
}

// A SUFFIX CARRIES ITS SIZE ON THE ITEM. The core keeps a random suffix's
// magnitude in a factor on the item rather than in the enchantment, so a stat
// effect with no amount means the caller never worked one out. Inventing one
// would be worse than saying so.
void AStatWithNoSizeIsNotAStat()
{
    GearResolvedProperty const resolved =
        GearReadRandomProperty({Effect(TYPE_STAT, 0, MOD_STAMINA)}, true);

    Expect("nothing is invented for it", resolved.stats.empty());
    Expect("and it is reported unread", resolved.unresolved);
}

// AN ENCHANTMENT NOBODY COULD LOOK UP, and an item that claims a property and
// shows nothing at all. Both are failures to read, not proofs of worthlessness.
void WhatCouldNotBeReadIsSaidRatherThanAssumedAway()
{
    GearResolvedProperty const missing =
        GearReadRandomProperty({Effect(TYPE_STAT, 10, MOD_AGILITY)}, false);
    Expect("an unreadable enchantment leaves the item a floor", missing.unresolved);
    Expect("and what WAS read is still kept", Carries(missing, MOD_AGILITY, 10));

    GearResolvedProperty const nothing = GearReadRandomProperty({}, true);
    Expect("a property with no effect at all is unread, not empty", nothing.unresolved);
    Expect("and claims no stats", nothing.stats.empty());
}

// ------------------------------------------------------------------------
//
// THE SLOT THAT COULD NOT BE SETTLED, END TO END. The family's paladin, a level
// 30 retribution melee, wearing Watcher's Jerkin - a stat-less leather chest of
// 43 armour and item level 30 - with a random property worth +6 spirit and +6
// stamina. In the bags, Grunt's Chestpiece: mail, 186 armour, item level 26,
// with +6 stamina and +6 strength rolled onto it. Both scored zero stats before
// this change, both were floors, and the sweep said "cannot be settled from the
// numbers" about it every five seconds for a day.

GearWearer TheOneWearingIt()
{
    GearWearer who;
    who.name = "the melee";
    who.level = 30;
    who.role = GearRole::Melee;
    who.cloth = true;
    who.leather = true;
    who.mail = true;
    return who;
}

GearItem WithProperty(GearItem item, GearResolvedProperty const& resolved)
{
    for (GearStat const& stat : resolved.stats)
        item.stats.push_back(stat);
    item.unresolvedRandomProperty = resolved.unresolved;
    return item;
}

void TheSlotNobodyCouldSettleIsSettled()
{
    GearItem worn;
    worn.name = "Watcher's Jerkin";
    worn.itemClass = CLASS_ARMOUR;
    worn.subClass = ARMOUR_LEATHER;
    worn.inventoryType = INV_CHEST;
    worn.itemLevel = 30;
    worn.armour = 43;

    GearItem candidate;
    candidate.name = "Grunt's Chestpiece";
    candidate.itemClass = CLASS_ARMOUR;
    candidate.subClass = ARMOUR_MAIL;
    candidate.inventoryType = INV_CHEST;
    candidate.itemLevel = 26;
    candidate.armour = 186;

    // What it was before: a property nobody read, on both sides.
    GearItem wornUnread = worn;
    wornUnread.unresolvedRandomProperty = true;
    GearItem candidateUnread = candidate;
    candidateUnread.unresolvedRandomProperty = true;

    GearVerdict const wornBefore = GearScore(wornUnread, TheOneWearingIt());
    GearVerdict const candidateBefore = GearScore(candidateUnread, TheOneWearingIt());
    Expect("before: what is worn is only a floor",
           wornBefore.confidence == GearConfidence::Floor);
    Expect("before: so nothing can be proved against it",
           GearCompare(candidateBefore, GearWorn(wornBefore)) == GearComparison::Undecided);

    // And what it is now, from the effects the items actually carry.
    GearItem const wornRead = WithProperty(
        worn,
        GearReadRandomProperty(
            {Effect(TYPE_STAT, 6, MOD_SPIRIT), Effect(TYPE_STAT, 6, MOD_STAMINA)}, true));
    GearItem const candidateRead = WithProperty(
        candidate,
        GearReadRandomProperty(
            {Effect(TYPE_STAT, 6, MOD_STAMINA), Effect(TYPE_STAT, 6, MOD_STRENGTH)}, true));

    GearVerdict const wornAfter = GearScore(wornRead, TheOneWearingIt());
    GearVerdict const candidateAfter = GearScore(candidateRead, TheOneWearingIt());
    Expect("after: what is worn is exactly known",
           wornAfter.confidence == GearConfidence::Exact);
    Expect("after: and the mail chest is exactly known",
           candidateAfter.confidence == GearConfidence::Exact);
    Expect("after: so the swap that nobody could settle is settled",
           GearCompare(candidateAfter, GearWorn(wornAfter)) == GearComparison::Better);

    // AND IT IS ONE-WAY, which is what makes the sweep converge: once the mail
    // chest is on, the leather one in the bags is the lower score and stays put.
    Expect("and it does not want to swap straight back",
           GearCompare(wornAfter, GearWorn(candidateAfter)) == GearComparison::NotBetter);

    // READING A STAT THE ROLE DOES NOT WANT STILL SETTLES THE SLOT. The spirit
    // on the leather is worth nothing to a melee, and reading it still matters:
    // it is the difference between a number that MIGHT be beaten and a number
    // that is known.
    Expect("the leather chest is worth more once its property is read",
           wornAfter.score > wornBefore.score);
}

// A PROPERTY THAT IS PART UNREADABLE STILL BLOCKS, which is the half that must
// not be widened.
void AFloorThatDoesNotClearIsStillUndecided()
{
    GearItem worn;
    worn.name = "a chest whose property is entirely read";
    worn.itemClass = CLASS_ARMOUR;
    worn.subClass = ARMOUR_MAIL;
    worn.inventoryType = INV_CHEST;
    worn.itemLevel = 30;
    worn.armour = 186;

    GearItem candidate = worn;
    candidate.name = "a chest whose property is an on-equip spell";
    candidate.armour = 100;
    candidate.unresolvedRandomProperty =
        GearReadRandomProperty({Effect(TYPE_EQUIP_SPELL, 0, 7701)}, true).unresolved;

    GearVerdict const wornVerdict = GearScore(worn, TheOneWearingIt());
    GearVerdict const candidateVerdict = GearScore(candidate, TheOneWearingIt());
    Expect("the unreadable one is a floor",
           candidateVerdict.confidence == GearConfidence::Floor);
    Expect("a floor that does not clear the margin proves nothing",
           GearCompare(candidateVerdict, GearWorn(wornVerdict)) ==
               GearComparison::Undecided);
}

}  // namespace

int main()
{
    AStatRolledOntoAnItemIsAStat();
    EveryEffectOnEveryEnchantmentIsRead();
    AnOnEquipSpellRolledOntoAnItemIsStillNotRead();
    AResistanceIsWorthSomethingThisFileCannotSay();
    TheHalfThatCanBeReadIsPricedAnyway();
    AStatWithNoSizeIsNotAStat();
    WhatCouldNotBeReadIsSaidRatherThanAssumedAway();
    TheSlotNobodyCouldSettleIsSettled();
    AFloorThatDoesNotClearIsStillUndecided();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a random property is read rather than shrugged at\n");
    return EXIT_SUCCESS;
}
