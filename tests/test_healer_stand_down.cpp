/*
 * The dungeon resurrector gets its whole window (mod-overseer#1026).
 *
 * Inside a run with a living resurrector on the map, the stuck-revival drive
 * stands down for STUCK_REVIVAL_HEALER_SECONDS from the death. #753 ended that
 * wait on the SECOND poll: the first poll stamped a "recently used spirit
 * healer" mark and the next one, a second later, read it and gave up. So the
 * polls are replayed second by second here, the way the drive asks.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
// STUCK_REVIVAL_HEALER_SECONDS in mod_overseer.cpp: the dungeon module's 90 s
// post-combat rez budget plus a 30 s margin.
constexpr int64_t WINDOW = 90 + 30;
}  // namespace

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, bool got, bool want)
    {
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, int(got), int(want));
        ++failures;
    };

    HealerStandDownFacts facts;
    facts.inRun = true;
    facts.livingResurrector = true;
    facts.windowSeconds = WINDOW;

    // The #1026 failure: dead 1 s, then 2 s, still waiting for the resurrector.
    facts.deadSeconds = 1;
    check("dead 1 s in a run with a living resurrector waits", HealerStandsDown(facts), true);
    facts.deadSeconds = 2;
    check("dead 2 s in a run with a living resurrector still waits", HealerStandsDown(facts),
          true);

    // Every poll of the window waits, in order, the way the drive asks.
    for (int64_t s = 0; s < WINDOW; ++s)
    {
        facts.deadSeconds = s;
        if (!HealerStandsDown(facts))
        {
            std::printf("FAIL the wait ended early at %lld s of %lld\n", (long long)s,
                        (long long)WINDOW);
            ++failures;
            break;
        }
    }

    facts.deadSeconds = WINDOW;
    check("the wait ends when the window passes", HealerStandsDown(facts), false);
    facts.deadSeconds = WINDOW + 60;
    check("and stays ended after it", HealerStandsDown(facts), false);

    facts.deadSeconds = 5;
    facts.livingResurrector = false;
    check("no living resurrector left: no wait", HealerStandsDown(facts), false);
    facts.livingResurrector = true;
    facts.inRun = false;
    check("no run open for the map: no wait", HealerStandsDown(facts), false);

    if (failures)
    {
        std::printf("healer stand-down: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("healer stand-down: all checks passed\n");
    return 0;
}
