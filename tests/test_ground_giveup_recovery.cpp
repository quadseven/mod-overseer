// A ground give-up (every bearing refused) is read like a no-progress release.
//
// Measured on a realm: a family head stood on one spot for a day and a half.
// Each cycle its errand to a point a hundred yards off was refused every
// bearing, released as unreachable, and let go of for a quarter of an hour
// before the same point came back. That path never read what the character
// stands on, and the bounded recovery counts thirty releases, which at one
// give-up per quarter hour is seven and a half hours of failing before the
// hour bound could even matter. The recovery has to be reachable at that
// cadence.

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
    StallRecoveryLimits const plain;
    StallRecoveryLimits const ground = GroundGiveUpLimits(plain);

    Check(ground.releases < plain.releases, "a ground give-up needs fewer releases");
    Check(ground.seconds == plain.seconds, "and still a whole hour");
    Check(ground.feetOnMeshYards == plain.feetOnMeshYards &&
              ground.nearestMeshYards == plain.nearestMeshYards,
          "the thresholds are the same ones");

    // Give-ups at the landing ceiling's cadence: the fifth is an hour in.
    Check(Nth(plain, 5) == StallRecoveryAction::Backoff,
          "the no-progress count does not escalate at five give-ups");
    Check(Nth(plain, 9) == StallRecoveryAction::Backoff,
          "nor at nine, which is two hours at this cadence");
    Check(Nth(ground, 4) == StallRecoveryAction::Backoff,
          "ground limits wait out the hour: the fourth is 45 minutes in");
    Check(Nth(ground, 5) == StallRecoveryAction::WalkToMesh,
          "ground limits escalate on the give-up an hour in");

    // Never loosens a limit set tighter than the default.
    StallRecoveryLimits tight;
    tight.releases = 2;
    Check(GroundGiveUpLimits(tight).releases == 2, "a tighter count is kept");

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
