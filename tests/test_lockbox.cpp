#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::LockboxFacts;
using OverseerDecisions::LockboxStep;
using OverseerDecisions::LockboxNext;

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
}

int main()
{
    LockboxFacts qualified{true, true, true, true};
    Check("a qualified rogue uses the natural lockpick action",
          LockboxNext(qualified) == LockboxStep::UnlockAndOpen);

    LockboxFacts noSkill = qualified;
    noSkill.hasLockpickingSkill = false;
    Check("the module never grants a missing skill",
          LockboxNext(noSkill) == LockboxStep::Refuse);

    LockboxFacts noSpell = qualified;
    noSpell.hasPickLockSpell = false;
    Check("the module never substitutes another unlock mechanism",
          LockboxNext(noSpell) == LockboxStep::Refuse);

    return failures;
}
