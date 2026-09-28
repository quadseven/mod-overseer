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
}  // namespace

int main()
{
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
