#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::DecideLockpickErrand;
using OverseerDecisions::IsLockpickTargetEntry;
using OverseerDecisions::LockpickErrandStep;
using OverseerDecisions::ShouldClearLockpickErrand;

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

char const* StepName(LockpickErrandStep step)
{
    switch (step)
    {
        case LockpickErrandStep::Stand: return "Stand";
        case LockpickErrandStep::Cast:  return "Cast";
    }
    return "?";
}

void Expect(char const* what, LockpickErrandStep got, LockpickErrandStep want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected %s, got %s\n", what, StepName(want), StepName(got));
}

}  // namespace

int main()
{
    // Target predicate: matching entry (the two pinned live lockbox entries).
    Check("the Ratchet strongbox entry is a lockpick target",
          IsLockpickTargetEntry(1660u));
    Check("the Redridge practice lockbox entry is a lockpick target",
          IsLockpickTargetEntry(1733u));
    Check("entry zero is not a lockpick target",
          !IsLockpickTargetEntry(0u));
    Check("an arbitrary non-lockbox entry is not a lockpick target",
          !IsLockpickTargetEntry(9999u));

    // Level-cap clear: max achievable skill at a level is 5 * level.
    Check("skill exactly at the cap clears the errand",
          ShouldClearLockpickErrand(5u * 10u, 10u));
    Check("skill above the cap clears the errand",
          ShouldClearLockpickErrand(5u * 10u + 1u, 10u));
    Check("skill just below the cap does not clear the errand",
          !ShouldClearLockpickErrand(5u * 10u - 1u, 10u));
    Check("level one cap is five, skill four does not clear",
          !ShouldClearLockpickErrand(4u, 1u));
    Check("level one cap is five, skill five clears",
          ShouldClearLockpickErrand(5u, 1u));

    // Errand-standing discipline: every transient refusal leaves it standing.
    // The decision only sees the boolean; that a null target from
    // FindNearestLockpickTarget becomes hasTarget=false and reaches this
    // decision is pinned in test_drive_lockpick_source.cpp, along with the
    // "cast but no skill-up" refusal, which is judged after the cast.
    Expect("no target in zone leaves the errand standing",
           DecideLockpickErrand(false, false, false, false),
           LockpickErrandStep::Stand);
    Expect("target present but out of reach leaves the errand standing",
           DecideLockpickErrand(true, false, false, false),
           LockpickErrandStep::Stand);
    Expect("in combat leaves the errand standing",
           DecideLockpickErrand(true, true, true, false),
           LockpickErrandStep::Stand);
    Expect("moving leaves the errand standing",
           DecideLockpickErrand(true, true, false, true),
           LockpickErrandStep::Stand);
    Expect("in reach and still casts Pick Lock",
           DecideLockpickErrand(true, true, false, false),
           LockpickErrandStep::Cast);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::printf("test_drive_lockpick: all cases passed\n");
    return EXIT_SUCCESS;
}
