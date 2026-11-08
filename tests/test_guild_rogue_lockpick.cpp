/*
 * Which rogues practice Lockpicking, and what a natural guild rogue does on a
 * poll (mod-overseer#1440).
 *
 * The standing errand was a roster row with `lockpick` set, and nothing sets it
 * for a guild member, so the natural guild's rogues sat at Lockpicking 1 at
 * levels 35 to 53 while random-bot rogues sit at their cap. These cases pin the
 * decision that now lets a natural guild rogue practice, and the cap check that
 * must read the current skill, not the maximum.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::DecideNaturalLockpick;
using OverseerDecisions::LockpickBracketOpen;
using OverseerDecisions::LockpickErrandApplies;
using OverseerDecisions::NaturalLockpickStep;

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

char const* StepName(NaturalLockpickStep step)
{
    switch (step)
    {
        case NaturalLockpickStep::Idle:       return "Idle";
        case NaturalLockpickStep::Cast:       return "Cast";
        case NaturalLockpickStep::GoToTarget: return "GoToTarget";
        case NaturalLockpickStep::GoToSpot:   return "GoToSpot";
    }
    return "?";
}

void Expect(char const* what, NaturalLockpickStep got, NaturalLockpickStep want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected %s, got %s\n", what, StepName(want), StepName(got));
}
}  // namespace

int main()
{
    // The report's own case: a level 49 natural guild rogue at Lockpicking 1.
    Check("a natural guild rogue below its cap gets the lockpick errand",
          LockpickErrandApplies(false, true, true, true, 1u, 49u));
    Check("a roster rogue with a standing errand below its cap still gets it",
          LockpickErrandApplies(true, false, true, true, 1u, 49u));
    Check("a rogue that is neither on the roster errand nor a natural guild member does not",
          !LockpickErrandApplies(false, false, true, true, 1u, 49u));
    Check("a natural guild member that is not a rogue does not",
          !LockpickErrandApplies(false, true, false, true, 1u, 49u));
    Check("a rogue that does not know Pick Lock does not",
          !LockpickErrandApplies(false, true, true, false, 1u, 49u));
    Check("a natural guild rogue at its cap does not",
          !LockpickErrandApplies(false, true, true, true, 5u * 49u, 49u));
    Check("a natural guild rogue one below its cap still does",
          LockpickErrandApplies(false, true, true, true, 5u * 49u - 1u, 49u));

    // The bracket the practice boxes teach.
    Check("Lockpicking 1 is inside the box bracket", LockpickBracketOpen(1u));
    Check("Lockpicking 99 is inside the box bracket", LockpickBracketOpen(99u));
    Check("Lockpicking 100 is past the box bracket", !LockpickBracketOpen(100u));

    // One poll of a natural guild rogue: (bracketOpen, hasTarget, inReach,
    // inCombat, moving, hasSpotOnMap).
    Expect("a live box in reach and a still rogue casts",
           DecideNaturalLockpick(true, true, true, false, false, true), NaturalLockpickStep::Cast);
    Expect("a live box in reach while it is still moving waits",
           DecideNaturalLockpick(true, true, true, false, true, true), NaturalLockpickStep::Idle);
    Expect("a live box in its zone but out of reach aims it at the box",
           DecideNaturalLockpick(true, true, false, false, false, true),
           NaturalLockpickStep::GoToTarget);
    Expect("no live box in its zone aims it at the nearest known spot on its map",
           DecideNaturalLockpick(true, false, false, false, false, true),
           NaturalLockpickStep::GoToSpot);
    Expect("no live box and no spot on its map does nothing",
           DecideNaturalLockpick(true, false, false, false, false, false),
           NaturalLockpickStep::Idle);
    Expect("in combat does nothing, even beside a box",
           DecideNaturalLockpick(true, true, true, true, false, true), NaturalLockpickStep::Idle);
    Expect("in combat does not walk either",
           DecideNaturalLockpick(true, false, false, true, false, true), NaturalLockpickStep::Idle);
    Expect("past the box bracket does nothing, even beside a box",
           DecideNaturalLockpick(false, true, true, false, false, true), NaturalLockpickStep::Idle);
    Expect("past the box bracket does not walk to a spot",
           DecideNaturalLockpick(false, false, false, false, false, true), NaturalLockpickStep::Idle);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_guild_rogue_lockpick: all cases passed\n");
    return EXIT_SUCCESS;
}
