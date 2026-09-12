/*
 * Does the economy errand budget (test_errand_budget.cpp) even apply to this
 * character's job? (infra#3613, mod-overseer's own sibling of #435/#438.)
 *
 * WHAT IT IS FOR. ErrandBudgetLimits answers "is a town trip eating the time
 * this character should be questing", and it was built and measured against a
 * character whose `job` is the schema default - picking up a vendor or repair
 * aim only incidentally while carrying sale goods for questing siblings. It
 * has no meaning for a character on job='craft': that character was never
 * going to be questing either way, and craft_supply.py's standing vendor
 * errand is not incidental, it IS the job. Charging that errand against a
 * budget invented to protect questing time called the errand off and refused
 * it for a fifteen-minute cool-off it could never earn back down, because a
 * craft-only character has no other job to bring its spend below the line -
 * so the refusal recurred every time the cool-off lifted, forever, and the
 * vial the character needed was never bought. Measured live: a vendor aim
 * written by craft_supply.py, refused within minutes ("economy errands had
 * taken more than their share of this character's time"), and rewritten by
 * the next ten-minute cycle only to be refused again.
 *
 * WHAT IS PINNED HERE:
 *
 *   - job='craft' is exempt. This is the whole of the fix.
 *   - Every other job still pays the budget, INCLUDING one that looks
 *     superficially similar (a made-up mode this rule has not been told
 *     about) - a rule that exempted anything it did not recognise would fail
 *     open, and this exists to fail closed, exactly like DriveQuests' own job
 *     gate does for the drive beside this one.
 *   - The empty string - the shape LoadJobs() never actually returns, since
 *     it filters 'quest' out of its own SELECT, but the one a caller reaches
 *     for a character with no row in the map at all - still pays the budget.
 *     Absence from the map means "as far as this rule is concerned, nothing
 *     exempts this character", not "assume the exemption".
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into this decision this stops building,
 * which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::MaintenanceBudgetApplies;

namespace
{

int failures = 0;

void CheckApplies(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: applies is %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// THE FIX ITSELF: a character whose job is 'craft' is not charged.
void CraftIsExempt()
{
    CheckApplies("job=craft", MaintenanceBudgetApplies("craft"), false);
}

// Every job this rule has been told nothing special about still pays the
// budget - failing closed, not open, for a mode nobody has reasoned about yet.
void EveryOtherJobStillPays()
{
    CheckApplies("job=quest", MaintenanceBudgetApplies("quest"), true);
    CheckApplies("job=train", MaintenanceBudgetApplies("train"), true);
    CheckApplies("job=dungeon:deadmines", MaintenanceBudgetApplies("dungeon:deadmines"), true);
    CheckApplies("an unrecognised job", MaintenanceBudgetApplies("some future mode"), true);
}

// A character absent from LoadJobs()'s map - which is every character on the
// schema default, and every character on a schema too old to carry `job` at
// all - is represented to this rule as the empty string, and the empty string
// must not be read as "exempt". Absence exempting a character would be an
// exemption nobody wrote on purpose, granted the moment a future caller forgot
// to look the name up first.
void AbsentFromTheJobMapStillPays()
{
    CheckApplies("no job on record", MaintenanceBudgetApplies(""), true);
}

}  // namespace

int main()
{
    CraftIsExempt();
    EveryOtherJobStillPays();
    AbsentFromTheJobMapStillPays();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the craft exemption from the errand budget holds\n");
    return EXIT_SUCCESS;
}
