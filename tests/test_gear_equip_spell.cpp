/*
 * What an on-equip spell is worth (#221).
 *
 * WHY THIS TEST EXISTS, MEASURED. On the current build the gear sweep put nothing on
 * for either family: zero "puts on" lines in the worldserver log, against a
 * "cannot be settled from the numbers" for every better piece in the bags. The
 * father, a level 46 protection warrior, carried Valorous Helm (plate, 376
 * armour, item level 48, +14 agility, +13 stamina, scoring 442) and wore
 * Southsea Head Bucket (plate, 355 armour, item level 45, +15 stamina, +6
 * strength, and "Equip: Increases defense rating by 6"). That last line is a
 * spell on the item, not a stat line, and the adapter turned any spell into
 * `hasEffect`, so the worn helm was a Floor and GearCompare - correctly -
 * refused to prove anything above a floor. The same held for his neck, chest,
 * shoulders and legs: every rare at this level carries its rating, attack
 * power, spell power or mana regeneration as an equip aura.
 *
 * WHERE THE NUMBERS COME FROM. The items, their stats and their equip lines are
 * the live realm's, read off the bridge's armory for the father's worn set and
 * bags. The aura each equip line is written as is the core's own encoding of
 * that line: a rating is SPELL_AURA_MOD_RATING over a combat-rating mask, a
 * stat is SPELL_AURA_MOD_STAT, attack power is the melee and ranged pair, spell
 * power is magic damage done plus healing done, mana per five is
 * SPELL_AURA_MOD_POWER_REGEN on mana.
 *
 * WHAT IT PINS, in order of how much it would cost to get wrong again:
 *
 *   1. AN EQUIP AURA THAT IS A STAT BECOMES A STAT, and the item is Exact.
 *   2. IT ACTUALLY UNBLOCKS THE SLOT: the father's helm, end to end.
 *   3. THE PAIRS ARE ONE STAT, so nobody is paid twice for the same bonus.
 *   4. WHAT IS NOT A STAT STAYS A FLOOR: a proc, a use, a rolling amount, an
 *      aura this file cannot translate, a spell nobody could find. Widening
 *      the gate past these would be the confident-but-wrong move #221 paid for.
 *
 * Two files and nothing else, on the terms tests/test_professions.cpp set out.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using OverseerDecisions::GearCompare;
using OverseerDecisions::GearComparison;
using OverseerDecisions::GearConfidence;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearItemSpell;
using OverseerDecisions::GearReadItemSpells;
using OverseerDecisions::GearResolvedProperty;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearSpellEffect;
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
int const CLASS_WEAPON = 2;
int const ARMOUR_MISC = 0;
int const ARMOUR_PLATE = 4;
int const INV_HEAD = 1;
int const INV_WEAPON = 13;
int const MOD_AGILITY = 3;
int const MOD_STRENGTH = 4;
int const MOD_INTELLECT = 5;
int const MOD_SPIRIT = 6;
int const MOD_STAMINA = 7;
int const MOD_DEFENSE_RATING = 12;
int const MOD_HIT_RATING = 31;
int const MOD_ATTACK_POWER = 38;
int const MOD_RANGED_ATTACK_POWER = 39;
int const MOD_MANA_REGENERATION = 43;
int const MOD_SPELL_POWER = 45;

// ItemSpelltriggerType.
int const ON_USE = 0;
int const ON_EQUIP = 1;
int const CHANCE_ON_HIT = 2;

// SpellEffects and AuraType.
int const APPLY_AURA = 6;
int const SCHOOL_DAMAGE = 2;
int const MOD_DAMAGE_DONE = 13;
int const MOD_RESISTANCE = 22;
int const MOD_STAT = 29;
int const MOD_POWER_REGEN = 85;
int const MOD_AP = 99;
int const MOD_RANGED_AP = 124;
int const MOD_HEALING_DONE = 135;
int const MOD_RATING = 189;

// Combat-rating masks, bit n for CombatRating n.
int const RATING_DEFENSE = 1 << 1;
int const RATING_HIT_ALL = (1 << 5) | (1 << 6) | (1 << 7);
int const RATING_HIT_MELEE_AND_RANGED = (1 << 5) | (1 << 6);
int const SCHOOLS_MAGIC = 0x7E;
int const SCHOOLS_ALL = 0x7F;

GearSpellEffect Aura(int aura, int misc, int amount)
{
    GearSpellEffect effect;
    effect.effect = APPLY_AURA;
    effect.aura = aura;
    effect.misc = misc;
    effect.amount = amount;
    return effect;
}

GearItemSpell Spell(int trigger, std::vector<GearSpellEffect> effects)
{
    GearItemSpell spell;
    spell.trigger = trigger;
    spell.effects = std::move(effects);
    return spell;
}

bool Carries(GearResolvedProperty const& resolved, int type, int value)
{
    for (GearStat const& stat : resolved.stats)
        if (stat.type == type && stat.value == value)
            return true;
    return false;
}

// ------------------------------------------------------------------------

// Southsea Head Bucket: "Equip: Increases defense rating by 6."
void ARatingAuraIsARating()
{
    GearResolvedProperty const read =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_RATING, RATING_DEFENSE, 6)})});
    Expect("the helm's defense rating is read as defense rating",
           Carries(read, MOD_DEFENSE_RATING, 6));
    Expect("and nothing is left unread", !read.unresolved);
    Expect("and it is the only thing claimed", read.stats.size() == 1);

    GearResolvedProperty const hit =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_RATING, RATING_HIT_ALL, 10)})});
    Expect("hit over all three ratings is the core's hit rating",
           Carries(hit, MOD_HIT_RATING, 10) && !hit.unresolved);

    // A mask the core has no stat line for is not guessed at.
    GearResolvedProperty const odd = GearReadItemSpells(
        {Spell(ON_EQUIP, {Aura(MOD_RATING, RATING_HIT_MELEE_AND_RANGED, 10)})});
    Expect("a rating mask that is no stat line is left unread",
           odd.unresolved && odd.stats.empty());
}

// Rockshard Pauldrons and Mark of Kern: "Increases attack power by N", the
// melee and ranged pair the core's own ITEM_MOD_ATTACK_POWER applies.
void AttackPowerIsOneStatNotTwo()
{
    GearResolvedProperty const pair = GearReadItemSpells(
        {Spell(ON_EQUIP, {Aura(MOD_AP, 0, 28), Aura(MOD_RANGED_AP, 0, 28)})});
    Expect("the pair is one attack power stat", Carries(pair, MOD_ATTACK_POWER, 28));
    Expect("and only one", pair.stats.size() == 1);
    Expect("and whole", !pair.unresolved);

    GearResolvedProperty const ranged =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_RANGED_AP, 0, 20)})});
    Expect("ranged alone is ranged attack power",
           Carries(ranged, MOD_RANGED_ATTACK_POWER, 20) && !ranged.unresolved);

    GearResolvedProperty const melee =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_AP, 0, 20)})});
    Expect("melee alone is no stat the core has, so it is a floor",
           melee.unresolved && melee.stats.empty());
}

// Lifeblood Amulet: "Increases spell power by 6", magic damage plus healing.
void SpellPowerIsDamageAndHealingTogether()
{
    GearResolvedProperty const both = GearReadItemSpells(
        {Spell(ON_EQUIP, {Aura(MOD_DAMAGE_DONE, SCHOOLS_MAGIC, 6),
                          Aura(MOD_HEALING_DONE, SCHOOLS_ALL, 6)})});
    Expect("the pair is spell power", Carries(both, MOD_SPELL_POWER, 6));
    Expect("and only that", both.stats.size() == 1 && !both.unresolved);

    GearResolvedProperty const healing =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_HEALING_DONE, SCHOOLS_ALL, 12)})});
    Expect("healing alone is not spell power", healing.unresolved && healing.stats.empty());

    GearResolvedProperty const fire =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_DAMAGE_DONE, 1 << 2, 7),
                                              Aura(MOD_HEALING_DONE, SCHOOLS_ALL, 7)})});
    Expect("one school's damage is not spell power", fire.unresolved && fire.stats.empty());
}

// Gahz'rilla Scale Armor, "Restores 11 mana per 5 sec", and a plain stat aura.
void ManaRegenerationAndPrimaryStats()
{
    GearResolvedProperty const mana =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_POWER_REGEN, 0, 11)})});
    Expect("mana per five is mana regeneration",
           Carries(mana, MOD_MANA_REGENERATION, 11) && !mana.unresolved);

    GearResolvedProperty const rage =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_POWER_REGEN, 1, 2)})});
    Expect("regeneration of anything but mana is not priced", rage.unresolved);

    GearResolvedProperty const stamina =
        GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_STAT, 2, 5)})});
    Expect("a stamina aura is stamina", Carries(stamina, MOD_STAMINA, 5) && !stamina.unresolved);

    GearResolvedProperty const all = GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_STAT, -1, 3)})});
    Expect("every stat is all five",
           Carries(all, MOD_STRENGTH, 3) && Carries(all, MOD_AGILITY, 3) &&
               Carries(all, MOD_STAMINA, 3) && Carries(all, MOD_INTELLECT, 3) &&
               Carries(all, MOD_SPIRIT, 3) && !all.unresolved);
}

// THE HALF THAT MUST NOT BE WIDENED. Gutwrencher's "Chance on hit: Wounds the
// target", a "Use:", a rolling amount, a resistance, a spell nobody could find
// and an effect that is not an aura are all worth something this file does not
// price, so each stays a floor.
void WhatIsNotAStatStaysAFloor()
{
    Expect("a chance on hit is unread",
           GearReadItemSpells({Spell(CHANCE_ON_HIT, {Aura(MOD_RATING, RATING_DEFENSE, 6)})})
               .unresolved);
    Expect("a use is unread",
           GearReadItemSpells({Spell(ON_USE, {Aura(MOD_STAT, 2, 5)})}).unresolved);

    GearSpellEffect rolling = Aura(MOD_STAT, 2, 5);
    rolling.fixedAmount = false;
    Expect("an amount that rolls is unread",
           GearReadItemSpells({Spell(ON_EQUIP, {rolling})}).unresolved);

    Expect("a resistance aura is unread",
           GearReadItemSpells({Spell(ON_EQUIP, {Aura(MOD_RESISTANCE, 1, 50)})}).unresolved);

    GearItemSpell missing = Spell(ON_EQUIP, {});
    missing.found = false;
    Expect("a spell nobody could find is unread", GearReadItemSpells({missing}).unresolved);

    GearSpellEffect damage;
    damage.effect = SCHOOL_DAMAGE;
    damage.amount = 80;
    Expect("an effect that is not an aura is unread",
           GearReadItemSpells({Spell(ON_EQUIP, {damage})}).unresolved);

    // The readable half is priced anyway: a higher floor settles more.
    GearResolvedProperty const half = GearReadItemSpells(
        {Spell(ON_EQUIP, {Aura(MOD_RATING, RATING_DEFENSE, 6)}), Spell(CHANCE_ON_HIT, {})});
    Expect("the defense is still counted beside a proc", Carries(half, MOD_DEFENSE_RATING, 6));
    Expect("and the proc still makes it a floor", half.unresolved);

    // An item with no spell has nothing unread.
    Expect("no spells is not unresolved", !GearReadItemSpells({}).unresolved);
}

// ------------------------------------------------------------------------
//
// THE FATHER'S HEAD SLOT, END TO END.

GearWearer TheFather()
{
    GearWearer who;
    who.name = "the father";
    who.level = 46;
    who.role = GearRole::Tank;
    who.cloth = true;
    who.leather = true;
    who.mail = true;
    who.plate = true;
    who.shield = true;
    return who;
}

GearItem Helm(char const* name, int armour, int itemLevel, std::vector<GearStat> stats)
{
    GearItem helm;
    helm.name = name;
    helm.itemClass = CLASS_ARMOUR;
    helm.subClass = ARMOUR_PLATE;
    helm.inventoryType = INV_HEAD;
    helm.itemLevel = itemLevel;
    helm.armour = armour;
    helm.stats = std::move(stats);
    return helm;
}

GearItem WithSpells(GearItem item, std::vector<GearItemSpell> const& spells)
{
    GearResolvedProperty const read = GearReadItemSpells(spells);
    for (GearStat const& stat : read.stats)
        item.stats.push_back(stat);
    item.hasEffect = read.unresolved;
    return item;
}

void TheHelmNobodyCouldSettleIsSettled()
{
    GearItem const bucket =
        Helm("Southsea Head Bucket", 355, 45, {{MOD_STAMINA, 15}, {MOD_STRENGTH, 6}});
    GearItem const valorous =
        Helm("Valorous Helm", 376, 48, {{MOD_AGILITY, 14}, {MOD_STAMINA, 13}});
    std::vector<GearItemSpell> const bucketSpells = {
        Spell(ON_EQUIP, {Aura(MOD_RATING, RATING_DEFENSE, 6)})};

    // What it was: any spell at all made the bucket an effect.
    GearItem bucketBefore = bucket;
    bucketBefore.hasEffect = true;
    GearVerdict const wornBefore = GearScore(bucketBefore, TheFather());
    GearVerdict const candidate = GearScore(valorous, TheFather());
    Expect("before: the bucket is only a floor", wornBefore.confidence == GearConfidence::Floor);
    Expect("before: so the better helm cannot be put on",
           GearCompare(candidate, GearWorn(wornBefore)) == GearComparison::Undecided);

    // What it is now.
    GearVerdict const wornAfter = GearScore(WithSpells(bucket, bucketSpells), TheFather());
    Expect("after: the bucket is exactly known", wornAfter.confidence == GearConfidence::Exact);
    Expect("after: and judged, which the Need vote reads", wornAfter.judged);
    Expect("after: the defense rating is priced in", wornAfter.score > wornBefore.score);
    Expect("after: the better helm goes on",
           GearCompare(candidate, GearWorn(wornAfter)) == GearComparison::Better);

    // ONE-WAY, which is what makes the sweep converge: once the plate helm is
    // on, the bucket in the bags is the lower score and stays there.
    Expect("and the bucket does not go straight back on",
           GearCompare(wornAfter, GearWorn(candidate)) == GearComparison::NotBetter);
}

// A weapon whose only spell is a proc stays a floor, and a ring whose only
// spell is a stat does not - the two halves side by side on real gear.
void AProcWeaponStaysAFloorAndAStatRingDoesNot()
{
    GearItem gutwrencher;
    gutwrencher.name = "Gutwrencher";
    gutwrencher.itemClass = CLASS_WEAPON;
    gutwrencher.inventoryType = INV_WEAPON;
    gutwrencher.itemLevel = 47;
    gutwrencher.dps = 30.f;
    GearVerdict const weapon =
        GearScore(WithSpells(gutwrencher, {Spell(CHANCE_ON_HIT, {})}), TheFather());
    Expect("a proc weapon is still a floor", weapon.confidence == GearConfidence::Floor);

    GearItem kern;
    kern.name = "Mark of Kern";
    kern.itemClass = CLASS_ARMOUR;
    kern.subClass = ARMOUR_MISC;
    kern.inventoryType = 11;
    kern.itemLevel = 36;
    kern.stats = {{MOD_STAMINA, 4}};
    GearVerdict const ring = GearScore(
        WithSpells(kern, {Spell(ON_EQUIP, {Aura(MOD_AP, 0, 20), Aura(MOD_RANGED_AP, 0, 20)})}),
        TheFather());
    Expect("an attack power ring is exact", ring.confidence == GearConfidence::Exact);
}

}  // namespace

int main()
{
    ARatingAuraIsARating();
    AttackPowerIsOneStatNotTwo();
    SpellPowerIsDamageAndHealingTogether();
    ManaRegenerationAndPrimaryStats();
    WhatIsNotAStatStaysAFloor();
    TheHelmNobodyCouldSettleIsSettled();
    AProcWeaponStaysAFloorAndAStatRingDoesNot();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("an equip spell that is a stat is read as one\n");
    return EXIT_SUCCESS;
}
