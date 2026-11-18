/*
 * A flight leg that a portal has carried away from its flight master.
 *
 * Measured on the dev realm: a leader was sent to an errand 2006 yards away and
 * took the flight option, boarding at a flight master 575 yards off. A mage
 * portal then carried the whole party to the capital, 2300 yards from that
 * master. Upstream's travel flight status has no way out except a flight that
 * ends, and the module held the leg open for ten minutes, so the leader was
 * aimed back across the hills at the master it had been taken away from, on a
 * straight spline 2170 yards long. The airborne guard stopped it afterwards.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FlightLegStrayed;

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    // The measured case: 575 yards at issue, about 2300 after the portal.
    Check(FlightLegStrayed(2300.f, 575.f), "a portal 1700 yards past the issue distance ends the leg");

    // An ordinary leg only ever gets nearer.
    Check(!FlightLegStrayed(575.f, 575.f), "standing where it was issued is not a stray");
    Check(!FlightLegStrayed(300.f, 575.f), "walking toward the master is not a stray");
    Check(!FlightLegStrayed(0.f, 575.f), "arriving at the master is not a stray");

    // A walk round a hill can lengthen the straight-line distance a little.
    Check(!FlightLegStrayed(575.f + 200.f, 575.f), "a detour inside the slack is not a stray");
    Check(!FlightLegStrayed(575.f + OverseerDecisions::FLIGHT_LEG_STRAY_YARDS, 575.f),
          "exactly the slack is not yet a stray");
    Check(FlightLegStrayed(575.f + OverseerDecisions::FLIGHT_LEG_STRAY_YARDS + 1.f, 575.f),
          "one yard past the slack is a stray");

    // A character issued at the master itself and carried off is still caught.
    Check(FlightLegStrayed(900.f, 0.f), "issued at the master, then carried 900 yards away");

    // No issue distance recorded: never guess a stray.
    Check(!FlightLegStrayed(5000.f, -1.f), "an unknown issue distance never ends a leg");

    if (failures)
        return EXIT_FAILURE;
    std::printf("flight leg strayed: ok\n");
    return EXIT_SUCCESS;
}
