/*
 * A walker whose every aim is refused as lethal takes the hearthstone rung
 * after a few refusals at one spot, instead of being re-aimed for ever
 * (mod-overseer#1706). Compiled against src/overseer_decisions.cpp.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

LethalRefusalFacts At(float x, bool stoneReady = true)
{
    LethalRefusalFacts f;
    f.x = x;
    f.y = 50.f;
    f.hasStone = true;
    f.stoneReady = stoneReady;
    f.sameMapBind = true;
    return f;
}

StallRecoveryLimits const LIM{};
}  // namespace

int main()
{
    {
        StallRecoveryState s;
        Check("1st counts", LethalRefusalStep(s, At(0.f), LIM).action == LethalRefusalAction::Count);
        Check("2nd counts", LethalRefusalStep(s, At(1.f), LIM).action == LethalRefusalAction::Count);
        LethalRefusalVerdict const v = LethalRefusalStep(s, At(2.f), LIM);
        Check("3rd hearths", v.action == LethalRefusalAction::Hearth);
        Check("count is 3", v.refusals == 3);
        LethalRefusalHearthAccepted(s);
        LethalRefusalVerdict const afterCast = LethalRefusalStep(s, At(2.f), LIM);
        Check("an accepted cast ends the streak",
              afterCast.action == LethalRefusalAction::Count && afterCast.refusals == 1);
    }
    {
        StallRecoveryState s;
        LethalRefusalStep(s, At(0.f), LIM);
        LethalRefusalStep(s, At(0.f), LIM);
        Check("third refusal asks to hearth",
              LethalRefusalStep(s, At(0.f), LIM).action == LethalRefusalAction::Hearth);
        // The cast is immediately refused, so no hearth started.
        LethalRefusalVerdict const retry = LethalRefusalStep(s, At(0.f), LIM);
        Check("the next refusal retries the blocked cast",
              retry.action == LethalRefusalAction::Hearth);
        Check("the retry keeps the refusal count", retry.refusals == 4);
    }
    {
        StallRecoveryState s;
        LethalRefusalStep(s, At(0.f), LIM);
        LethalRefusalStep(s, At(1.f), LIM);
        LethalRefusalVerdict const v = LethalRefusalStep(s, At(500.f), LIM);
        Check("moved walker starts afresh", v.action == LethalRefusalAction::Count && v.refusals == 1);
    }
    {
        StallRecoveryState s;
        LethalRefusalStep(s, At(0.f, false), LIM);
        LethalRefusalStep(s, At(0.f, false), LIM);
        Check("cooling stone: no rung",
              LethalRefusalStep(s, At(0.f, false), LIM).action == LethalRefusalAction::Keep);
        Check("streak kept", LethalRefusalStep(s, At(0.f, false), LIM).refusals == 4);
        Check("rung once the stone is ready",
              LethalRefusalStep(s, At(0.f, true), LIM).action == LethalRefusalAction::Hearth);
    }
    {
        StallRecoveryState s;
        LethalRefusalFacts f = At(0.f);
        f.sameMapBind = false;
        LethalRefusalStep(s, f, LIM);
        LethalRefusalStep(s, f, LIM);
        Check("other-map bind: no rung",
              LethalRefusalStep(s, f, LIM).action == LethalRefusalAction::Keep);
    }
    {
        StallRecoveryState s;
        LethalRefusalFacts f = At(0.f);
        f.hearthPending = true;
        LethalRefusalStep(s, f, LIM);
        LethalRefusalStep(s, f, LIM);
        Check("cast pending: no rung",
              LethalRefusalStep(s, f, LIM).action == LethalRefusalAction::Keep);
        f.hearthPending = false;
        Check("rung once the cast is done",
              LethalRefusalStep(s, f, LIM).action == LethalRefusalAction::Hearth);
    }
    {
        StallRecoveryState s;
        LethalRefusalFacts f = At(0.f);
        f.hasStone = false;
        LethalRefusalStep(s, f, LIM);
        LethalRefusalStep(s, f, LIM);
        Check("no stone: no rung",
              LethalRefusalStep(s, f, LIM).action == LethalRefusalAction::Keep);
    }
    if (failures == 0)
        std::printf("ok\n");
    return failures ? 1 : 0;
}
