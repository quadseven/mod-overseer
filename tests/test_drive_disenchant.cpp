#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::DisenchantErrandStep;
using OverseerDecisions::DecideDisenchantErrand;
using OverseerDecisions::IsDisenchantTargetEntry;
using OverseerDecisions::ShouldClearDisenchantErrand;

namespace
{
int failures = 0;

void Check(char const* what, bool value)
{
    if (!value)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

char const* StepName(DisenchantErrandStep step)
{
    switch (step)
    {
        case DisenchantErrandStep::Stand: return "Stand";
        case DisenchantErrandStep::Cast:  return "Cast";
    }
    return "?";
}

void Expect(char const* what, DisenchantErrandStep got, DisenchantErrandStep want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected %s, got %s\n", what, StepName(want), StepName(got));
}

} // namespace

int main()
{
    // Fixed green-item entries: the three pinned live entries.
    Check("a pinned green entry is a disenchant target",
          IsDisenchantTargetEntry(2307u));
    Check("another pinned green entry is a disenchant target",
          IsDisenchantTargetEntry(2308u));
    Check("a third pinned green entry is a disenchant target",
          IsDisenchantTargetEntry(2309u));
    Check("entry zero is not a disenchant target",
          !IsDisenchantTargetEntry(0u));
    Check("an arbitrary non-green entry is not a disenchant target",
          !IsDisenchantTargetEntry(9999u));

    // Reagent read-back: the errand clears only when the dust count rose.
    Check("dust count rising clears the errand",
          ShouldClearDisenchantErrand(0u, 1u));
    Check("dust count rising from one clears the errand",
          ShouldClearDisenchantErrand(1u, 2u));
    Check("dust count unchanged does not clear the errand",
          !ShouldClearDisenchantErrand(3u, 3u));
    Check("dust count falling does not clear the errand",
          !ShouldClearDisenchantErrand(5u, 4u));

    // Errand-standing discipline: every transient refusal leaves it standing.
    Expect("no item leaves the errand standing",
           DecideDisenchantErrand(false, false, false),
           DisenchantErrandStep::Stand);
    Expect("in combat leaves the errand standing",
           DecideDisenchantErrand(true, true, false),
           DisenchantErrandStep::Stand);
    Expect("moving leaves the errand standing",
           DecideDisenchantErrand(true, false, true),
           DisenchantErrandStep::Stand);
    Expect("item present, still, not fighting casts Disenchant",
           DecideDisenchantErrand(true, false, false),
           DisenchantErrandStep::Cast);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::printf("test_drive_disenchant: all cases passed\n");
    return EXIT_SUCCESS;
}
