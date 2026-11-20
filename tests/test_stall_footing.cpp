// What a stalled errand is standing on, and the one bounded recovery (#188).

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

StallFootingFacts Facts(float nearest, bool computed, bool noPath, bool shortcut, bool endOff)
{
    StallFootingFacts f;
    f.nearestPolygonYards = nearest;
    f.pathComputed = computed;
    f.pathNoPath = noPath;
    f.pathShortcut = shortcut;
    f.pathEndOffMesh = endOff;
    return f;
}

StallRecoveryFacts Off(std::int64_t now)
{
    StallRecoveryFacts f;
    f.footing = StallFooting::OffMesh;
    f.now = now;
    f.x = -100.f;
    f.y = 200.f;
    f.nearestPolygonYards = 6.f;
    f.walkSafe = true;
    f.hasStone = true;
    f.stoneReady = true;
    f.bindMap = 0;
    f.currentMap = 0;
    return f;
}

// Releases a minute apart, the way the live loop produces them, up to and
// including release number `n`; returns the verdict of the last one.
StallRecoveryVerdict Run(StallRecoveryState& s, StallRecoveryFacts base, unsigned n,
                         StallRecoveryLimits const& lim, std::int64_t start = 1000,
                         std::int64_t step = 120)
{
    StallRecoveryVerdict v;
    for (unsigned i = 0; i < n; ++i)
    {
        base.now = start + static_cast<std::int64_t>(i) * step;
        v = StallRecoveryStep(s, base, lim);
    }
    return v;
}
}  // namespace

