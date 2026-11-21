// A head standing off the mesh with no walked errand still reaches the bounded
// recovery. The step only runs on a no-progress release; once the aim is backed
// off or held, no release comes, so an armed streak must be reachable without
// one (StallRecoveryIdleDue).

#include "overseer_decisions.h"

#include <cstdio>

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

StallRecoveryFacts Off(std::int64_t now)
{
    StallRecoveryFacts f;
    f.footing = StallFooting::OffMesh;
    f.now = now;
    f.x = 10.f;
    f.y = 20.f;
    f.nearestPolygonYards = 5.f;
    f.walkSafe = true;
    return f;
}
}  // namespace

int main()
{
    StallRecoveryLimits const lim;
    StallRecoveryState s;

    Check(!StallRecoveryIdleDue(s, 10.f, 20.f, 1000, lim, 60), "no streak: not due");

    // Releases at the start, then the errand is backed off and none follow.
    StallRecoveryStep(s, Off(1000), lim);
    StallRecoveryStep(s, Off(1060), lim);
    StallRecoveryStep(s, Off(1120), lim);
    Check(!StallRecoveryIdleDue(s, 10.f, 20.f, 1200, lim, 60), "armed releases but under the hour: not due");
    Check(StallRecoveryIdleDue(s, 10.f, 20.f, 1000 + lim.seconds, lim, 60),
          "an hour at the spot with no further release: due");
    Check(!StallRecoveryIdleDue(s, 10.f, 20.f, 1000 + lim.seconds + 10, lim, 60),
          "asked again inside the interval: not due");

    // The idle reading runs the step like a release and reaches the rung.
    StallRecoveryVerdict const v = StallRecoveryStep(s, Off(1000 + lim.seconds + 70), lim);
    Check(v.action == StallRecoveryAction::WalkToMesh, "the idle reading reaches rung 1");
    Check(!StallRecoveryIdleDue(s, 10.f, 20.f, 1000 + lim.seconds + 200, lim, 60),
          "escalated streak is not due again");

    // Moved away: not stuck there any more.
    StallRecoveryState m;
    for (int i = 0; i < 3; ++i)
        StallRecoveryStep(m, Off(1000 + i * 60), lim);
    Check(!StallRecoveryIdleDue(m, 500.f, 500.f, 1000 + lim.seconds, lim, 60), "moved: not due");

    // Fewer releases than the minimum never fire on time alone.
    StallRecoveryState few;
    StallRecoveryStep(few, Off(1000), lim);
    Check(!StallRecoveryIdleDue(few, 10.f, 20.f, 1000 + lim.seconds * 2, lim, 60),
          "too few releases: not due");

    if (failures == 0)
        std::printf("ok\n");
    return failures ? 1 : 0;
}
