/*
 * An off-mesh leader that stands still recovers within minutes, and the town
 * trip does not keep claiming it meanwhile (mod-overseer#1698).
 *
 * The measured loop: the walk never paths, the no-progress release fires a few
 * times, the aim is backed off, and the town trip takes the leader again and
 * re-arms the walk, until the one-hour bound finally lets the recovery rung
 * fire. Two rules end it:
 *
 *  1. A walker that stands within a few yards of one spot, with the streak
 *     armed, reaches the recovery after stillSeconds, not after the hour.
 *  2. While that recovery is pending the walker is not claimed for an errand
 *     (StallRecoveryHoldsErrand), and the hold is bounded.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
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

StallRecoveryFacts Off(std::int64_t now, bool rule = true)
{
    StallRecoveryFacts f;
    f.footing = StallFooting::OffMesh;
    f.now = now;
    f.x = -100.f;
    f.y = 200.f;
    f.nearestPolygonYards = 6.f;
    f.walkSafe = true;
    f.stillRule = rule;
    return f;
}

StallRecoveryLimits const LIM{};
constexpr float X = -100.f;
constexpr float Y = 200.f;

// The leader's releases a minute apart, then the town trip's polls every 30 s
// asking to claim; returns the seconds from the first release to the rung.
void ALeaderOffTheMeshRecoversInMinutes()
{
    StallRecoveryState s;
    std::int64_t const t0 = 1000;
    for (int i = 0; i < 3; ++i)
        Check("the first releases only back off",
              StallRecoveryStep(s, Off(t0 + i * 60), LIM).action == StallRecoveryAction::Backoff);

    std::int64_t recoveredAt = 0;
    int claims = 0;
    for (std::int64_t t = t0 + 120; t < t0 + LIM.seconds && !recoveredAt; t += 30)
    {
        if (StallRecoveryHoldsErrand(s, X, Y, t, LIM))
        {
            // The town trip is refused; the idle read runs at its interval.
            if (StallRecoveryIdleDue(s, X, Y, t, LIM, 60))
            {
                StallRecoveryVerdict const v = StallRecoveryStep(s, Off(t), LIM);
                if (v.action == StallRecoveryAction::WalkToMesh)
                    recoveredAt = t;
            }
        }
        else if (!s.escalated)
            ++claims;  // the town trip would have claimed the leader
    }
    Check("the recovery rung is reached", recoveredAt != 0);
    Check("within ten minutes, not the hour", recoveredAt != 0 && recoveredAt - t0 <= 600);
    Check("the town trip never claimed the leader before recovery", claims == 0);
    Check("once recovered the hold is released",
          !StallRecoveryHoldsErrand(s, X, Y, recoveredAt + 30, LIM));
}

void WithoutTheQuickRuleTheHourStands()
{
    StallRecoveryState s;
    for (int i = 0; i < 3; ++i)
        StallRecoveryStep(s, Off(1000 + i * 60, false), LIM);
    Check("no hold without the rule", !StallRecoveryHoldsErrand(s, X, Y, 1300, LIM));
    Check("no quick recovery without the rule",
          StallRecoveryStep(s, Off(1000 + 400, false), LIM).action == StallRecoveryAction::Backoff);
}

void AWalkerThatMovesIsNeitherHeldNorRecovered()
{
    StallRecoveryState s;
    // Drifting a few yards at every release: never still for the window.
    StallRecoveryVerdict v;
    for (int i = 0; i < 8; ++i)
    {
        StallRecoveryFacts f = Off(1000 + i * 120);
        f.x += static_cast<float>(i) * (LIM.stillYards + 1.f);
        v = StallRecoveryStep(s, f, LIM);
    }
    Check("a drifting walker does not take the quick rung", v.action == StallRecoveryAction::Backoff);
    Check("and is not held", !StallRecoveryHoldsErrand(s, X + 40.f, Y, 1000 + 7 * 120, LIM));

    // Moved past the reset distance: not held.
    StallRecoveryState m;
    for (int i = 0; i < 3; ++i)
        StallRecoveryStep(m, Off(1000 + i * 60), LIM);
    Check("held while it stays", StallRecoveryHoldsErrand(m, X, Y, 1200, LIM));
    Check("not held once it moved away",
          !StallRecoveryHoldsErrand(m, X + LIM.movedResetYards + 5.f, Y, 1200, LIM));
}

void TheHoldIsBoundedWhenNoRungCanBeTaken()
{
    StallRecoveryState s;
    for (int i = 0; i < 3; ++i)
        StallRecoveryStep(s, Off(1000 + i * 60), LIM);
    Check("held while armed", StallRecoveryHoldsErrand(s, X, Y, 1000 + 200, LIM));
    Check("dropped after the grace",
          !StallRecoveryHoldsErrand(s, X, Y, 1000 + LIM.stillSeconds + LIM.holdGraceSeconds + 200,
                                    LIM));
    // Standing on the mesh ends the streak and the hold.
    StallRecoveryFacts on = Off(1300);
    on.footing = StallFooting::OnMeshConnected;
    StallRecoveryStep(s, on, LIM);
    Check("on the mesh: not held", !StallRecoveryHoldsErrand(s, X, Y, 1310, LIM));
}
}  // namespace

int main()
{
    Check("the bound stays an hour", LIM.seconds == 3600);
    ALeaderOffTheMeshRecoversInMinutes();
    WithoutTheQuickRuleTheHourStands();
    AWalkerThatMovesIsNeitherHeldNorRecovered();
    TheHoldIsBoundedWhenNoRungCanBeTaken();
    if (failures == 0)
        std::printf("ok\n");
    return failures ? 1 : 0;
}