int main()
{
    StallRecoveryLimits const lim;
    Check(lim.seconds >= 3600, "the bound is at least an hour");

    // CLASSIFICATION: the three readings.
    Check(ClassifyStallFooting(Facts(0.4f, true, false, false, false), lim.feetOnMeshYards) ==
              StallFooting::OnMeshConnected,
          "feet on the mesh with a real route read as connected");
    Check(ClassifyStallFooting(Facts(0.4f, true, true, false, false), lim.feetOnMeshYards) ==
              StallFooting::OnMeshDisconnected,
          "feet on the mesh and NOPATH read as disconnected");
    Check(ClassifyStallFooting(Facts(0.4f, true, false, false, true), lim.feetOnMeshYards) ==
              StallFooting::OnMeshDisconnected,
          "feet on the mesh and a path ending off it read as disconnected");
    Check(ClassifyStallFooting(Facts(0.4f, false, false, false, false), lim.feetOnMeshYards) ==
              StallFooting::OnMeshDisconnected,
          "feet on the mesh and no path computed read as disconnected");
    Check(ClassifyStallFooting(Facts(-1.f, true, false, false, false), lim.feetOnMeshYards) ==
              StallFooting::OffMesh,
          "no polygon found reads as off the mesh, whatever the path says");
    Check(ClassifyStallFooting(Facts(9.f, true, false, true, false), lim.feetOnMeshYards) ==
              StallFooting::OffMesh,
          "a polygon beyond the feet bound reads as off the mesh");
    Check(ClassifyStallFooting(Facts(lim.feetOnMeshYards, true, false, false, false),
                               lim.feetOnMeshYards) == StallFooting::OnMeshConnected,
          "a polygon exactly at the feet bound is on the mesh");

    // EVERY REFUSED RELEASE FOR LONGER THAN THE BOUND, AND NO RECOVERY TRIED:
    // no escalation before the bound, in releases or in seconds.
    {
        StallRecoveryState s;
        StallRecoveryVerdict v = Run(s, Off(0), lim.releases - 1, lim);
        Check(v.action == StallRecoveryAction::Backoff, "no escalation before the release count");
        Check(v.releases == lim.releases - 1, "the streak counts its releases");
        // Plenty of releases, too fast to span the hour: still none.
        StallRecoveryState quick;
        v = Run(quick, Off(0), lim.releases + 20, lim, 1000, 10);
        Check(v.action == StallRecoveryAction::Backoff,
              "releases faster than the hour do not escalate");
        // Enough seconds, too few releases: still none.
        StallRecoveryState slow;
        v = Run(slow, Off(0), 5, lim, 1000, 3600);
        Check(v.action == StallRecoveryAction::Backoff, "few releases do not escalate");
    }

    // ESCALATION FIRES ONCE AT THE BOUND AND NOT AGAIN: the walk when its point
    // is near and safe.
    {
        StallRecoveryState s;
        StallRecoveryVerdict v = Run(s, Off(0), lim.releases, lim, 1000, 130);
        Check(v.action == StallRecoveryAction::WalkToMesh, "escalates to the walk at the bound");
        v = Run(s, Off(0), 1, lim, 1000 + 40 * 130, 130);
        Check(v.action == StallRecoveryAction::Backoff, "does not escalate a second time");
        v = Run(s, Off(0), 3, lim, 1000 + 41 * 130, 130);
        Check(v.action == StallRecoveryAction::Backoff, "still plain backoff afterwards");
    }

    // A WALK THAT IS NOT SAFE, OR A POLYGON TOO FAR, GOES TO THE HEARTHSTONE.
    {
        StallRecoveryState s;
        StallRecoveryFacts f = Off(0);
        f.walkSafe = false;
        StallRecoveryVerdict v = Run(s, f, lim.releases, lim, 1000, 130);
        Check(v.action == StallRecoveryAction::Hearth, "an unsafe walk falls to the hearthstone");
        StallRecoveryState far;
        f = Off(0);
        f.nearestPolygonYards = lim.nearestMeshYards + 1.f;
        v = Run(far, f, lim.releases, lim, 1000, 130);
        Check(v.action == StallRecoveryAction::Hearth, "a far polygon falls to the hearthstone");
        StallRecoveryState none;
        f = Off(0);
        f.nearestPolygonYards = -1.f;
        v = Run(none, f, lim.releases, lim, 1000, 130);
        Check(v.action == StallRecoveryAction::Hearth, "no polygon at all falls to the hearthstone");
    }

    // A RECALL REFUSED BY ITS OWN RULES FALLS BACK TO THE PLAIN BACKOFF, and
    // says why at most once an hour.
    {
        struct Case
        {
            char const* name;
            bool hasStone;
            bool stoneReady;
            std::uint32_t bindMap;
            bool pending;
            char const* mention;
        };
        Case const cases[] = {
            {"no stone", false, true, 0, false, "no hearthstone"},
            {"on cooldown", true, false, 0, false, "cooldown"},
            {"bound on another map", true, true, 1, false, "map 1"},
            {"a cast already out", true, true, 0, true, "already"},
        };
        for (Case const& c : cases)
        {
            StallRecoveryState s;
            StallRecoveryFacts f = Off(0);
            f.walkSafe = false;
            f.hasStone = c.hasStone;
            f.stoneReady = c.stoneReady;
            f.bindMap = c.bindMap;
            f.hearthPending = c.pending;
            StallRecoveryVerdict v = Run(s, f, lim.releases, lim, 1000, 130);
            Check(v.action == StallRecoveryAction::SayNoRecovery, c.name);
            Check(v.reason.find(c.mention) != std::string::npos, c.name);
            // Not spent: asked again, said again only after the say interval.
            v = Run(s, f, 1, lim, 1000 + 40 * 130, 130);
            Check(v.action == StallRecoveryAction::Backoff, "refusal is not repeated at once");
            v = Run(s, f, 1, lim, 1000 + 40 * 130 + lim.saySeconds, 130);
            Check(v.action == StallRecoveryAction::SayNoRecovery, "refusal is said again later");
        }
    }

    // THE DISCONNECTED CASE NEVER RECOVERS, however long it lasts, and is said
    // at most once an hour.
    {
        StallRecoveryState s;
        StallRecoveryFacts f = Off(0);
        f.footing = StallFooting::OnMeshDisconnected;
        unsigned said = 0;
        for (unsigned i = 0; i < 200; ++i)
        {
            f.now = 1000 + static_cast<std::int64_t>(i) * 130;
            StallRecoveryVerdict const v = StallRecoveryStep(s, f, lim);
            Check(v.action != StallRecoveryAction::WalkToMesh &&
                      v.action != StallRecoveryAction::Hearth,
                  "disconnected never walks or hearths");
            if (v.action == StallRecoveryAction::SayUnreachable)
            {
                ++said;
                Check(v.reason.find("person must decide") != std::string::npos,
                      "the unreachable line says a person must decide");
            }
        }
        // 200 releases * 130 s = 26000 s: at most one say per 3600 s.
        Check(said >= 1 && said <= 8, "unreachable is said, and at most once an hour");
    }

    // A connected reading says nothing and recovers nothing; and a streak that
    // is broken by a non-off-mesh reading starts over.
    {
        StallRecoveryState s;
        StallRecoveryFacts f = Off(0);
        Run(s, f, lim.releases - 1, lim, 1000, 130);
        f.footing = StallFooting::OnMeshConnected;
        f.now = 1000 + 100 * 130;
        Check(StallRecoveryStep(s, f, lim).action == StallRecoveryAction::Backoff,
              "connected says nothing");
        f.footing = StallFooting::OffMesh;
        f.now += 130;
        Check(StallRecoveryStep(s, f, lim).action == StallRecoveryAction::Backoff,
              "a broken streak starts over and does not escalate");
    }

    // A character that has moved away is no longer stuck there: the streak
    // starts over, and a spent escalation is available again.
    {
        StallRecoveryState s;
        StallRecoveryFacts f = Off(0);
        Check(Run(s, f, lim.releases, lim, 1000, 130).action == StallRecoveryAction::WalkToMesh,
              "first streak escalates");
        f.x += lim.movedResetYards + 5.f;
        f.now = 1000 + 100 * 130;
        Check(StallRecoveryStep(s, f, lim).action == StallRecoveryAction::Backoff,
              "after it moved the streak restarts");
        Check(s.offMeshReleases == 1 && !s.escalated, "and the escalation is available again");
    }

    // A hearth the adapter could not start is not spent.
    {
        StallRecoveryState s;
        StallRecoveryFacts f = Off(0);
        f.walkSafe = false;
        Check(Run(s, f, lim.releases, lim, 1000, 130).action == StallRecoveryAction::Hearth,
              "hearth chosen");
        StallRecoveryNotDone(s);
        f.now = 1000 + 40 * 130;
        Check(StallRecoveryStep(s, f, lim).action == StallRecoveryAction::Hearth,
              "an escalation that was not done is asked again");
    }

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
