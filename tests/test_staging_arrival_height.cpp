/*
 * Arriving at an aimed place means being there across and up, and a run adopts
 * its own staging aim after a restart (#596).
 *
 * Measured on the dev realm on 2026-09-23: the Horde leader "reached" the
 * Ragefire staging point in Orgrimmar's Cleft of Shadow while standing on the
 * level above it, 71 yards up. The errand was released, re-armed and released
 * again every fifty seconds until GATHERING refused the party for being above
 * the point. And after a worldserver restart the run deferred staging to an
 * outstanding travel errand that was its own staging aim, and waited on itself.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::DungeonRunMayClaimTravel;
using OverseerDecisions::ParsePlaceAim;
using OverseerDecisions::PlaceAimArrived;
using OverseerDecisions::TravelErrandIsTheRunsOwnAim;

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

// The module's own numbers: TRAVEL_ARRIVED_POSITION_YARDS and
// TRAVEL_ARRIVED_VERTICAL_YARDS.
constexpr float Across = 5.f;
constexpr float Up = 10.f;

std::string const Staging = "at:1:1807.39,-4407.8,-18.4334";

void ArrivalAsksHeightToo()
{
    Check("the measured case: in the plane but 71 yards above is not arrived",
          !PlaceAimArrived(3.f, 71.f, Across, Up));
    Check("and 71 yards below is not arrived either",
          !PlaceAimArrived(3.f, -71.f, Across, Up));
    Check("standing on the point is arrived", PlaceAimArrived(3.f, 0.4f, Across, Up));
    Check("a stride of slope is still the same place",
          PlaceAimArrived(4.9f, -9.5f, Across, Up));
    Check("out of reach across is not arrived whatever the height",
          !PlaceAimArrived(6.f, 0.f, Across, Up));
    Check("a door aim asks the plane alone, as before",
          PlaceAimArrived(3.f, 20.f, Across, 0.f));
}

void APlaceAimReadsBack()
{
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    Check("the staging aim parses", ParsePlaceAim(Staging, map, x, y, z));
    Check("its map is Kalimdor", map == 1);
    Check("its x", x > 1807.38f && x < 1807.40f);
    Check("its y", y > -4407.81f && y < -4407.79f);
    Check("its z", z > -18.44f && z < -18.43f);

    Check("a creature aim is not a place", !ParsePlaceAim("vendor", map, x, y, z));
    Check("a trigger aim is not a place", !ParsePlaceAim("trigger:2230", map, x, y, z));
    Check("a missing coordinate is refused", !ParsePlaceAim("at:1:1807.39,-4407.8", map, x, y, z));
    Check("trailing text is refused", !ParsePlaceAim(Staging + "x", map, x, y, z));
    Check("a bad separator is refused",
          !ParsePlaceAim("at:1:1807.39;-4407.8,-18.4", map, x, y, z));
    Check("empty is refused", !ParsePlaceAim("", map, x, y, z));
}

void TheRunKnowsItsOwnAim()
{
    std::vector<std::string> const own{Staging};
    Check("the column holding the staging aim is the run's own",
          TravelErrandIsTheRunsOwnAim(Staging, own, Across, Up));
    Check("the same point re-derived after a restart is still the run's own",
          TravelErrandIsTheRunsOwnAim("at:1:1807.4,-4407.8,-18.43", own, Across, Up));
    Check("an approach aim in the list is the run's own too",
          TravelErrandIsTheRunsOwnAim("at:1:1790,-4390,-17",
                                      {Staging, "at:1:1790,-4390,-17"}, Across, Up));
    Check("a vendor errand is not the run's",
          !TravelErrandIsTheRunsOwnAim("vendor", own, Across, Up));
    Check("another place is not the run's",
          !TravelErrandIsTheRunsOwnAim("at:1:1600,-4240,46", own, Across, Up));
    Check("the same x and y on another map is not the run's",
          !TravelErrandIsTheRunsOwnAim("at:0:1807.39,-4407.8,-18.4334", own, Across, Up));
    Check("the level above the point is not the run's",
          !TravelErrandIsTheRunsOwnAim("at:1:1807.39,-4407.8,52.6", own, Across, Up));
    Check("a run with no aims owns nothing",
          !TravelErrandIsTheRunsOwnAim(Staging, {}, Across, Up));
}

void TheRunAdoptsItsOwnAim()
{
    Check("its own staging aim does not hold the run up",
          DungeonRunMayClaimTravel(true, true));
    Check("somebody else's errand still does",
          !DungeonRunMayClaimTravel(true, false));
    Check("no errand at all never did", DungeonRunMayClaimTravel(false, false));
}

}  // namespace

int main()
{
    ArrivalAsksHeightToo();
    APlaceAimReadsBack();
    TheRunKnowsItsOwnAim();
    TheRunAdoptsItsOwnAim();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a place is reached across and up, and a run adopts its own aim\n");
    return EXIT_SUCCESS;
}
