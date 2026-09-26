/*
 * What goes in a tank's off hand, decided without a world.
 *
 * THE MEASUREMENT THIS FILE EXISTS FOR (dev realm, 2026-09-26). The operator
 * lowered the Alliance family to its natural levels. The head, a Protection
 * warrior and the family's main tank, went from 60 to 38, and every shield he
 * owned needed level 39 to 58. The family handed him a Sorcerer Sphere and the
 * equip pass put it on:
 *
 *     9882  Sorcerer Sphere   class 4 (armour)  subclass 0 (misc)
 *           InventoryType 23 (held in off hand)  ItemLevel 43  RequiredLevel 38
 *           Armor 0, no template stats, a rolled caster property
 *     9843  Banded Shield     class 4  subclass 6 (shield)  InventoryType 14
 *           ItemLevel 33  RequiredLevel 28  Armor 628  Block 13
 *     14777 Ravager's Shield  class 4  subclass 6  InventoryType 14
 *           ItemLevel 44  RequiredLevel 39  Armor 1380  Block 20
 *
 * He died four times in thirteen minutes. The orb scored for him because the
 * item-level tiebreak alone (half a point a level) beats an empty off hand,
 * and nothing asked what a warrior tank's off hand is FOR.
 *
 * WHAT IT PINS:
 *
 *   1. A HELD-IN-OFF-HAND IS NEVER A SHIELD TANK'S, at any item level. It is
 *      refused with a code and a sentence, so it has no score to compare.
 *   2. NOR IS IT A MELEE CHARACTER'S. The Retribution paladin is refused too.
 *   3. IT IS STILL A CASTER'S, and a feral tank's, whose only off hand it is.
 *   4. AN OFF-HAND WEAPON IS NOT A SHIELD TANK'S EITHER, and stays a dual
 *      wielder's.
 *   5. ANY SHIELD BEATS A WORN ORB for a shield tank, even one ten item levels
 *      lower, because a refused incumbent is worth exactly nothing.
 *
 * It compiles the pure file and this one and NOTHING ELSE, on the terms
 * tests/test_gear.cpp already set out.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::GearCompare;
using OverseerDecisions::GearComparison;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearRefusal;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearUsability;
using OverseerDecisions::GearUsable;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWearer;
using OverseerDecisions::GearWorn;

namespace
{

int const CLASS_WEAPON = 2;
int const CLASS_ARMOUR = 4;
int const SUB_MISC = 0;
int const SUB_SHIELD = 6;
int const SUB_DAGGER = 15;
int const INV_SHIELD = 14;
int const INV_WEAPON_OFF_HAND = 22;
int const INV_HOLDABLE = 23;

int failures = 0;

void Check(bool ok, char const* what, std::string const& detail = "")
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
    }
}

void ExpectNotForRole(char const* what, GearUsability const& got, char const* whyContains)
{
    Check(!got.usable, what, "expected a refusal, got usable");
    Check(got.refusal == GearRefusal::NotForRole, what, "expected NotForRole, got why='" + got.why + "'");
    Check(got.why.find(whyContains) != std::string::npos, what,
          std::string("expected the reason to mention '") + whyContains + "', got '" + got.why + "'");
}

// The Alliance head as the drive reads him: level 38, the roster's
// Protection tree, and the skills a warrior holds at 38 (no plate until 40).
GearWearer Grug()
{
    GearWearer who;
    who.name = "Grug";
    who.level = 38;
    who.role = GearRole::Tank;
    who.cloth = who.leather = who.mail = who.shield = true;
    who.canDualWield = true;
    return who;
}

GearItem SorcererSphere()
{
    GearItem item;
    item.name = "Sorcerer Sphere";
    item.itemClass = CLASS_ARMOUR;
    item.subClass = SUB_MISC;
    item.inventoryType = INV_HOLDABLE;
    item.itemLevel = 43;
    item.requiredLevel = 38;
    item.quality = 2;
    return item;
}

GearItem Shield(char const* name, int itemLevel, int requiredLevel, int armour)
{
    GearItem item;
    item.name = name;
    item.itemClass = CLASS_ARMOUR;
    item.subClass = SUB_SHIELD;
    item.inventoryType = INV_SHIELD;
    item.itemLevel = itemLevel;
    item.requiredLevel = requiredLevel;
    item.armour = armour;
    item.quality = 2;
    return item;
}

GearItem OffHandBlade()
{
    GearItem item;
    item.name = "an off-hand blade";
    item.itemClass = CLASS_WEAPON;
    item.subClass = SUB_DAGGER;
    item.inventoryType = INV_WEAPON_OFF_HAND;
    item.itemLevel = 40;
    item.requiredLevel = 35;
    item.dps = 20.f;
    return item;
}

void TheOrbIsNeverTheShieldTanks()
{
    ExpectNotForRole("the Sorcerer Sphere is not a Protection warrior's",
                     GearUsable(SorcererSphere(), Grug()), "a tank holds a shield there");

    GearVerdict const verdict = GearScore(SorcererSphere(), Grug());
    Check(!verdict.wearable, "so it is not wearable for him");
    Check(verdict.score == 0.f, "and carries no score to compare");
    Check(verdict.refusal == GearRefusal::NotForRole, "and says which gate answered");

    // At any item level: the tiebreak is what made it win, so a better orb
    // must not win either.
    GearItem better = SorcererSphere();
    better.itemLevel = 60;
    better.requiredLevel = 30;
    Check(!GearScore(better, Grug()).wearable, "a higher-level orb is refused the same way");
}

void NorIsItAMeleeCharacters()
{
    GearWearer grog;
    grog.name = "Grog";
    grog.level = 38;
    grog.role = GearRole::Melee;  // Retribution
    grog.cloth = grog.leather = grog.mail = grog.shield = true;
    ExpectNotForRole("a Retribution paladin does not hold an orb",
                     GearUsable(SorcererSphere(), grog), "no use in melee");
}

void ItIsStillACastersAndAFeralTanks()
{
    GearWearer og;
    og.name = "Og";
    og.level = 38;
    og.role = GearRole::Caster;
    og.cloth = true;
    og.canDualWield = false;
    Check(GearUsable(SorcererSphere(), og).usable, "a mage may hold the orb");
    Check(GearScore(SorcererSphere(), og).wearable, "and it scores for her");

    GearWearer ugga = og;
    ugga.name = "Ugga";
    ugga.role = GearRole::Healer;
    Check(GearUsable(SorcererSphere(), ugga).usable, "so may a priest");

    // A bear tank has no shield skill, and a held piece is the only thing its
    // off hand can hold.
    GearWearer bear;
    bear.name = "a feral druid in a tank seat";
    bear.level = 38;
    bear.role = GearRole::Tank;
    bear.cloth = bear.leather = true;
    bear.shield = false;
    Check(GearUsable(SorcererSphere(), bear).usable, "a tank without a shield skill may hold it");
}

void AnOffHandWeaponIsNotTheShieldTanksEither()
{
    ExpectNotForRole("a shield tank does not fill the off hand with a weapon",
                     GearUsable(OffHandBlade(), Grug()), "a tank holds a shield there");

    GearWearer rogue;
    rogue.name = "Bork";
    rogue.level = 38;
    rogue.role = GearRole::Melee;
    rogue.cloth = rogue.leather = true;
    rogue.canDualWield = true;
    Check(GearUsable(OffHandBlade(), rogue).usable, "a dual-wielding rogue still may");
}

void AnyShieldBeatsAWornOrb()
{
    // What the drive measures a carried shield against: the orb worn now,
    // scored for the man wearing it. A refusal is worth exactly zero.
    GearVerdict const worn = GearScore(SorcererSphere(), Grug());

    GearVerdict const banded = GearScore(Shield("Banded Shield", 33, 28, 628), Grug());
    Check(banded.wearable, "the Banded Shield is his at 38");
    Check(GearCompare(banded, GearWorn(worn)) == GearComparison::Better,
          "and it beats the orb, ten item levels lower");

    GearWearer at39 = Grug();
    at39.level = 39;
    GearVerdict const ravager = GearScore(Shield("Ravager's Shield", 44, 39, 1380), at39);
    Check(GearCompare(ravager, GearWorn(GearScore(SorcererSphere(), at39))) ==
              GearComparison::Better,
          "and at 39 Ravager's Shield does too");

    // Until 39 it is refused on level alone, a reason that expires.
    Check(GearUsable(Shield("Ravager's Shield", 44, 39, 1380), Grug()).refusal ==
              GearRefusal::BelowRequiredLevel,
          "at 38 Ravager's Shield waits on level, not on role");
}

}  // namespace

int main()
{
    TheOrbIsNeverTheShieldTanks();
    NorIsItAMeleeCharacters();
    ItIsStillACastersAndAFeralTanks();
    AnOffHandWeaponIsNotTheShieldTanksEither();
    AnyShieldBeatsAWornOrb();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a tank's off hand holds a shield\n");
    return EXIT_SUCCESS;
}
