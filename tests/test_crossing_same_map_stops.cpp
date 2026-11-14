/*
 * Same-map ride stops are selected by the boat's mooring, not its surveyed berth (#1481).
 *
 * The island boat has Rut'theran and Auberdine stops on map 1. From Darnassus,
 * the boarding stop is Rut'theran even when the only surveyed berth is at
 * Auberdine. That is a missing boarding berth, not a route that vanished.
 *
 * Compiled against src/overseer_decisions.cpp and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace D = OverseerDecisions;

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

constexpr float DARNASSUS_X = 9947.0f;
constexpr float DARNASSUS_Y = 2482.0f;
constexpr float RUTTHERAN_X = 8640.0f;
constexpr float RUTTHERAN_Y = 841.0f;

std::vector<D::SameMapRideStop> IslandBoat(bool ruttheranBerth, bool auberdineBerth)
{
    return {{1, RUTTHERAN_X, RUTTHERAN_Y, ruttheranBerth},
            {1, D::AUBERDINE_STOP_X, D::AUBERDINE_STOP_Y, auberdineBerth}};
}

void SelectsTheIslandBoatStopsByMooring()
{
    D::SameMapRideSelection const selected = D::SelectSameMapRideStops(
        IslandBoat(true, true), 1, DARNASSUS_X, DARNASSUS_Y,
        D::AUBERDINE_STOP_X, D::AUBERDINE_STOP_Y);
    Check("Darnassus boards at Rut'theran", selected.boardingStop == 0);
    Check("the requested Auberdine stop is the landing", selected.landingStop == 1);
    Check("both surveyed berths make the route ready",
          selected.route == D::SameMapRideRoute::Ready);

    D::CrossingOffer offer;
    offer.entry = 1;
    offer.crew = D::CrewWelcome::Welcome;
    offer.berthKnown = true;
    offer.landingKnown = true;
    offer.toBerthYards = 100.f;
    offer.landingToGoalYards = 0.f;
    offer.rideSeconds = 120.f;
    offer.periodSeconds = 900.f;
    D::CrossingPriceLimits limits;
    limits.yardsPerSecond = 1.f;
    D::CrossingPrice const price = D::PriceCrossings({offer}, limits);
    Check("the selected island boat is priced", price.pick == 0 &&
          price.verdicts[0] == D::OfferVerdict::Priced);
}

void ReportsTheMissingBoardingBerthByStop()
{
    D::SameMapRideSelection const selected = D::SelectSameMapRideStops(
        IslandBoat(false, true), 1, DARNASSUS_X, DARNASSUS_Y,
        D::AUBERDINE_STOP_X, D::AUBERDINE_STOP_Y);
    Check("the real mooring pair is still selected", selected.boardingStop == 0 &&
          selected.landingStop == 1);
    Check("the absent Rut'theran berth is the failure",
          selected.route == D::SameMapRideRoute::NoBoardingBerth);
    D::CrossingOffer offer;
    offer.crew = D::CrewWelcome::Welcome;
    offer.berthKnown = false;
    offer.landingKnown = true;
    offer.rideSeconds = 120.f;
    offer.periodSeconds = 900.f;
    D::CrossingPriceLimits limits;
    limits.yardsPerSecond = 1.f;
    D::CrossingPrice const price = D::PriceCrossings({offer}, limits);
    Check("the selected route reaches pricing as a missing berth",
          price.pick < 0 && price.verdicts[0] == D::OfferVerdict::NoBerth);
    std::string const reason = D::SameMapRideReason(selected, 1);
    Check("the reason names the boarding stop", reason.find("boarding stop 1") != std::string::npos);
    Check("the reason names map 1", reason.find("map 1") != std::string::npos);
}

void RefusesWhenBothEndsSelectOneStop()
{
    D::SameMapRideSelection const selected = D::SelectSameMapRideStops(
        IslandBoat(true, true), 1, RUTTHERAN_X, RUTTHERAN_Y,
        RUTTHERAN_X, RUTTHERAN_Y);
    Check("same-stop rides are called out before berth checks",
          selected.route == D::SameMapRideRoute::SameStop);
    Check("the reason names the selected stop",
          D::SameMapRideReason(selected, 1).find("stop 1 on map 1") != std::string::npos);
}

void NoStopOnTheRequestedMapIsNamed()
{
    D::SameMapRideSelection const selected =
        D::SelectSameMapRideStops(IslandBoat(true, true), 0, 0.f, 0.f, 1.f, 1.f);
    Check("an absent map stop is not a same-stop route",
          selected.route == D::SameMapRideRoute::NoStops);
    Check("the reason names the map", D::SameMapRideReason(selected, 0).find("map 0") !=
          std::string::npos);
}

void TheAdapterUsesTheSelectionAndKeepsItsRefusalDetail()
{
    std::ifstream in("src/mod_overseer.cpp", std::ios::binary);
    std::string const source((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    Check("adapter source is readable", !source.empty());
    Check("same-map pricing calls the pure stop selector",
          source.find("SelectSameMapRideStops(") != std::string::npos);
    Check("the row refusal carries the detailed route verdict",
          source.find("refuseWith(wall, routeDetails)") != std::string::npos);
}

}  // namespace

int main()
{
    SelectsTheIslandBoatStopsByMooring();
    ReportsTheMissingBoardingBerthByStop();
    RefusesWhenBothEndsSelectOneStop();
    NoStopOnTheRequestedMapIsNamed();
    TheAdapterUsesTheSelectionAndKeepsItsRefusalDetail();

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("same-map ride stop selection: all checks passed\n");
    return 0;
}
