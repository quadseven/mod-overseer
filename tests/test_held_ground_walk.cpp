/*
 * A far-held member on lethal ground with its hearthstone not ready walks to a
 * graveyard it can survive, and is held there (mod-overseer#744).
 *
 * On wow-dev 2026-10-18 21:47:49 two level 31 members were held on Desolace
 * ground that spawns level 34, their stones not ready. The verdict was a
 * silent Hold, because nothing ever told DecideHeldGround a safe spot was
 * known, and both died within 12 seconds `driven by idle`. These cases pin the
 * rule the adapter now feeds: walk only to a graveyard whose own ground is
 * below the #697 margin, and stop walking once there.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, HeldGroundFacts const& facts, HeldGroundStep want)
    {
        HeldGroundStep const got = DecideHeldGround(facts).step;
        if (got == want)
            return;
        std::printf("FAIL %s: got %u, wanted %u\n", name, unsigned(got), unsigned(want));
        ++failures;
    };
    auto expect = [&failures](char const* name, bool got, bool want)
    {
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, int(got), int(want));
        ++failures;
    };

    // The Desolace case: level 31 on level 34 ground, stone not ready.
    HeldGroundFacts facts;
    facts.memberLevel = 31;
    facts.groundTopLevel = 34;
    facts.levelGap = 3;
    facts.hearthReady = false;
    facts.boundOnMemberMap = true;
    expect("lethal ground, stone not ready, needs a safe spot",
           HeldGroundNeedsSafeSpot(facts), true);

    // A graveyard 240 yards off with nothing hostile at it: walk there.
    facts.safeSpotKnown = true;
    facts.safeSpotTopLevel = 0;
    facts.safeSpotYards = 240.0f;
    check("lethal ground, stone not ready, survivable graveyard far off", facts,
          HeldGroundStep::WalkToSafety);

    // A graveyard whose own spawns are as lethal: no walk, it is held.
    facts.safeSpotTopLevel = 36;
    check("the graveyard is lethal too", facts, HeldGroundStep::Hold);

    // Spawns just under the margin at the graveyard are survivable.
    facts.safeSpotTopLevel = 33;
    check("graveyard below the margin", facts, HeldGroundStep::WalkToSafety);

    // Arrived: within the arrival radius it is held where it stands.
    facts.safeSpotTopLevel = 0;
    facts.safeSpotYards = HELD_GROUND_ARRIVED_YARDS;
    check("already at the graveyard", facts, HeldGroundStep::Hold);

    // No graveyard known: held, as before.
    facts.safeSpotKnown = false;
    facts.safeSpotYards = 240.0f;
    check("no graveyard known", facts, HeldGroundStep::Hold);

    // A ready stone still wins over any walk.
    facts.safeSpotKnown = true;
    facts.hearthReady = true;
    check("ready stone hearths rather than walks", facts, HeldGroundStep::Hearth);
    expect("ready stone needs no safe spot", HeldGroundNeedsSafeSpot(facts), false);

    // Survivable ground and a city need no safe spot, and are held.
    facts.hearthReady = false;
    facts.groundTopLevel = 33;
    expect("ground below the margin needs no safe spot", HeldGroundNeedsSafeSpot(facts), false);
    check("ground below the margin holds", facts, HeldGroundStep::Hold);
    facts.groundTopLevel = 80;
    facts.resting = true;
    expect("a city needs no safe spot", HeldGroundNeedsSafeSpot(facts), false);
    check("a city holds", facts, HeldGroundStep::Hold);

    if (!failures)
        std::printf("held ground walk: all checks passed\n");
    return failures ? 1 : 0;
}
