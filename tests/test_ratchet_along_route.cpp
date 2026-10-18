/*
 * The distance ratchet for a walker on a surveyed route (#70). A route round a
 * crater rim walks AWAY from its aim for most of its length, so the straight
 * line alone reads a walker who is arriving as one who has stalled. Compiled
 * without AzerothCore, like every test here.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::Ratchet;
using OverseerDecisions::RatchetAlongRoute;
using OverseerDecisions::RatchetLimits;
using OverseerDecisions::RatchetReading;
using OverseerDecisions::RatchetState;
using OverseerDecisions::RouteMark;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// The travel backstop's own shape: 10 yards nearer inside twenty minutes.
RatchetLimits const TRAVEL{RatchetReading::DistanceToTarget, 10.f, 20 * 60};

RouteMark On(std::uint64_t route, long at)
{
    RouteMark mark;
    mark.route = route;
    mark.at = at;
    return mark;
}

// The catch-up walk this issue was measured on, compressed into one shape:
// start 1663 yards out, then a route whose nodes stand further out than the
// start (1780, 1772, then out to 2070 at Thistleshrub Valley) for longer than
// the twenty-minute backstop, every poll a step further along the same route.
// The straight-line ratchet stalls and releases a walker that is arriving; the
// route-aware one does not.
void AWalkerGoingRoundTheRimIsNotStalled()
{
    float const gaps[] = {1663.f, 1780.f, 1772.f, 1700.f, 1690.f, 1800.f, 1950.f,
                          2070.f, 2000.f, 1900.f, 1800.f, 1700.f, 1680.f, 1670.f};
    RatchetState straight;
    RatchetState along;
    RouteMark seen;
    bool straightStalled = false;
    bool alongStalled = false;
    time_t now = 1000;
    long cursor = 0;
    for (float const gap : gaps)
    {
        straightStalled = Ratchet(straight, gap, now, TRAVEL).stalled || straightStalled;
        alongStalled =
            RatchetAlongRoute(along, seen, gap, On(7, cursor), now, TRAVEL).stalled ||
            alongStalled;
        now += 120;
        cursor += 40;
    }
    Check("the straight line alone calls the rim walk stalled (the defect)", straightStalled,
          true);
    Check("a walker further along its route every poll is not stalled", alongStalled, false);
    Check("route progress leaves the best straight-line reading alone",
          along.best == 1663.f, true);
}

// A walker wedged mid-corridor keeps the same cursor on the same route, so the
// release-as-unreachable path still catches it, on the same clock as before.
void AWalkerWedgedOnItsRouteStillStalls()
{
    RatchetState state;
    RouteMark seen;
    RatchetAlongRoute(state, seen, 900.f, On(3, 12), 100, TRAVEL);
    Check("same cursor inside the patience is not stalled",
          RatchetAlongRoute(state, seen, 905.f, On(3, 12), 100 + 1200, TRAVEL).stalled, false);
    Check("same cursor past the patience stalls",
          RatchetAlongRoute(state, seen, 902.f, On(3, 12), 100 + 1201, TRAVEL).stalled, true);
}

// A replan hands out a new route, whose cursor starts again; reading that as
// progress would let an errand that is re-planned every few polls never stall.
void ANewRouteIsNotProgressByItself()
{
    RatchetState state;
    RouteMark seen;
    RatchetAlongRoute(state, seen, 900.f, On(3, 40), 100, TRAVEL);
    auto const replanned = RatchetAlongRoute(state, seen, 900.f, On(4, 41), 200, TRAVEL);
    Check("a different route at a higher index is not progress", replanned.progressed, false);
    Check("the clock was not restarted by the replan", state.since == 100, true);
    Check("the first step on the new route is progress",
          RatchetAlongRoute(state, seen, 900.f, On(4, 42), 300, TRAVEL).progressed, true);
    Check("and it restarts the clock", state.since == 300, true);
    Check("losing the route is not progress",
          RatchetAlongRoute(state, seen, 900.f, RouteMark{}, 400, TRAVEL).progressed, false);
    Check("the remembered mark follows the reading", seen.route == 0 && seen.at == -1, true);
}

// No route at all is exactly Ratchet, and so is a real approach on a route.
void WithoutARouteItIsThePlainRatchet()
{
    RatchetState plain;
    RatchetState along;
    RouteMark seen;
    float const gaps[] = {500.f, 495.f, 480.f, 480.f, 470.f, 469.f};
    time_t now = 10;
    for (float const gap : gaps)
    {
        auto const a = Ratchet(plain, gap, now, TRAVEL);
        auto const b = RatchetAlongRoute(along, seen, gap, RouteMark{}, now, TRAVEL);
        Check("same progress verdict with no route", a.progressed, b.progressed);
        Check("same stall verdict with no route", a.stalled, b.stalled);
        now += 700;
    }
    Check("same best with no route", plain.best == along.best, true);
    Check("same clock with no route", plain.since == along.since, true);

    RatchetState onRoute;
    RouteMark mark;
    RatchetAlongRoute(onRoute, mark, 500.f, On(9, 0), 10, TRAVEL);
    RatchetAlongRoute(onRoute, mark, 400.f, On(9, 3), 20, TRAVEL);
    Check("a real approach on a route still moves best", onRoute.best == 400.f, true);
}

// The other readings are not distances to a target, and a route says nothing
// about them.
void OtherReadingsIgnoreTheRoute()
{
    RatchetLimits const count{RatchetReading::CountAchieved, 0.f, 60};
    RatchetState state;
    RouteMark seen;
    RatchetAlongRoute(state, seen, 0.f, On(5, 0), 100, count);
    Check("a count reading is not moved on by route progress",
          RatchetAlongRoute(state, seen, 0.f, On(5, 9), 200, count).progressed, false);
}

}  // namespace

int main()
{
    AWalkerGoingRoundTheRimIsNotStalled();
    AWalkerWedgedOnItsRouteStillStalls();
    ANewRouteIsNotProgressByItself();
    WithoutARouteItIsThePlainRatchet();
    OtherReadingsIgnoreTheRoute();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("route progress keeps a walker going round the rim from stalling\n");
    return EXIT_SUCCESS;
}
