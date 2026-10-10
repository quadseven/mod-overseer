/*
 * Two parts of the cross-to-map row that the dev realm showed never landing
 * anyone (quadseven/wow-overseer#805): the step off the deck, and the ride
 * between two stops of one map.
 *
 * THE LANDING. Of 427 cross-to-map rows on the dev realm in 31 hours none
 * arrived. A member that did sail was walked 26 yards off the Theramore boat to
 * a landing 0.4 yards away, the transport let go of it five polls running, and
 * it was still a passenger when the boat cast off. PickLanding judges a landing
 * on its neighbours and does not try a failed one twice.
 *
 * THE RIDE. `ride-to-stop map:<id> x:<x> y:<y>` is the Undercity to Grom'gol
 * zeppelin, which never leaves map 0. ReadCrossing answers it with the same
 * walk, board, ride, walk off and done as a crossing between two maps.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
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

void CheckAction(char const* what, D::CrossingAction got, D::CrossingAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, D::CrossingActionName(got),
                D::CrossingActionName(want));
    ++failures;
}

D::LandingProbe Probe(bool onDeck, bool hullBeside)
{
    D::LandingProbe p;
    p.onDeck = onDeck;
    p.hullBeside = hullBeside;
    return p;
}

void ALandingAtTheHullsEdgeIsNotTheFirstChoice()
{
    // The Theramore case: the nearest dry landing has the hull beside it.
    std::vector<D::LandingProbe> const landings = {Probe(true, true), Probe(false, true),
                                                   Probe(false, false), Probe(false, false)};
    Check("the nearest landing with the hull clear wins over a nearer one at the edge",
          D::PickLanding(landings, 0) == 2);
    Check("a landing that did not hold is not tried twice",
          D::PickLanding(landings, 1) == 3);
    Check("when every clear landing failed, the nearest dry one is the last resort",
          D::PickLanding(landings, 2) == 1);
    Check("a deck that covers every landing gives none",
          D::PickLanding({Probe(true, true), Probe(true, false)}, 0) == -1);
    Check("no landings gives none", D::PickLanding({}, 0) == -1);
    Check("only edge landings: the nearest dry one",
          D::PickLanding({Probe(true, false), Probe(false, true), Probe(false, true)}, 0) == 1);
}

void TheRideRowParses()
{
    D::CrossRequest r = D::ParseCrossRequest("ride-to-stop map:0 x:1830.5 y:238.25");
    Check("a ride parses", !*r.error && r.ride && r.map == 0 && r.x > 1830.4f && r.x < 1830.6f &&
                               r.y > 238.2f && r.y < 238.3f);
    r = D::ParseCrossRequest("ride-to-stop map:0 x:-12432 y:208");
    Check("negative and whole coordinates parse", !*r.error && r.ride && r.x < -12431.f);
    r = D::ParseCrossRequest("cross-to-map map:1");
    Check("a crossing is not a ride", !*r.error && !r.ride && r.map == 1);
    for (char const* bad :
         {"ride-to-stop", "ride-to-stop map:0", "ride-to-stop map:0 x:1", "ride-to-stop map:0 y:1 x:1",
          "ride-to-stop map:0 x:a y:1", "ride-to-stop map:0 x: y:1", "ride-to-stop map:0 x:1. y:1",
          "ride-to-stop map:0 x:--1 y:1", "ride-to-stop map:0 x:1 y:1 extra",
          "ride-to-stop map:x x:1 y:1", "ride-to-stop map:0 x:12345678901 y:1",
          "cross-to-map map:0 x:1 y:1"})
        Check(bad, std::strcmp(D::ParseCrossRequest(bad).error, D::CrossRefusal::Malformed) == 0);
    Check("a ride is a cross row", D::IsCrossRow("ride-to-stop map:0 x:1 y:2"));
}

void ARiderAlreadyAtTheStopIsToldSo()
{
    D::CrossFacts f;
    f.onTargetMap = true;
    Check("a crossing already on the map says so",
          std::strcmp(D::CrossGate(f), D::CrossRefusal::AlreadyThere) == 0);
    f.rideRequest = true;
    Check("a ride already at the stop says so",
          std::strcmp(D::CrossGate(f), D::CrossRefusal::AlreadyAtStop) == 0);
    Check("and asking again does not help",
          !D::CrossRefusalRetryable(D::CrossRefusal::AlreadyAtStop));
}

D::CrossingWorld RideWorld()
{
    D::CrossingWorld w;
    w.originMap = 0;
    w.destinationMap = 0;
    w.sameMap = true;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.mooringKnown = true;
    return w;
}

D::CrossingMember Rider()
{
    D::CrossingMember m;
    m.name = "Zug";
    m.readable = true;
    m.isLeader = true;
    m.mapId = 0;
    return m;
}

D::CrossingLimits Limits()
{
    D::CrossingLimits l;
    l.berthArrivedYards = 12.f;
    l.gatherYards = 30.f;
    l.minBoardDwellMs = 15000;
    return l;
}

D::CrossingAction Read(D::CrossingWorld const& w, D::CrossingMember const& m)
{
    return D::ReadCrossing(w, {m}, Limits()).action;
}

void AZeppelinRideOnOneMapRunsTheSameFiveSteps()
{
    using A = D::CrossingAction;
    D::CrossingWorld w = RideWorld();
    D::CrossingMember m = Rider();

    m.berthDistance = 900.f;
    CheckAction("far from the berth the rider walks to it", Read(w, m), A::Walk);

    m.berthDistance = 3.f;
    CheckAction("at the berth with the zeppelin away it holds", Read(w, m), A::Hold);

    w.dockedAtOrigin = true;
    w.dwellLeftMs = 40000;
    CheckAction("at the berth with the zeppelin docked it boards", Read(w, m), A::Board);

    m.aboard = true;
    m.berthDistance = 0.f;
    CheckAction("aboard at the origin stop it rides, not walks off", Read(w, m), A::Ride);

    w.dockedAtOrigin = false;
    CheckAction("aboard between the stops it rides", Read(w, m), A::Ride);

    w.dockedAtDestination = true;
    w.dwellLeftMs = 40000;
    CheckAction("aboard at the far stop it walks off", Read(w, m), A::WalkOff);

    w.dwellLeftMs = 4000;
    CheckAction("with the stop nearly over it watches", Read(w, m), A::Disembark);

    m.aboard = false;
    m.atLanding = true;
    CheckAction("at the landing and off the deck it is done", Read(w, m), A::Done);
}

void AnEndedStopIsNotAnArrival()
{
    // The origin stop's landing is where the rider started on a round trip: a
    // rider that is off the deck and NOT at the landing is still waiting.
    D::CrossingWorld w = RideWorld();
    D::CrossingMember m = Rider();
    m.berthDistance = 3.f;
    m.atLanding = false;
    Check("off the deck and away from the landing is not done",
          Read(w, m) != D::CrossingAction::Done);
}

void TwoMapsStillRefuseOneMap()
{
    D::CrossingWorld w = RideWorld();
    w.sameMap = false;
    CheckAction("a crossing from a map to itself is still a caller bug", Read(w, Rider()),
                D::CrossingAction::Refuse);
}

void AMemberOnAnotherMapIsOffTheRide()
{
    D::CrossingWorld w = RideWorld();
    D::CrossingMember m = Rider();
    m.mapId = 1;
    CheckAction("a rider on a third map is refused, not walked", Read(w, m),
                D::CrossingAction::Refuse);
}

}  // namespace

int main()
{
    ALandingAtTheHullsEdgeIsNotTheFirstChoice();
    TheRideRowParses();
    ARiderAlreadyAtTheStopIsToldSo();
    AZeppelinRideOnOneMapRunsTheSameFiveSteps();
    AnEndedStopIsNotAnArrival();
    TwoMapsStillRefuseOneMap();
    AMemberOnAnotherMapIsOffTheRide();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
