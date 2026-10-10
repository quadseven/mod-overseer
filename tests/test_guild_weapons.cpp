/*
 * A natural guild member buys its own weapon, and rests out its sickness.
 *
 * Measured on wow-dev (2026-09-28): members at levels 10 to 16 wore item level
 * 2 to 5 gear and died about 550 times in 30 minutes to creatures at or up to
 * two levels above them; spirit-healer revives sent them back out at a quarter
 * of their stats.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;
void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A WEAPON IN ANY BAG, AND A RANGED ONE, SENDS A GUILD MEMBER TO THE WEAPON
// MASTER (2026-10-10). Measured on the dev realm: 32 guild members carried 33
// weapons that beat what they wore by their own spec's gear score and that
// waited only on a weapon skill their class can learn. The drive read the
// backpack's main-hand weapons alone, so it saw 8 of them: 19 sat in a bag
// beyond the backpack and 4 were bows, guns or a thrown weapon.
CarriedWeapon Weapon(uint32_t inventoryType, uint32_t subclass, uint32_t itemLevel)
{
    CarriedWeapon weapon;
    weapon.inventoryType = inventoryType;
    weapon.subclass = subclass;
    weapon.itemLevel = itemLevel;
    weapon.requiredLevel = 1;
    weapon.skillLearnable = true;
    return weapon;
}

void AWeaponInAnyBagOrTheRangedSlotIsTrainedFor()
{
    // A Draenei mage at level 25 wearing a level 8 staff, with Thief's Blade
    // (a one-hand sword, item level 22) in its backpack and nothing ranged.
    std::vector<CarriedWeapon> mage = {Weapon(13, 7, 22)};
    std::vector<std::size_t> picks = WeaponsWorthTraining(mage, 25, 8, 0, 3);
    Check(picks.size() == 1 && picks[0] == 0, "a better sword it has no skill for is trained for");

    // A hunter at level 12 wearing a level 3 bow, a Pellet Rifle (a gun, item
    // level 7, inventory type 26) in a bag.
    std::vector<CarriedWeapon> hunter = {Weapon(26, 3, 7)};
    picks = WeaponsWorthTraining(hunter, 12, 10, 3, 3);
    Check(picks.size() == 1 && picks[0] == 0,
          "a ranged weapon is judged against the worn ranged piece, not the main hand");
    picks = WeaponsWorthTraining(hunter, 12, 10, 5, 3);
    Check(picks.empty(), "and a ranged weapon inside the gain is not");
    std::vector<CarriedWeapon> thrower = {Weapon(25, 16, 12)};
    Check(WeaponsWorthTraining(thrower, 20, 15, 0, 3).size() == 1, "a thrown weapon counts too");

    // The best gain first, each judged against its own slot.
    std::vector<CarriedWeapon> both = {Weapon(13, 7, 14), Weapon(15, 2, 20), Weapon(17, 8, 30)};
    picks = WeaponsWorthTraining(both, 30, 10, 12, 3);
    Check(picks.size() == 3 && picks[0] == 2 && picks[1] == 1 && picks[2] == 0,
          "the biggest gain over its own slot is trained for first");

    CarriedWeapon known = Weapon(13, 7, 22);
    known.skillKnown = true;
    CarriedWeapon unlearnable = Weapon(13, 4, 22);
    unlearnable.skillLearnable = false;
    CarriedWeapon tooHigh = Weapon(13, 7, 22);
    tooHigh.requiredLevel = 26;
    CarriedWeapon wand = Weapon(26, 19, 30);
    CarriedWeapon pick = Weapon(17, 14, 30);
    CarriedWeapon held = Weapon(23, 0, 30);
    Check(WeaponsWorthTraining({known, unlearnable, tooHigh, wand, pick, held}, 25, 8, 0, 3).empty(),
          "never a skill it holds, one its class cannot learn, a weapon above its level, a "
          "wand, a tool or a piece that is not a weapon");
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheDriveReadsEveryBag()
{
    std::string const src = ReadModule();
    std::size_t const drive = src.find("void DriveGuildWeaponFor(");
    std::size_t const vendors = src.find("// 2. THE BEST WEAPON A VENDOR ON ITS MAP SELLS IT.");
    Check(drive != std::string::npos && vendors != std::string::npos && vendors > drive,
          "the drive and its vendor step are where the test looks");
    if (drive == std::string::npos || vendors == std::string::npos || vendors < drive)
        return;
    std::string const lesson = src.substr(drive, vendors - drive);
    Check(lesson.find("bot->GetBagByPos(b)") != std::string::npos,
          "the lesson step reads the bags beyond the backpack");
    Check(lesson.find("EQUIPMENT_SLOT_RANGED") != std::string::npos,
          "and the worn ranged piece");
    Check(lesson.find("OverseerDecisions::WeaponsWorthTraining(") != std::string::npos,
          "and asks the pure pick which weapon is worth a lesson");
}
}  // namespace

int main()
{
    AWeaponInAnyBagOrTheRangedSlotIsTrainedFor();
    TheDriveReadsEveryBag();

    std::vector<WeaponOffer> const offers = {
        {9, 900, 300.f},    // 0: a level-9 sword, 9 silver
        {13, 2400, 800.f},  // 1: better, 24 silver
        {13, 2100, 900.f},  // 2: same level, cheaper, further
        {20, 90000, 50.f},  // 3: far beyond the purse
        {5, 100, 10.f},     // 4: no better than a starting weapon
    };
    Check(ChooseWeaponOffer(offers, 4, 4000, 500, 3) == 2,
          "the best affordable item level, cheaper on a tie");
    Check(ChooseWeaponOffer(offers, 4, 1500, 500, 3) == 0,
          "a thinner purse buys the best it can afford");
    Check(ChooseWeaponOffer(offers, 4, 400, 500, 3) == -1,
          "the reserve is never spent");
    Check(ChooseWeaponOffer(offers, 12, 4000, 500, 3) == -1,
          "a worn weapon within the gain is kept");
    Check(ChooseWeaponOffer({}, 4, 4000, 500, 3) == -1, "no offers, no walk");

    Check(WeaponSkillSpellFor(8) == 202, "two-handed swords are spell 202");
    Check(WeaponSkillSpellFor(15) == 1180, "daggers are spell 1180");
    Check(WeaponSkillSpellFor(14) == 0, "a miscellaneous weapon has no teacher");

    RevivedSickGroundFacts sick;
    sick.sick = true;
    sick.memberLevel = 12;
    sick.groundTopLevel = 10;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::Stay,
          "a family member on safe ground carries on");
    sick.restWhileSick = true;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::HoldOutOfCombat,
          "a guild member rests its sickness out on safe ground");
    sick.groundTopLevel = 20;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::HoldOutOfCombat,
          "on lethal ground with no stone ready it still holds out of combat");
    sick.hearthReady = true;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::Hearth,
          "on lethal ground with the stone ready it hearths away");
    sick.resting = true;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::HoldOutOfCombat,
          "in an inn it rests");
    sick.sick = false;
    Check(DecideRevivedSickGround(sick) == RevivedSickGroundStep::Stay,
          "no sickness, no rest");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild weapons: all checks passed\n");
    return 0;
}
