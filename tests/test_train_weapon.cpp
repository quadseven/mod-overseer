// A member carrying a weapon it has no skill for buys the skill at a weapon
// master (wow-overseer#399). Measured on the dev realm on 2026-09-28: the level
// 35 mage carried a one-handed sword with no Swords skill and wore no main hand.
#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* name, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", name);
    ++failures;
}
}

int main()
{
    Check("the verb is recognised", IsWeaponTrainRow("train-weapon skill:43"));
    Check("a cast row is not it", !IsWeaponTrainRow("12345"));
    Check("a learn row is not it", !IsWeaponTrainRow("use entry:5"));
    Check("a trainer walk is not it", !IsWeaponTrainRow("walk-to-trainer skill:43"));

    WeaponTrainRequest const swords = ParseWeaponTrainRequest("train-weapon skill:43");
    Check("swords parses", swords.skill == 43 && std::string(swords.error).empty());

    WeaponTrainRequest const spaced = ParseWeaponTrainRequest("  train-weapon   skill:45 ");
    Check("blanks are tolerated", spaced.skill == 45);

    char const* bad[] = {"train-weapon", "train-weapon skill:", "train-weapon skill:0",
                         "train-weapon skill:x", "train-weapon skill:43 now",
                         "train-weapon spell:201", "train-weapon skill:99999999999"};
    for (char const* row : bad)
    {
        WeaponTrainRequest const r = ParseWeaponTrainRequest(row);
        Check(row, r.skill == 0 && std::string(r.error) == WeaponTrainRefusal::Malformed);
    }

    Check("no trainer in reach is retried",
          WeaponTrainRefusalRetryable(WeaponTrainRefusal::NoTrainer));
    Check("money is retried", WeaponTrainRefusalRetryable(WeaponTrainRefusal::NotTaught));
    Check("already held is final",
          !WeaponTrainRefusalRetryable(WeaponTrainRefusal::AlreadyHeld));
    Check("not a weapon is final",
          !WeaponTrainRefusalRetryable(WeaponTrainRefusal::NotAWeapon));
    Check("malformed is final", !WeaponTrainRefusalRetryable(WeaponTrainRefusal::Malformed));

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
