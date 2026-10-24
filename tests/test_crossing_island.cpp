/*
 * A member crossing out of Teldrassil takes the Rut'theran boat first
 * (wow-overseer#1401).
 *
 * Measured on wow-dev 2026-10-24: 99 of the guild's 117 self deaths in a day
 * were below the water line off Teldrassil's west coast. A `cross-to-map` row
 * for a member on the island was priced against the Menethil to Auberdine boat,
 * whose berth lies across the sea, and the walk to it drowned the member about
 * once a minute. The island's only road off is the Rut'theran to Auberdine boat.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

namespace D = OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
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
    D::CrossRequest const toEast = D::ParseCrossRequest("cross-to-map map:0");
    Check("the request parses", !*toEast.error && toEast.map == 0 && !toEast.ride);

    D::IslandFirstLeg const shadowglen = D::CrossIslandFirst(1, D::TELDRASSIL_ZONE, toEast);
    Check("a member in Teldrassil rides the island boat first", shadowglen.first);
    Check("the first leg is a ride on map 1", shadowglen.leg.ride && shadowglen.leg.map == 1);
    Check("the first leg ends at the Auberdine stop",
          shadowglen.leg.x == D::AUBERDINE_STOP_X && shadowglen.leg.y == D::AUBERDINE_STOP_Y);

    Check("a member in Darnassus rides the island boat first",
          D::CrossIslandFirst(1, D::DARNASSUS_ZONE, toEast).first);
    Check("a member in Darkshore crosses as asked",
          !D::CrossIslandFirst(1, 148, toEast).first);
    Check("zone 141 on another map is not the island",
          !D::CrossIslandFirst(0, D::TELDRASSIL_ZONE, toEast).first);
    Check("an unchanged request comes back unchanged off the island",
          D::CrossIslandFirst(1, 148, toEast).leg.map == 0 &&
              !D::CrossIslandFirst(1, 148, toEast).leg.ride);

    D::CrossRequest const ride = D::ParseCrossRequest("ride-to-stop map:1 x:6341.4 y:557.7");
    Check("a ride row is left alone", ride.ride && !D::CrossIslandFirst(1, D::TELDRASSIL_ZONE, ride).first);

    D::CrossRequest const sameMap = D::ParseCrossRequest("cross-to-map map:1");
    Check("a row for the island's own map is left alone",
          !D::CrossIslandFirst(1, D::TELDRASSIL_ZONE, sameMap).first);

    D::CrossRequest const bad = D::ParseCrossRequest("cross-to-map");
    Check("a refused request is left alone",
          *bad.error && !D::CrossIslandFirst(1, D::TELDRASSIL_ZONE, bad).first);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
