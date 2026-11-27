// A ground give-up (every bearing refused) is read like a no-progress release.
//
// The escalation depends on time off the mesh at the same spot, not on a
// release count that depends on poll cadence. A streak releasing every quarter
// of an hour reaches it at the hour.

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

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
    f.nearestPolygonYards = 6.f;
    f.walkSafe = true;
    return f;
}

// Give-ups a quarter of an hour apart; the action of release number `n`.
StallRecoveryAction Nth(StallRecoveryLimits const& lim, unsigned n)
{
    StallRecoveryState s;
    StallRecoveryAction last = StallRecoveryAction::Backoff;
    for (unsigned i = 0; i < n; ++i)
        last = StallRecoveryStep(s, Off(1000 + static_cast<std::int64_t>(i) * 900), lim).action;
    return last;
}
}  // namespace

int main()
{
    StallRecoveryLimits const lim;

    Check(lim.seconds == 3600, "the bound is a whole hour");
    Check(lim.releases >= 2 && lim.releases <= 3, "a small minimum number of releases");

    // A streak at a quarter-hour cadence escalates at the hour, not before.
    Check(Nth(lim, 4) == StallRecoveryAction::Backoff,
          "the fourth release is 45 minutes in and does not escalate");
    Check(Nth(lim, 5) == StallRecoveryAction::WalkToMesh,
          "the release an hour in escalates at a quarter-hour cadence");

    // A character that moved further than the reset distance starts over.
    {
        StallRecoveryState s;
        for (unsigned i = 0; i < 4; ++i)
            StallRecoveryStep(s, Off(1000 + static_cast<std::int64_t>(i) * 900), lim);
        StallRecoveryFacts moved = Off(1000 + 4 * 900);
        moved.x += lim.movedResetYards + 5.f;
        Check(StallRecoveryStep(s, moved, lim).action == StallRecoveryAction::Backoff,
              "after it moved the hour is not reached");
        Check(s.offMeshReleases == 1, "and the streak starts over");
    }

    // Fewer than the minimum releases never escalates, however long they span.
    {
        StallRecoveryState s;
        StallRecoveryAction last = StallRecoveryAction::Backoff;
        for (unsigned i = 0; i + 1 < lim.releases; ++i)
            last = StallRecoveryStep(s, Off(1000 + static_cast<std::int64_t>(i) * 36000), lim).action;
        Check(last == StallRecoveryAction::Backoff, "too few releases never escalate");
    }

    // A member released again and again at one spot is left behind by an
    // errand well before the hour, and only while the streak is fresh.
    {
        StallRecoveryState s;
        for (unsigned i = 0; i < lim.leaveBehindReleases; ++i)
        {
            Check(!StallRecoveryLeaveBehind(s, 1000 + static_cast<std::int64_t>(i) * 30, lim),
                  "fewer than the bound is still waited for");
            StallRecoveryStep(s, Off(1000 + static_cast<std::int64_t>(i) * 30), lim);
        }
        std::int64_t const last = 1000 + (lim.leaveBehindReleases - 1) * 30;
        Check(last - 1000 < lim.seconds, "the hour has not elapsed");
        Check(StallRecoveryLeaveBehind(s, last, lim), "the bound of releases leaves it behind");
        Check(!StallRecoveryLeaveBehind(s, last + lim.freshSeconds + 1, lim),
              "a streak nobody released lately is history");
        StallRecoveryFacts moved = Off(last + 30);
        moved.x += lim.movedResetYards + 5.f;
        StallRecoveryStep(s, moved, lim);
        Check(!StallRecoveryLeaveBehind(s, last + 30, lim), "moving away starts the streak over");
    }

    // The adapter has to ask: DriveTravel's give-up branch reads the footing.
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL: cannot read src/mod_overseer.cpp\n");
        return 1;
    }
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t const give = text.find("if (refusal.giveUp)");
    std::size_t const stop = text.find("StopRefusedWalk(bot, botAI);", give);
    Check(give != std::string::npos && stop != std::string::npos,
          "the ground give-up branch is findable");
    std::string const branch = text.substr(give, stop - give);
    Check(branch.find("ReadStalledFooting(bot, name, pos, true)") != std::string::npos,
          "the ground give-up reads the footing and may recover");

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
