/*
 * A trinket whose worth is a known effect is priced for the role it fits
 * (wow-overseer#2358).
 *
 * WHAT WAS MEASURED. A live read found a level 60 warrior wearing one flat
 * trinket in BOTH trinket slots. The second copy is refused since the
 * worn-once rule (tests/test_gear_converges.cpp, ATrinketIsNotWornTwice), but
 * the trinket that should have had the slot could still not win it: a proc
 * or a "Use:" was a spell nothing priced, so the piece scored its item level
 * and whatever flat stat it carried, and a flat trinket with more countable
 * stats kept the slot for ever with the comparison left undecided.
 *
 * WHAT THIS FILE PINS.
 *
 *   1. A known effect is priced as the standing stat it averages out to, for
 *      the roles it fits and for nobody else: a melee-hit proc is worth
 *      nothing to a hunter or a healer.
 *   2. A spell in the table is READ, so the score is exact and the comparison
 *      settles; a proc that is not in the table stays the floor it was.
 *   3. THE SECOND SLOT. With one flat trinket in both slots, the second copy
 *      in the bags is refused and the role-fitting effect trinket is Better
 *      than the copy it displaces - for the role it fits, and not for a role
 *      it does not.
 *
 * The numbers are mirrored by the bridge's drive mirror in the wow-overseer
 * repository, which pins the same scores for the same fixture pieces.
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
using OverseerDecisions::GearIsSecondCopyOfTrinket;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearItemSpell;
using OverseerDecisions::GearKnownEffect;
using OverseerDecisions::GearKnownEffectFor;
using OverseerDecisions::GearKnownEffectWorth;
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

bool Near(float a, float b)
{
    float const gap = a > b ? a - b : b - a;
    return gap < 0.01f;
}

// The core's own ids, spelled out so a failure reads as a thing.
int const CLASS_ARMOUR = 4;
int const ARMOUR_MISC = 0;
int const INV_TRINKET = 12;
int const MOD_CRIT_RATING = 32;
int const MOD_ATTACK_POWER = 38;
int const MOD_SPELL_POWER = 45;
int const ON_USE = 0;
int const ON_EQUIP = 1;
int const APPLY_AURA = 6;
int const PROC_TRIGGER_SPELL = 42;
int const MOD_AP = 99;
int const MOD_RANGED_AP = 124;

// The two effects the table knows, by the spell an item names.
int const EXTRA_ATTACK_PROC = 15600;  // "Chance on melee hit to gain 1 extra attack."
int const ATTACK_POWER_USE = 25891;   // "Use: +280 melee and ranged attack power, 20 sec."
int const SOME_OTHER_PROC = 99999;

unsigned const HAND = 11815;
unsigned const BREADTH = 13965;

GearSpellEffect Aura(int aura, int misc, int amount)
{
    GearSpellEffect effect;
    effect.effect = APPLY_AURA;
    effect.aura = aura;
    effect.misc = misc;
    effect.amount = amount;
    return effect;
}

GearItemSpell Spell(int id, int trigger, std::vector<GearSpellEffect> effects)
{
    GearItemSpell spell;
    spell.id = id;
    spell.trigger = trigger;
    spell.effects = std::move(effects);
    return spell;
}

// A trinket built the way the adapter builds one: the template's facts, then
// whatever GearReadItemSpells made of its spells.
GearItem Trinket(char const* name, int itemLevel, std::vector<GearStat> stats,
                 std::vector<GearItemSpell> const& spells)
{
    GearItem item;
    item.name = name;
    item.itemClass = CLASS_ARMOUR;
    item.subClass = ARMOUR_MISC;
    item.inventoryType = INV_TRINKET;
    item.itemLevel = itemLevel;
    item.stats = std::move(stats);
    GearResolvedProperty const read = GearReadItemSpells(spells);
    for (GearStat const& stat : read.stats)
        item.stats.push_back(stat);
    item.knownEffects = read.knownEffects;
    item.unreadReasons = read.unreadReasons;
    item.hasEffect = read.unresolved;
    return item;
}

// The effect trinket: item level 58, an equip spell of 20 attack power and the
// extra-attack proc, which is an on-equip aura that triggers another spell.
GearItem EffectTrinket(int procSpell = EXTRA_ATTACK_PROC)
{
    return Trinket("Hand of Justice", 58, {},
                   {Spell(9336, ON_EQUIP, {Aura(MOD_AP, 0, 20), Aura(MOD_RANGED_AP, 0, 20)}),
                    Spell(procSpell, ON_EQUIP, {Aura(PROC_TRIGGER_SPELL, 0, 0)})});
}

// The flat trinket: item level 63 and 28 critical strike rating, nothing else.
GearItem FlatTrinket()
{
    return Trinket("Blackhand's Breadth", 63, {{MOD_CRIT_RATING, 28}}, {});
}

// The on-use trinket: item level 66 and the attack power "Use:", nothing else.
GearItem UseTrinket()
{
    return Trinket("Earthstrike", 66, {},
                   {Spell(ATTACK_POWER_USE, ON_USE,
                          {Aura(MOD_AP, 0, 280), Aura(MOD_RANGED_AP, 0, 280)})});
}

GearWearer Wearer(GearRole role)
{
    GearWearer who;
    who.role = role;
    who.level = 60;
    return who;
}

// ------------------------------------------------------------------------

void TheTableIsSmallAndSaysWhoEachEffectFits()
{
    GearKnownEffect const* const proc = GearKnownEffectFor(EXTRA_ATTACK_PROC);
    Expect("the extra-attack proc is known", proc != nullptr);
    if (proc)
        Expect("and is 28 attack power", proc->stat == MOD_ATTACK_POWER && proc->amount == 28);

    GearKnownEffect const* const use = GearKnownEffectFor(ATTACK_POWER_USE);
    Expect("the attack power use is known", use != nullptr);
    if (use)
        Expect("and is 280 for 20 sec of 120, 46",
               use->stat == MOD_ATTACK_POWER && use->amount == 46);

    Expect("a spell nobody listed is not known", GearKnownEffectFor(SOME_OTHER_PROC) == nullptr);
    Expect("and no spell at all is not known", GearKnownEffectFor(0) == nullptr);

    // WHO IT FITS. The proc fires on a melee hit: the two roles that swing.
    Expect("the proc is 28 to a melee", Near(GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Melee), 28.f));
    Expect("and 8.4 to a tank, at a tank's price for attack power",
           Near(GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Tank), 8.4f));
    Expect("nothing to a hunter, who prices attack power but never swings",
           GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Ranged) == 0.f);
    Expect("nothing to a healer", GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Healer) == 0.f);
    Expect("nothing to a caster", GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Caster) == 0.f);
    Expect("and no opinion for no role",
           GearKnownEffectWorth(EXTRA_ATTACK_PROC, GearRole::Unknown) == 0.f);

    // The use is melee and ranged attack power: the hunter has it too.
    Expect("the use is 46 to a hunter", Near(GearKnownEffectWorth(ATTACK_POWER_USE, GearRole::Ranged), 46.f));
    Expect("and 46 to a melee", Near(GearKnownEffectWorth(ATTACK_POWER_USE, GearRole::Melee), 46.f));
    Expect("and nothing to a healer", GearKnownEffectWorth(ATTACK_POWER_USE, GearRole::Healer) == 0.f);
}

void AKnownEffectIsReadAndAnUnknownOneIsStillAFloor()
{
    GearResolvedProperty const known =
        GearReadItemSpells({Spell(EXTRA_ATTACK_PROC, ON_EQUIP, {Aura(PROC_TRIGGER_SPELL, 0, 0)})});
    Expect("a known proc leaves nothing unread", !known.unresolved);
    Expect("and is carried by name, not folded into the stats every role prices",
           known.knownEffects.size() == 1 && known.knownEffects[0] == EXTRA_ATTACK_PROC &&
               known.stats.empty());

    GearResolvedProperty const use = GearReadItemSpells(
        {Spell(ATTACK_POWER_USE, ON_USE, {Aura(MOD_AP, 0, 280), Aura(MOD_RANGED_AP, 0, 280)})});
    Expect("a known use leaves nothing unread", !use.unresolved);
    Expect("and its burst is not counted as if it were always on", use.stats.empty());

    GearResolvedProperty const other =
        GearReadItemSpells({Spell(SOME_OTHER_PROC, ON_EQUIP, {Aura(PROC_TRIGGER_SPELL, 0, 0)})});
    Expect("a proc nobody listed is still unread", other.unresolved);
    Expect("and names nothing", other.knownEffects.empty());

    GearItemSpell missing = Spell(EXTRA_ATTACK_PROC, ON_EQUIP, {});
    missing.found = false;
    Expect("a listed spell the world does not have is not credited",
           GearReadItemSpells({missing}).unresolved &&
               GearReadItemSpells({missing}).knownEffects.empty());

    GearVerdict const priced = GearScore(EffectTrinket(), Wearer(GearRole::Melee));
    Expect("the priced trinket is exactly known",
           priced.judged && priced.confidence == GearConfidence::Exact);
    GearVerdict const floor = GearScore(EffectTrinket(SOME_OTHER_PROC), Wearer(GearRole::Melee));
    Expect("the unlisted one is a floor, as before",
           !floor.judged && floor.confidence == GearConfidence::Floor);
    Expect("at the 49 its attack power and item level come to", Near(floor.score, 49.f));
}

void TheScoreIsTheRolesOwn()
{
    // 20 attack power, 28 for the proc, and half a point per item level.
    Expect("a melee scores the effect trinket 77",
           Near(GearScore(EffectTrinket(), Wearer(GearRole::Melee)).score, 77.f));
    // 0.3 a point of attack power for a tank: 6 + 8.4 + 29.
    Expect("a tank scores it 43.4",
           Near(GearScore(EffectTrinket(), Wearer(GearRole::Tank)).score, 43.4f));
    // A hunter has the 20 attack power and not the proc.
    Expect("a hunter scores it 49",
           Near(GearScore(EffectTrinket(), Wearer(GearRole::Ranged)).score, 49.f));
    // A healer has neither.
    Expect("a healer scores it 29",
           Near(GearScore(EffectTrinket(), Wearer(GearRole::Healer)).score, 29.f));
    // 280 for 20 sec of every 120 is 46, and 33 for the item level.
    Expect("a hunter scores the use trinket 79",
           Near(GearScore(UseTrinket(), Wearer(GearRole::Ranged)).score, 79.f));
    Expect("and a healer scores it its item level",
           Near(GearScore(UseTrinket(), Wearer(GearRole::Healer)).score, 33.f));
    // 28 crit at 1.5 and 31.5 for the item level.
    Expect("a melee scores the flat trinket 73.5",
           Near(GearScore(FlatTrinket(), Wearer(GearRole::Melee)).score, 73.5f));
}

// THE SECOND SLOT (wow-overseer#2358, the second box). One flat trinket is in
// both trinket slots, a third copy and the effect trinket are in the bags.
void ARoleFittingEffectTrinketOutranksADuplicateFlatOneForTheSecondSlot()
{
    GearWearer const warrior = Wearer(GearRole::Melee);
    GearVerdict const flat = GearScore(FlatTrinket(), warrior);
    GearVerdict const effect = GearScore(EffectTrinket(), warrior);

    Expect("another copy of the flat trinket is refused for the second slot",
           GearIsSecondCopyOfTrinket(true, BREADTH, BREADTH));
    Expect("the effect trinket is not a copy of it",
           !GearIsSecondCopyOfTrinket(true, HAND, BREADTH));
    Expect("the effect trinket outranks the flat one on score", effect.score > flat.score);
    Expect("and is Better than the copy worn in the second slot",
           GearCompare(effect, GearWorn(flat)) == GearComparison::Better);
    Expect("which is not Better than it in turn",
           GearCompare(flat, GearWorn(effect)) == GearComparison::NotBetter);

    // WITHOUT THE PRICE IT NEVER SETTLED: a floor of 49 against 73.5.
    GearVerdict const unpriced = GearScore(EffectTrinket(SOME_OTHER_PROC), warrior);
    Expect("the same piece with its proc unread is left undecided",
           GearCompare(unpriced, GearWorn(flat)) == GearComparison::Undecided);

    // AND ONLY FOR THE ROLE IT FITS. A hunter keeps the flat crit trinket.
    GearWearer const hunter = Wearer(GearRole::Ranged);
    Expect("a hunter does not take a melee proc over flat crit",
           GearCompare(GearScore(EffectTrinket(), hunter), GearWorn(GearScore(FlatTrinket(), hunter))) ==
               GearComparison::NotBetter);
    // A healer's flat trinket is spell power; the proc is nothing to them.
    GearWearer const healer = Wearer(GearRole::Healer);
    GearItem const healing = Trinket("Flat Healing Charm", 60, {{MOD_SPELL_POWER, 20}}, {});
    Expect("nor does a healer over flat spell power",
           GearCompare(GearScore(EffectTrinket(), healer), GearWorn(GearScore(healing, healer))) ==
               GearComparison::NotBetter);
    // The on-use trinket takes a hunter's second slot the same way.
    Expect("a hunter's attack power use outranks the flat copy",
           GearCompare(GearScore(UseTrinket(), hunter), GearWorn(GearScore(FlatTrinket(), hunter))) ==
               GearComparison::Better);
}

}  // namespace

int main()
{
    TheTableIsSmallAndSaysWhoEachEffectFits();
    AKnownEffectIsReadAndAnUnknownOneIsStillAFloor();
    TheScoreIsTheRolesOwn();
    ARoleFittingEffectTrinketOutranksADuplicateFlatOneForTheSecondSlot();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a known trinket effect is priced for the role it fits\n");
    return EXIT_SUCCESS;
}
