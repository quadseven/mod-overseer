/*
 * What a piece of gear is worth to one character, decided without a world.
 *
 * WHY THESE CASES AND NOT INVENTED ONES. Every item in this file is a real row
 * out of the live world's `item_template`, and every character is a real row
 * out of `characters` and `character_skills`, because the defect being pinned
 * was not a hypothesis - it was a level 27 warrior tank standing in a cloth
 * robe and leather boots with four pieces of mail on around them, and a level
 * 22 priest carrying leather she can never put on. A test written from
 * imagined numbers would have passed on the day the family was wrong.
 *
 * WHAT IT PINS, in order of how much it would cost to get wrong again:
 *
 *   1. PROFICIENCY IS PER CHARACTER. The same warrior facts, with and without
 *      a plate row, give opposite answers about the same breastplate. Nothing
 *      here may reach that answer from the class id, because a warrior learns
 *      plate at 40 and the one this was measured on is 27.
 *   2. ITEM LEVEL DOES NOT DECIDE. A mail boot three item levels BELOW a
 *      leather boot wins for a tank, because it carries twice the armour; and
 *      a stat-less robe two item levels ABOVE a healer's loses, because it
 *      carries no intellect. Both directions, because fixing one by breaking
 *      the other is the easiest possible regression.
 *   3. A REFUSAL IS FINAL AND HAS NO NUMBER. An item the character cannot wear
 *      never gets a score to be compared, which is what stops the priest
 *      needing leather again.
 *   4. THE SEVERING AXE (#14). A two-hander is measured against both hands.
 *
 * It compiles the pure file and this one and NOTHING ELSE, on the terms
 * tests/test_professions.cpp already set out: if a core type ever gets into a
 * decision, this stops linking.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::GearContender;
using OverseerDecisions::GearIncumbent;
using OverseerDecisions::GearIsUpgrade;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearNeedWinner;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearStat;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWearer;

namespace
{

// The core's own ids, spelled so a failure reads as a thing rather than a
// number. AzerothCore ItemTemplate.h: item classes at :25-70 for the stat
// types, armour subclasses in the GetSkill table at :793-795.
int const CLASS_WEAPON = 2;
int const CLASS_ARMOUR = 4;
int const CLASS_RECIPE = 9;

int const SUB_MISC = 0;
int const SUB_CLOTH = 1;
int const SUB_LEATHER = 2;
int const SUB_MAIL = 3;
int const SUB_PLATE = 4;
int const SUB_SHIELD = 6;

int const INV_CHEST = 5;
int const INV_LEGS = 7;
int const INV_FEET = 8;
int const INV_HANDS = 10;
int const INV_ONE_HAND = 13;
int const INV_SHIELD_SLOT = 14;
int const INV_TWO_HAND = 17;

int const MOD_INTELLECT = 5;
int const MOD_SPIRIT = 6;
int const MOD_STAMINA = 7;
int const MOD_STRENGTH = 4;

int failures = 0;

void Fail(char const* what, std::string const& detail)
{
    ++failures;
    std::printf("FAIL %s: %s\n", what, detail.c_str());
}

void ExpectWearable(char const* what, GearVerdict const& got)
{
    if (!got.wearable)
        Fail(what, "expected wearable, got refused with why=" + got.why);
}

void ExpectRefused(char const* what, GearVerdict const& got, char const* becauseContains)
{
    if (got.wearable)
    {
        Fail(what, "expected a refusal, got a wearable item scoring " +
                       std::to_string(static_cast<int>(got.score)));
        return;
    }
    if (got.score != 0.f)
        Fail(what, "a refusal must carry no score, got " +
                       std::to_string(static_cast<int>(got.score)));
    if (got.why.find(becauseContains) == std::string::npos)
        Fail(what, std::string("expected the reason to mention '") + becauseContains +
                       "', got '" + got.why + "'");
}

void ExpectScore(char const* what, GearVerdict const& got, float expected)
{
    // Exact on purpose. These are the numbers the weighting produces, and a
    // change to the weighting SHOULD have to come here and say so out loud.
    if (got.score < expected - 0.01f || got.score > expected + 0.01f)
        Fail(what, "expected a score of " + std::to_string(expected) + ", got " +
                       std::to_string(got.score));
}

void ExpectUpgrade(char const* what, GearVerdict const& candidate, float incumbent)
{
    if (!GearIsUpgrade(candidate, incumbent))
        Fail(what, "expected an upgrade: " + std::to_string(candidate.score) +
                       " against " + std::to_string(incumbent));
}

void ExpectNoUpgrade(char const* what, GearVerdict const& candidate, float incumbent)
{
    if (GearIsUpgrade(candidate, incumbent))
        Fail(what, "expected NO upgrade: " + std::to_string(candidate.score) +
                       " against " + std::to_string(incumbent));
}

void ExpectJudged(char const* what, GearVerdict const& got, bool judged)
{
    if (got.judged != judged)
        Fail(what, std::string("expected judged=") + (judged ? "true" : "false") +
                       ", got why='" + got.why + "'");
}

void ExpectName(char const* what, std::string const& got, char const* expected)
{
    if (got != expected)
        Fail(what, std::string("expected '") + expected + "', got '" + got + "'");
}

// ---------------------------------------------------------- the characters --

// Level 27 human warrior, protection. His real skill rows are mail, leather,
// cloth and shield - and NO plate, because a warrior learns plate at 40.
GearWearer Tank()
{
    GearWearer who;
    who.name = "the tank";
    who.level = 27;
    who.role = GearRole::Tank;
    who.cloth = true;
    who.leather = true;
    who.mail = true;
    who.plate = false;
    who.shield = true;
    return who;
}

// Level 22 priest, holy. One armour proficiency and no others.
GearWearer Healer()
{
    GearWearer who;
    who.name = "the healer";
    who.level = 22;
    who.role = GearRole::Healer;
    who.cloth = true;
    return who;
}

// Level 24 paladin, retribution: mail, leather, cloth AND a shield he is not
// meant to be holding.
GearWearer MeleeWithAShield()
{
    GearWearer who;
    who.name = "the retribution paladin";
    who.level = 24;
    who.role = GearRole::Melee;
    who.cloth = true;
    who.leather = true;
    who.mail = true;
    who.shield = true;
    return who;
}

// -------------------------------------------------------------- the items --

GearItem Armour(char const* name, int subClass, int inventoryType, int itemLevel,
                int requiredLevel, int armour, std::vector<GearStat> stats = {})
{
    GearItem item;
    item.name = name;
    item.itemClass = CLASS_ARMOUR;
    item.subClass = subClass;
    item.inventoryType = inventoryType;
    item.itemLevel = itemLevel;
    item.requiredLevel = requiredLevel;
    item.quality = 2;
    item.armour = armour;
    item.stats = stats;
    return item;
}

GearItem Weapon(char const* name, int inventoryType, int itemLevel, float dps,
                std::vector<GearStat> stats = {})
{
    GearItem item;
    item.name = name;
    item.itemClass = CLASS_WEAPON;
    item.subClass = 7;  // sword; the score does not read a weapon subclass
    item.inventoryType = inventoryType;
    item.itemLevel = itemLevel;
    item.quality = 2;
    item.dps = dps;
    item.stats = stats;
    return item;
}

// ---------------------------------------------------------------- the cases --

// The measurement this whole change came from. Raider's Boots, mail, item level
// 19, 113 armour, no stats, any class - against the leather boots the tank is
// actually wearing, item level 22, 56 armour, no stats. Twice the armour, three
// item levels lower. Item level alone gets this backwards.
void MailBeatsLeatherForATankEvenThreeItemLevelsDown()
{
    GearWearer const who = Tank();
    GearVerdict const worn =
        GearScore(Armour("Scouting Boots", SUB_LEATHER, INV_FEET, 22, 17, 56), who);
    GearVerdict const drop =
        GearScore(Armour("Raider's Boots", SUB_MAIL, INV_FEET, 19, 14, 113), who);

    ExpectWearable("the tank may wear leather", worn);
    ExpectWearable("the tank may wear mail", drop);
    ExpectScore("the worn leather boot", worn, 67.f);
    ExpectScore("the dropped mail boot", drop, 122.5f);
    ExpectUpgrade("mail beats leather for a tank", drop, worn.score);

    // And the other way round: having taken the mail, the leather is not a
    // reason to swap back. The sweep must not oscillate.
    ExpectNoUpgrade("and the leather does not win it back", worn, drop.score);
}

// The same two boots for someone the armour is worth less to. A rogue is in
// leather because a rogue is in leather; the point is that the tank's answer is
// not hard-wired for everyone.
void TheSameTwoBootsAreWorthLessToSomeoneNotHoldingTheBoss()
{
    GearWearer who = Tank();
    who.role = GearRole::Melee;
    GearVerdict const worn =
        GearScore(Armour("Scouting Boots", SUB_LEATHER, INV_FEET, 22, 17, 56), who);
    GearVerdict const drop =
        GearScore(Armour("Raider's Boots", SUB_MAIL, INV_FEET, 19, 14, 113), who);

    // 0.30 an armour point rather than a whole one: 16.8 + 11 against 33.9 + 9.5.
    ExpectScore("melee values the leather boot", worn, 27.8f);
    ExpectScore("melee values the mail boot", drop, 43.4f);
    ExpectUpgrade("armour still decides between two stat-less boots", drop, worn.score);
}

// The second half of the measurement: a cloth robe on the tank. He CAN wear it -
// he holds the cloth skill - so this is not a proficiency refusal, it is the
// armour number doing the work a "wrong armour class" penalty would otherwise
// have to be invented for.
void ClothOnAPlateTrackTankLosesOnArmourAlone()
{
    GearWearer const who = Tank();
    GearVerdict const robe =
        GearScore(Armour("Ritual Shroud", SUB_CLOTH, INV_CHEST, 24, 19, 38), who);
    GearVerdict const mail =
        GearScore(Armour("a mail chest", SUB_MAIL, INV_CHEST, 20, 15, 168), who);

    ExpectWearable("the tank may wear cloth, he just should not", robe);
    ExpectUpgrade("four item levels lower and still the mail", mail, robe.score);
}

// PROFICIENCY IS READ PER CHARACTER. Identical facts but for one boolean, and
// the answer about the same breastplate is opposite. Nothing may reach this
// from the class id.
void ProficiencyIsPerCharacterAndNotPerClass()
{
    GearItem const plate =
        Armour("a plate chest", SUB_PLATE, INV_CHEST, 25, 20, 300);

    GearWearer const young = Tank();  // level 27: mail, no plate row
    ExpectRefused("a warrior below 40 has no plate row", GearScore(plate, young),
                  "no plate proficiency");

    GearWearer grown = Tank();
    grown.level = 40;
    grown.plate = true;
    GearVerdict const allowed = GearScore(plate, grown);
    ExpectWearable("the same warrior with the plate row may wear it", allowed);
    ExpectScore("and it is scored on its armour", allowed, 312.5f);
}

// The priest and the leather. This is the one that must never produce a number:
// a score invites a comparison, and the comparison is what had her needing
// boots she can never put on.
void APriestIsNeverOfferedLeatherHoweverGoodItIs()
{
    GearWearer const who = Healer();  // level 22, cloth and nothing else

    // THE GATES RUN IN THE CORE'S OWN ORDER, and this test learned that the
    // hard way rather than assuming it: the two Dervish pieces are refused on
    // LEVEL, not on leather, because they require 23 and she is 22. That is
    // the core's order too - Player::CanUseItem checks the level and never
    // checks armour proficiency at all - and it is worth pinning, because it
    // means two of the five pieces rotting in her bags she could not have put
    // on even if she had been born a rogue.
    ExpectRefused("Dervish Boots are out of her level before they are leather",
                  GearScore(Armour("Dervish Boots", SUB_LEATHER, INV_FEET, 28, 23, 62), who),
                  "requires level 23");
    ExpectRefused("and so are Dervish Gloves",
                  GearScore(Armour("Dervish Gloves", SUB_LEATHER, INV_HANDS, 28, 23, 56), who),
                  "requires level 23");

    // The other three are within her level and refused on the armour class
    // alone, which is the rule this change exists for. All three are real rows
    // out of her real bags.
    ExpectRefused("Feral Bindings are leather",
                  GearScore(Armour("Feral Bindings", SUB_LEATHER, 9, 16, 11, 31), who),
                  "no leather proficiency");
    ExpectRefused("Tribal Bracers are leather",
                  GearScore(Armour("Tribal Bracers", SUB_LEATHER, 9, 10, 5, 23), who),
                  "no leather proficiency");
    ExpectRefused("Veteran Boots are mail",
                  GearScore(Armour("Veteran Boots", SUB_MAIL, INV_FEET, 14, 9, 89), who),
                  "no mail proficiency");

    // And a refusal is never an upgrade, whichever gate produced it. Feral
    // Bindings carry 31 armour against the 13 on the bracers she wears, and
    // still do not move.
    GearVerdict const worn =
        GearScore(Armour("Mystic's Bracelets", SUB_CLOTH, 9, 17, 12, 13), who);
    GearVerdict const leather =
        GearScore(Armour("Feral Bindings", SUB_LEATHER, 9, 16, 11, 31), who);
    ExpectWearable("her own cloth bracers are fine", worn);
    ExpectNoUpgrade("a refusal is never an upgrade", leather, worn.score);
}

// The same error in the other direction, and the reason the weighting is a
// weighting rather than a bigger armour number. Her worn robe is two item
// levels LOWER and carries the intellect and spirit a healer is wearing gear
// for; the newer one carries nothing at all.
void StatsOutweighTwoItemLevelsForAHealer()
{
    GearWearer const who = Healer();
    GearVerdict const worn = GearScore(
        Armour("Seer's Robe", SUB_CLOTH, INV_CHEST, 21, 16, 35,
               {GearStat{MOD_INTELLECT, 6}, GearStat{MOD_SPIRIT, 3}}),
        who);
    GearVerdict const newer =
        GearScore(Armour("Buccaneer's Vest", SUB_CLOTH, INV_CHEST, 23, 18, 37), who);

    ExpectScore("the robe with intellect and spirit", worn, 35.f);
    ExpectScore("the newer robe with nothing on it", newer, 15.2f);
    ExpectNoUpgrade("item level does not buy the swap", newer, worn.score);
}

void RequiredLevelIsAGateAndSaysSo()
{
    GearWearer const who = Healer();  // level 22
    ExpectRefused("a level 30 robe on a level 22 priest",
                  GearScore(Armour("something better later", SUB_CLOTH, INV_CHEST, 40, 30, 90), who),
                  "requires level 30");
}

void AClassThatMayNotUseItIsRefusedBeforeAnythingElse()
{
    GearWearer who = Tank();
    who.classAllowed = false;
    ExpectRefused("the class mask is the first gate",
                  GearScore(Armour("somebody else's mail", SUB_MAIL, INV_CHEST, 25, 20, 200), who),
                  "wrong class");
}

void AWeaponNeedsItsOwnSkill()
{
    GearWearer who = Healer();
    who.weaponProficient = false;
    ExpectRefused("no proficiency with the weapon",
                  GearScore(Weapon("a two-handed axe", INV_TWO_HAND, 25, 20.f), who),
                  "no proficiency with this weapon");
}

void AnEmptySlotIsAlwaysAnUpgrade()
{
    GearWearer const who = Healer();
    GearVerdict const anything =
        GearScore(Armour("Mystic's Slippers", SUB_CLOTH, INV_FEET, 18, 13, 22), who);
    ExpectUpgrade("the first item into an empty slot", anything,
                  GearIncumbent(0.f, 0.f, false));
}

// #14, the Severing Axe. A green two-hander is not an upgrade for a tank
// holding a shield, however good the axe is on its own, because taking it
// empties the off hand.
void ATwoHanderIsMeasuredAgainstBothHands()
{
    GearWearer const who = Tank();
    GearVerdict const mainHand = GearScore(Weapon("Ironpatch Blade", INV_ONE_HAND, 20, 14.f), who);
    GearVerdict const shield =
        GearScore(Armour("Dervish Buckler", SUB_SHIELD, INV_SHIELD_SLOT, 28, 23, 545), who);
    GearVerdict const axe = GearScore(
        Weapon("Severing Axe", INV_TWO_HAND, 25, 30.f,
               {GearStat{MOD_STRENGTH, 6}, GearStat{MOD_STAMINA, 4}}),
        who);

    // Against the main hand alone it would win, which is exactly the mistake.
    ExpectUpgrade("the axe beats the one-hander on its own", axe, mainHand.score);
    ExpectNoUpgrade("and loses to the pair", axe,
                    GearIncumbent(mainHand.score, shield.score, true));
}

// A shield carries an order of magnitude more armour than anything else, and
// only a tank is choosing to hold one instead of a weapon.
void AShieldIsOnlyWorthItsArmourToATank()
{
    GearWearer const ret = MeleeWithAShield();
    GearVerdict const shield =
        GearScore(Armour("Scouting Buckler", SUB_SHIELD, INV_SHIELD_SLOT, 22, 17, 445), ret);
    GearVerdict const offHand = GearScore(Weapon("an off-hand sword", INV_ONE_HAND, 22, 12.f), ret);
    ExpectUpgrade("a retribution paladin wants the weapon", offHand, shield.score);

    GearWearer const tank = Tank();
    GearVerdict const tankShield =
        GearScore(Armour("Scouting Buckler", SUB_SHIELD, INV_SHIELD_SLOT, 22, 17, 445), tank);
    GearVerdict const tankOffHand = GearScore(Weapon("an off-hand sword", INV_ONE_HAND, 22, 12.f), tank);
    ExpectNoUpgrade("and a tank does not", tankOffHand, tankShield.score);
}

// Being honest about coverage. None of these is a refusal - the item is
// wearable and has a score - but the score is not the whole story, and the
// caller is told so rather than acting on it.
void WhatItWillNotClaimToHaveJudged()
{
    GearWearer const who = Tank();

    GearItem proc = Armour("something with a proc", SUB_MAIL, INV_CHEST, 25, 20, 200);
    proc.hasEffect = true;
    GearVerdict const procVerdict = GearScore(proc, who);
    ExpectWearable("a proc item is still wearable", procVerdict);
    ExpectJudged("but it is not judged", procVerdict, false);

    GearItem suffix = Armour("something of the Bear", SUB_MAIL, INV_CHEST, 25, 20, 200);
    suffix.unresolvedRandomProperty = true;
    ExpectJudged("nor is an unresolved random suffix", GearScore(suffix, who), false);

    GearWearer noRole = Tank();
    noRole.role = GearRole::Unknown;
    ExpectJudged("nor is anything for a character with no role",
                 GearScore(Armour("mail", SUB_MAIL, INV_CHEST, 25, 20, 200), noRole), false);

    GearItem recipe;
    recipe.name = "a recipe";
    recipe.itemClass = CLASS_RECIPE;
    recipe.itemLevel = 30;
    ExpectJudged("nor is anything that is not worn", GearScore(recipe, who), false);

    // A refusal, by contrast, IS a judgement: it is certain, and the caller may
    // act on it. This is what lets an unwearable drop still be greeded rather
    // than left for somebody else to decide.
    ExpectJudged("but a refusal is decided",
                 GearScore(Armour("plate", SUB_PLATE, INV_CHEST, 25, 20, 300), who), true);

    // And a plain piece of gear on a character with a role is judged.
    ExpectJudged("and so is an ordinary piece of mail",
                 GearScore(Armour("mail", SUB_MAIL, INV_CHEST, 25, 20, 200), who), true);
}

// A ring with no armour and no stats is the thinnest thing the score ever sees;
// it must not come out negative, and it must not beat a real one.
void SomethingWorthNothingIsWorthNothingAndNotLess()
{
    GearWearer const who = Healer();
    GearVerdict const plain = GearScore(Armour("Quartz Ring", SUB_MISC, 11, 20, 15, 0), who);
    ExpectWearable("a ring needs no proficiency", plain);
    ExpectScore("and is worth its item level and nothing else", plain, 10.f);
}

// The margin exists so a family does not spend its life swapping. A gain of
// less than one percent is not a reason to move.
void AMarginStopsTheFamilySwappingForever()
{
    GearWearer const who = Tank();
    GearVerdict const barely =
        GearScore(Armour("a hair better", SUB_MAIL, INV_LEGS, 20, 15, 201), who);
    ExpectNoUpgrade("half a point is not a reason to move", barely, 210.5f);
    ExpectUpgrade("but a real gain is", barely, 150.f);
}

// Who needs when two of them want the same drop: the one with the lower total
// equipped score, so a run raises the party's floor.
void TheWorstGearedMemberNeedsAndTheRestGreed()
{
    std::vector<GearContender> contenders;
    contenders.push_back(GearContender{"the well geared one", 40.f, 900.f});
    contenders.push_back(GearContender{"the poorly geared one", 10.f, 300.f});
    ExpectName("the party's floor is what rises", GearNeedWinner(contenders),
               "the poorly geared one");

    // A tie on total goes to the bigger gain.
    std::vector<GearContender> tied;
    tied.push_back(GearContender{"small gain", 5.f, 500.f});
    tied.push_back(GearContender{"big gain", 50.f, 500.f});
    ExpectName("a tie goes to the bigger gain", GearNeedWinner(tied), "big gain");

    // A tie on both goes to the name, so the answer never depends on the order
    // the party happens to be walked in.
    std::vector<GearContender> identical;
    identical.push_back(GearContender{"zebra", 5.f, 500.f});
    identical.push_back(GearContender{"aardvark", 5.f, 500.f});
    ExpectName("and a tie on both is decided, not arbitrary",
               GearNeedWinner(identical), "aardvark");
    std::vector<GearContender> reversed;
    reversed.push_back(GearContender{"aardvark", 5.f, 500.f});
    reversed.push_back(GearContender{"zebra", 5.f, 500.f});
    ExpectName("whichever order they are offered in", GearNeedWinner(reversed),
               "aardvark");

    // Nobody wanting it is not somebody needing it.
    std::vector<GearContender> nobody;
    nobody.push_back(GearContender{"not for me", 0.f, 300.f});
    nobody.push_back(GearContender{"nor me", -20.f, 100.f});
    ExpectName("nobody needs what nobody wants", GearNeedWinner(nobody), "");
    ExpectName("and an empty party needs nothing", GearNeedWinner({}), "");
}

}  // namespace

int main()
{
    MailBeatsLeatherForATankEvenThreeItemLevelsDown();
    TheSameTwoBootsAreWorthLessToSomeoneNotHoldingTheBoss();
    ClothOnAPlateTrackTankLosesOnArmourAlone();
    ProficiencyIsPerCharacterAndNotPerClass();
    APriestIsNeverOfferedLeatherHoweverGoodItIs();
    StatsOutweighTwoItemLevelsForAHealer();
    RequiredLevelIsAGateAndSaysSo();
    AClassThatMayNotUseItIsRefusedBeforeAnythingElse();
    AWeaponNeedsItsOwnSkill();
    AnEmptySlotIsAlwaysAnUpgrade();
    ATwoHanderIsMeasuredAgainstBothHands();
    AShieldIsOnlyWorthItsArmourToATank();
    WhatItWillNotClaimToHaveJudged();
    SomethingWorthNothingIsWorthNothingAndNotLess();
    AMarginStopsTheFamilySwappingForever();
    TheWorstGearedMemberNeedsAndTheRestGreed();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the gear decisions hold\n");
    return EXIT_SUCCESS;
}
