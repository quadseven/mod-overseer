/*
 * Who may hold a thing, decided without a world (mod-overseer#411).
 *
 * THE MEASUREMENT THIS FILE EXISTS FOR. A loot ranking offered entry 2280,
 * Kam's Walking Stick, to a ROGUE, against entry 6472, Stinging Viper, that the
 * rogue was already holding:
 *
 *     2280  class 2 (weapon)  subclass 10 (STAFF)  InventoryType 17 (two-hand)
 *           ItemLevel 27  RequiredLevel 22  AllowableClass -1
 *     6472  class 2 (weapon)  subclass 4 (one-hand mace)  InventoryType 13
 *           ItemLevel 24
 *
 * A rogue can never hold a staff. The ranking passed it because
 * `AllowableClass = -1` means the ITEM restricts nobody, and a class mask was
 * the only class gate applied. Whether a character may hold a weapon is not an
 * item property at all: it is a proficiency, granted by spells the class
 * learns, recorded in that character's own skills, and absent from
 * `item_template` entirely. So an item-level comparison plus a class mask keeps
 * offering staves and polearms to rogues and plate to mages, for ever, and
 * looks right every time.
 *
 * WHICH NUMBERS ARE REAL. Everything in the two blocks above is off the world
 * table and is used exactly as written. The damage figures below are NOT: the
 * issue records no damage line, and inventing one and calling it measured is
 * how a document goes stale. They are chosen to make the rule bite - a staff
 * good enough to beat the main hand on its own - and are labelled as such
 * wherever they appear. The proficiency cases need no damage at all, which is
 * the point of them: a refusal is reached before there is any number to
 * compare, so there is never a number to be tempted by.
 *
 * WHAT IT PINS, in order of what it would cost to get wrong again:
 *
 *   1. THE ROGUE IS NOT A CANDIDATE FOR 2280, and the refusal says which gate
 *      answered and which weapon it was about.
 *   2. EVERY MEMBER IS CHECKED, not just the one somebody noticed. The family
 *      sweep below asserts the whole list, admitted and refused, because the
 *      second name on that ranking was the one nobody looked at.
 *   3. A REFUSAL IS EXPLAINABLE. `why` is a sentence and `refusal` is a code,
 *      so a name missing from a list is answerable rather than mysterious. A
 *      character wrongly ON a list is loud; one wrongly OFF it is silent, and
 *      that is the worse of the two.
 *   4. A TWO-HANDER IS PRICED AGAINST BOTH HANDS. Three item levels of "gain"
 *      is a whole second weapon of loss for somebody dual-wielding.
 *   5. AN OFF-HAND WEAPON NEEDS DUAL WIELD. The mirror of the same error, and
 *      the one the core would otherwise refuse silently once every five
 *      seconds for ever.
 *
 * It compiles the pure file and this one and NOTHING ELSE, on the terms
 * tests/test_gear.cpp already set out: if a core type ever gets into one of
 * these decisions, this stops linking.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::GearConfidence;
using OverseerDecisions::GearIncumbentPair;
using OverseerDecisions::GearIncumbentSaid;
using OverseerDecisions::GearIncumbentScore;
using OverseerDecisions::GearIsUpgrade;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearRefusal;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearUsable;
using OverseerDecisions::GearUsability;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWeaponKindName;
using OverseerDecisions::GearWearer;

namespace
{

// The core's own ids, spelled out so a failure reads as a thing rather than a
// number. ItemTemplate.h:344-364 for the weapon subclasses, :262-284 for the
// inventory types.
int const CLASS_WEAPON = 2;
int const CLASS_ARMOUR = 4;

int const SUB_MACE_ONE_HAND = 4;
int const SUB_STAFF = 10;
int const SUB_DAGGER = 15;
int const SUB_CLOTH = 1;

int const INV_ONE_HAND = 13;
int const INV_TWO_HAND = 17;
int const INV_WEAPON_OFF_HAND = 22;
int const INV_HOLDABLE = 23;
int const INV_RANGED_RIGHT = 26;
int const INV_CHEST = 5;

int failures = 0;

void Fail(char const* what, std::string const& detail)
{
    ++failures;
    std::printf("FAIL %s: %s\n", what, detail.c_str());
}

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

void ExpectUsable(char const* what, GearUsability const& got)
{
    if (!got.usable)
        Fail(what, "expected usable, got refused with why='" + got.why + "'");
}

void ExpectRefusal(char const* what, GearUsability const& got, GearRefusal refusal,
                   char const* whyContains)
{
    if (got.usable)
    {
        Fail(what, "expected a refusal, got usable");
        return;
    }
    if (got.refusal != refusal)
    {
        Fail(what, "the refusal code is not the one expected; why='" + got.why + "'");
        return;
    }
    if (got.why.find(whyContains) == std::string::npos)
        Fail(what, std::string("expected the reason to mention '") + whyContains +
                       "', got '" + got.why + "'");
}

void ExpectText(char const* what, std::string const& got, std::string const& want)
{
    if (got != want)
        Fail(what, "got '" + got + "', want '" + want + "'");
}

// -------------------------------------------------------------- the items --

// Entry 2280, exactly as the world table carries it. `AllowableClass -1`
// restricts nobody, which is why it reaches every character on the roster:
// `classAllowed` below is true for all five, deliberately, because that is the
// state that produced the defect.
GearItem KamsWalkingStick(float dps = 0.f)
{
    GearItem item;
    item.name = "Kam's Walking Stick";
    item.itemClass = CLASS_WEAPON;
    item.subClass = SUB_STAFF;
    item.inventoryType = INV_TWO_HAND;
    item.itemLevel = 27;
    item.requiredLevel = 22;
    item.quality = 2;
    item.dps = dps;
    return item;
}

// Entry 6472, the item it was compared against.
GearItem StingingViper(float dps = 0.f)
{
    GearItem item;
    item.name = "Stinging Viper";
    item.itemClass = CLASS_WEAPON;
    item.subClass = SUB_MACE_ONE_HAND;
    item.inventoryType = INV_ONE_HAND;
    item.itemLevel = 24;
    item.quality = 2;
    item.dps = dps;
    return item;
}

GearItem Weapon(char const* name, int subClass, int inventoryType, int itemLevel, float dps)
{
    GearItem item;
    item.name = name;
    item.itemClass = CLASS_WEAPON;
    item.subClass = subClass;
    item.inventoryType = inventoryType;
    item.itemLevel = itemLevel;
    item.quality = 2;
    item.dps = dps;
    return item;
}

// --------------------------------------------------------- the characters --
//
// NAMED BY ROLE AND PROFICIENCY, never by class id, because a class id is
// exactly what this file must never be able to reason from. `weaponProficient`
// is the caller's answer for THIS item: whether the character holds the skill
// line ItemTemplate::GetSkill names for it. The adapter resolves it per item
// from the character's own skills, so these fixtures set it per item too.

GearWearer Member(char const* name, int level, GearRole role)
{
    GearWearer who;
    who.name = name;
    who.level = level;
    who.role = role;
    who.cloth = true;  // every class holds cloth from level 1
    return who;
}

// One member of the family, and whether the staff's skill line is one they
// hold. `holdsStaves` is the whole of what separates them here.
struct FamilyMember
{
    char const* name;
    int level;
    GearRole role;
    bool holdsStaves;
    bool dualWields;
};

// ------------------------------------------------------------- the cases --

// #411 itself. The rogue holds one-hand maces and daggers and no staff line at
// all, and the staff carries no class restriction, so every gate except
// proficiency admits it.
void TheRogueIsNotACandidateForTheStaff()
{
    GearWearer rogue = Member("the rogue", 22, GearRole::Melee);
    rogue.leather = true;
    rogue.canDualWield = true;
    rogue.classAllowed = true;      // AllowableClass -1 admits everybody
    rogue.weaponProficient = false; // no staff line in character_skills

    GearUsability const staff = GearUsable(KamsWalkingStick(), rogue);
    ExpectRefusal("a rogue is refused the staff", staff,
                  GearRefusal::NoWeaponProficiency, "no proficiency with this weapon");
    Check(staff.why.find("staff") != std::string::npos,
          "and the refusal names the weapon it is about");

    // The mace the staff was ranked against is admitted by the same call, from
    // the same character, so the refusal is about the staff and not about
    // weapons in general.
    GearWearer viperReader = rogue;
    viperReader.weaponProficient = true;
    ExpectUsable("and the one-hand mace it was compared against is not",
                 GearUsable(StingingViper(), viperReader));
}

// A REFUSAL IS REACHED BEFORE THERE IS A NUMBER. This is what stops the staff
// being "3 item levels better" in anybody's report: there is no score to be
// three better than.
void ARefusedItemNeverGetsAScore()
{
    GearWearer rogue = Member("the rogue", 22, GearRole::Melee);
    rogue.leather = true;
    rogue.weaponProficient = false;

    // A staff generous enough to beat anything, if it could be held at all.
    GearVerdict const verdict = GearScore(KamsWalkingStick(40.f), rogue);
    Check(!verdict.wearable, "a refused staff is not wearable");
    Check(verdict.score == 0.f, "a refused staff carries no score");
    Check(verdict.refusal == GearRefusal::NoWeaponProficiency,
          "and the verdict carries the code as well as the sentence");
    Check(verdict.confidence == GearConfidence::Exact,
          "a refusal is certain, not a guess");
    Check(!GearIsUpgrade(verdict, 0.f),
          "and it is not an upgrade even over an empty hand");
}

// EVERY CANDIDATE, NOT JUST THE ONE SOMEBODY NOTICED. The ranking that started
// #411 named a second wanter, and nobody checked them. This walks the whole
// family and asserts the entire list in both directions.
void EveryMemberIsCheckedAndEveryRefusalSaysWhy()
{
    std::vector<FamilyMember> const family = {
        {"the rogue", 22, GearRole::Melee, false, true},
        {"the second wanter", 24, GearRole::Caster, true, false},
        {"the tank", 27, GearRole::Tank, true, true},
        {"the healer", 22, GearRole::Healer, true, false},
        {"the one still too young", 20, GearRole::Melee, false, true},
    };

    int admitted = 0;
    int refused = 0;
    for (FamilyMember const& member : family)
    {
        GearWearer who = Member(member.name, member.level, member.role);
        who.canDualWield = member.dualWields;
        who.classAllowed = true;  // AllowableClass -1: nobody is shut out by the item
        who.weaponProficient = member.holdsStaves;

        GearUsability const answer = GearUsable(KamsWalkingStick(), who);
        if (answer.usable)
        {
            ++admitted;
            continue;
        }
        ++refused;

        // THE HALF THAT MAKES A MISSING NAME ANSWERABLE. Whatever the gate, the
        // refusal carries a code a caller can branch on and a sentence a person
        // can disagree with. An empty `why` here is the silent disappearance
        // this test exists to prevent.
        Check(!answer.why.empty(), "every refusal carries a sentence");
        Check(answer.refusal != GearRefusal::None, "every refusal carries a code");
    }

    // The level 20 member is refused for a reason that will expire, and the
    // rogue for one that never will. Both are refused; only one of them is
    // about the character being young, and the sentences have to differ.
    Check(admitted == 3, "three of the five hold the staff line");
    Check(refused == 2, "and two do not");

    GearWearer young = Member("the one still too young", 20, GearRole::Melee);
    young.weaponProficient = true;
    ExpectRefusal("a level 20 member is refused on level, not on proficiency",
                  GearUsable(KamsWalkingStick(), young),
                  GearRefusal::BelowRequiredLevel, "requires level 22");
}

// The gates are applied in the core's own order and each is final, so the first
// one to answer is the one reported. A character that is not allowed the item at
// all is told that rather than being told about a skill.
void TheFirstGateToAnswerIsTheOneReported()
{
    GearWearer who = Member("somebody else's item", 30, GearRole::Melee);
    who.classAllowed = false;
    who.weaponProficient = false;
    ExpectRefusal("the class mask answers before the skill does",
                  GearUsable(KamsWalkingStick(), who),
                  GearRefusal::WrongClass, "wrong class");

    GearWearer plateless = Member("no plate yet", 27, GearRole::Tank);
    plateless.leather = true;
    plateless.mail = true;
    GearItem breastplate;
    breastplate.name = "a plate breastplate";
    breastplate.itemClass = CLASS_ARMOUR;
    breastplate.subClass = 4;  // plate
    breastplate.inventoryType = INV_CHEST;
    breastplate.itemLevel = 30;
    breastplate.requiredLevel = 25;
    breastplate.armour = 300;
    ExpectRefusal("and armour is answered by its own skill line",
                  GearUsable(breastplate, plateless),
                  GearRefusal::NoArmourProficiency, "no plate proficiency");
}

// #411's second error, and the one that would still have been wrong if the
// staff had been holdable. A two-hander empties the off hand, so a character
// holding a weapon in each hand gives up BOTH to take it.
void ATwoHanderIsPricedAgainstBothHandsForADualWielder()
{
    GearWearer who = Member("a dual wielder who holds staves", 27, GearRole::Melee);
    who.leather = true;
    who.mail = true;
    who.canDualWield = true;
    who.weaponProficient = true;

    // Damage figures chosen, not measured - see the header. What is measured is
    // the SHAPE: the staff is three item levels above the main hand, which is
    // the "gain" the ranking reported.
    GearVerdict const mainHand =
        GearScore(Weapon("a one-hander", SUB_MACE_ONE_HAND, INV_ONE_HAND, 24, 15.f), who);
    GearVerdict const offHand =
        GearScore(Weapon("a second one-hander", SUB_DAGGER, INV_ONE_HAND, 22, 13.f), who);
    GearVerdict const staff = GearScore(KamsWalkingStick(22.f), who);

    Check(staff.wearable, "a character holding the staff line may hold the staff");

    // Against the main hand alone it wins, which is exactly the mistake.
    Check(GearIsUpgrade(staff, mainHand.score),
          "the staff beats the main hand on its own");

    GearIncumbentScore main;
    main.score = mainHand.score;
    GearIncumbentScore off;
    off.score = offHand.score;
    GearIncumbentScore const pair = GearIncumbentPair(main, off);
    Check(!GearIsUpgrade(staff, pair.score),
          "and it loses to the pair, because taking it costs the off hand too");

    // AND THE SENTENCE SAYS SO. The number alone cannot tell a reader whether
    // the off hand was counted; this is the half that makes it checkable.
    std::string const said = GearIncumbentSaid(pair, true, off.score);
    Check(said.find("for both hands") != std::string::npos,
          "the line says the comparison was against both hands");
    Check(said.find(std::to_string(static_cast<int>(off.score))) != std::string::npos,
          "and names what the off hand was worth");

    ExpectText("a one-hand swap says nothing extra", GearIncumbentSaid(main, false, 0.f),
               std::to_string(static_cast<int>(main.score)));

    GearIncumbentScore const empty;
    ExpectText("and an empty off hand is still said out loud",
               GearIncumbentSaid(main, true, empty.score),
               std::to_string(static_cast<int>(main.score)) +
                   " for both hands, and the off hand was empty");
}

// The mirror of the same error. An INVTYPE_WEAPONOFFHAND has exactly one home
// and the core refuses it there without dual wield, so offering it is a swap
// that can never land - re-attempted once every five seconds, for ever.
void AnOffHandWeaponNeedsDualWield()
{
    GearWearer caster = Member("a caster", 24, GearRole::Caster);
    caster.canDualWield = false;
    caster.weaponProficient = true;

    ExpectRefusal("an off-hand weapon is refused without dual wield",
                  GearUsable(Weapon("an off-hand blade", SUB_DAGGER, INV_WEAPON_OFF_HAND, 25, 14.f),
                             caster),
                  GearRefusal::NeedsDualWield, "cannot dual wield");

    GearWearer rogue = caster;
    rogue.name = "a rogue";
    rogue.canDualWield = true;
    ExpectUsable("and admitted for somebody who can",
                 GearUsable(Weapon("an off-hand blade", SUB_DAGGER, INV_WEAPON_OFF_HAND, 25, 14.f),
                            rogue));

    // A held-in-off-hand needs no dual wield at all - the core sends it to the
    // same slot and never asks (PlayerStorage.cpp:2034-2038 names only
    // INVTYPE_WEAPON and INVTYPE_WEAPONOFFHAND) - so refusing it here would take
    // away an item the character can genuinely hold.
    GearItem tome;
    tome.name = "something held in the off hand";
    tome.itemClass = CLASS_ARMOUR;
    tome.subClass = 0;  // misc: no proficiency of its own
    tome.inventoryType = INV_HOLDABLE;
    tome.itemLevel = 25;
    ExpectUsable("a held-in-off-hand needs no dual wield", GearUsable(tome, caster));

    // An INVTYPE_WEAPON has a main hand to go to, and the core declines to offer
    // the off-hand slot for one without dual wield (PlayerStorage.cpp:186-187),
    // so a rule here could only take away something holdable.
    ExpectUsable("and a plain one-hander is never refused for it",
                 GearUsable(StingingViper(), caster));

    // A ranged two-hander does NOT empty the off hand in 3.3.5: it goes to the
    // ranged slot, and a hunter holds a bow and a pair of melee weapons at once.
    ExpectUsable("nor is a ranged weapon",
                 GearUsable(Weapon("a bow", 2, INV_RANGED_RIGHT, 25, 18.f), caster));
}

// The sentence names the weapon so a reader can check the claim. It decides
// nothing, which is why it is allowed to be a table.
void TheRefusalNamesTheWeapon()
{
    ExpectText("a staff is called a staff", GearWeaponKindName(KamsWalkingStick()), "staff");
    ExpectText("and a one-hand mace a one-hand mace",
               GearWeaponKindName(StingingViper()), "one-hand mace");

    GearItem robe;
    robe.itemClass = CLASS_ARMOUR;
    robe.subClass = SUB_CLOTH;
    robe.inventoryType = INV_CHEST;
    ExpectText("armour has no weapon name", GearWeaponKindName(robe), "");
}

}  // namespace

int main()
{
    TheRogueIsNotACandidateForTheStaff();
    ARefusedItemNeverGetsAScore();
    EveryMemberIsCheckedAndEveryRefusalSaysWhy();
    TheFirstGateToAnswerIsTheOneReported();
    ATwoHanderIsPricedAgainstBothHandsForADualWielder();
    AnOffHandWeaponNeedsDualWield();
    TheRefusalNamesTheWeapon();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("who may hold a thing holds\n");
    return EXIT_SUCCESS;
}
