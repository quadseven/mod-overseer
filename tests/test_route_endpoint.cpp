/*
 * A route must end where it was asked to end.
 *
 * PathGenerator answers a point it cannot reach by substituting the closest
 * point it CAN reach, and reports that as a path. Accepting it as a route is
 * what walked the family at mountains. The old bound was twenty yards, which
 * is TRAVEL_GROUND_DROP_YARDS doubled and was never a statement about how far
 * a route may miss; it is five now, which is the bound the core itself uses in
 * PathGenerator::IsInvalidDestinationZ against this very quantity.
 *
 * The second half of this file is about the flag that is NOT consulted.
 * mod-overseer#203 refused every PATHFIND_INCOMPLETE path. The core sets that
 * flag whenever the corridor's last polygon is not the destination's
 * (PathGenerator.cpp:604-611), which covers both a route truncated at
 * MAX_PATH_LENGTH - fine, heading the right way, finish it next poll - and a
 * route whose destination was moved because it could not be reached. Refusing
 * both cost every long walk: after #210 shipped, three characters sent 455,
 * 510 and 1,040 yards got no route at all and stood still. The difference
 * between the two is not in the flag, it is in GetActualEndPosition, which the
 * core rewrites only in the second case. That is the quantity these tests
 * pin.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::RoutedPathGoesWhereAsked;
using OverseerDecisions::TravelEndpointWithinTolerance;

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

// The tolerance the adapter passes: TRAVEL_ROUTE_ENDPOINT_YARDS.
constexpr float TOLERANCE = 5.f;

// The tolerance it used to pass, kept so these tests can say what the change
// actually bites on rather than only that the new number works.
constexpr float OLD_TOLERANCE = 20.f;

// Named for the three inputs that are not heights, so the four-corner cases
// below read as sentences instead of as rows of bare booleans.
constexpr bool ROUTED = false;        // not PATHFIND_NOPATH
constexpr bool NO_ROUTE = true;
constexpr bool ON_THE_MESH = false;   // not PATHFIND_FARFROMPOLY_END
constexpr bool OFF_THE_MESH = true;
constexpr bool REAL_PATH = false;     // more than two points
constexpr bool SHORTCUT = true;

// ---------------------------------------------------------------- the height

void ARouteThatFinishesWhereItWasAskedIsARoute()
{
    Check("dead on the requested height",
          TravelEndpointWithinTolerance(100.f, 100.f, TOLERANCE), true);
    Check("two yards high, a doorway sill",
          TravelEndpointWithinTolerance(102.f, 100.f, TOLERANCE), true);
    Check("two yards low",
          TravelEndpointWithinTolerance(98.f, 100.f, TOLERANCE), true);
}

void TheBoundaryIsInclusiveAndSymmetric()
{
    Check("exactly the tolerance above",
          TravelEndpointWithinTolerance(105.f, 100.f, TOLERANCE), true);
    Check("exactly the tolerance below",
          TravelEndpointWithinTolerance(95.f, 100.f, TOLERANCE), true);
    Check("one yard past it above",
          TravelEndpointWithinTolerance(106.f, 100.f, TOLERANCE), false);
    Check("one yard past it below",
          TravelEndpointWithinTolerance(94.f, 100.f, TOLERANCE), false);
}

// The heights the operator saw accepted and walked, against the roads the
// party was actually standing on.
void TheRedridgeHeightsAreRefused()
{
    Check("an end at z 242 against a road at z 100",
          TravelEndpointWithinTolerance(242.f, 100.f, TOLERANCE), false);
    Check("an end at z 262 against a road at z 100",
          TravelEndpointWithinTolerance(262.f, 100.f, TOLERANCE), false);
    Check("an end at z 303 against a road at z 100",
          TravelEndpointWithinTolerance(303.f, 100.f, TOLERANCE), false);
}

// Those three were refused by the old twenty as well. What twenty admitted was
// the NEAR miss: the shoulder of a hill a dozen yards over the aim, which is a
// climb a character starts and a fall it finishes.
void TheTighteningBitesOnTheNearMiss()
{
    Check("twelve yards over the aim, under the old tolerance",
          TravelEndpointWithinTolerance(112.f, 100.f, OLD_TOLERANCE), true);
    Check("twelve yards over the aim, under the new one",
          TravelEndpointWithinTolerance(112.f, 100.f, TOLERANCE), false);
    Check("nineteen yards under the aim, under the old tolerance",
          TravelEndpointWithinTolerance(81.f, 100.f, OLD_TOLERANCE), true);
    Check("nineteen yards under the aim, under the new one",
          TravelEndpointWithinTolerance(81.f, 100.f, TOLERANCE), false);
}

// A distance cannot be negative. Folding it to its magnitude would turn a sign
// typo into a rule twice as loose as the one written, so it refuses instead,
// and it refuses even the end that needed no tolerance at all: the caller
// asked a question that has no answer.
void ANegativeToleranceIsRefusedRatherThanRead()
{
    Check("negative tolerance, end exact",
          TravelEndpointWithinTolerance(100.f, 100.f, -5.f), false);
    Check("negative tolerance, end near",
          TravelEndpointWithinTolerance(101.f, 100.f, -5.f), false);
    Check("negative tolerance, end on a peak",
          TravelEndpointWithinTolerance(303.f, 100.f, -5.f), false);
}

// Zero is a real answer and not a mistake: the route must finish on the height
// asked for and nowhere else.
void ZeroToleranceDemandsTheExactHeight()
{
    Check("zero tolerance, end exact",
          TravelEndpointWithinTolerance(100.f, 100.f, 0.f), true);
    Check("zero tolerance, end one yard off",
          TravelEndpointWithinTolerance(101.f, 100.f, 0.f), false);
}

// ----------------------------------------------------------- the whole verdict

// THE FOUR CORNERS, and the thing worth noticing is that completeness is not
// one of the axes. It cannot be: the core reports a truncated route and a
// moved destination under the same PATHFIND_INCOMPLETE flag. What separates
// them is whether the core rewrote the actual end position, so that is what
// these four cases vary. The middle two are the whole point of the change.
void TheFourCorners()
{
    // Complete, and it ended where it was asked. The ordinary good route.
    Check("arrived at the aim",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   100.f, 100.f, TOLERANCE), true);

    // Complete in the flags, but the end is nowhere near the aim's height. The
    // core does not produce this, and if it ever does it is not to be walked.
    Check("says it arrived, forty yards above the aim",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   140.f, 100.f, TOLERANCE), false);

    // TRUNCATED AT MAX_PATH_LENGTH: the corridor ran out of buffer, so the flag
    // says incomplete, but the core never touched the actual end position and
    // it is still the aim. This is the 455, 510 and 1,040 yard errand, and
    // #203 refused it. It is admitted now, and the next poll paths on from
    // further along.
    Check("a long route cut short, actual end still the aim",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   100.f, 100.f, TOLERANCE), true);

    // DESTINATION MOVED: the mesh could not reach the aim, substituted the
    // closest point it could, and said so through SetActualEndPosition. The
    // flag is the same one as the case above; the height is what tells them
    // apart, and this is the one that killed somebody.
    Check("destination moved to a shoulder 140 yards up",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   240.f, 100.f, TOLERANCE), false);
}

// The boundary belongs to the whole verdict too, not only to the height test
// it delegates to.
void TheVerdictKeepsTheBoundary()
{
    Check("exactly the tolerance above, routed",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   105.f, 100.f, TOLERANCE), true);
    Check("exactly the tolerance below, routed",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   95.f, 100.f, TOLERANCE), true);
    Check("one yard past the tolerance, routed",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   106.f, 100.f, TOLERANCE), false);
}

// Three refusals that no amount of walking improves, each of which must hold
// even when the height is perfect - otherwise a good height would launder a
// bad route.
void WhatIsStillRefusedOutright()
{
    Check("no route at all, height perfect",
          RoutedPathGoesWhereAsked(NO_ROUTE, ON_THE_MESH, REAL_PATH,
                                   100.f, 100.f, TOLERANCE), false);
    Check("the aim is off the mesh, height perfect",
          RoutedPathGoesWhereAsked(ROUTED, OFF_THE_MESH, REAL_PATH,
                                   100.f, 100.f, TOLERANCE), false);
    Check("a two point shortcut, height perfect",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, SHORTCUT,
                                   100.f, 100.f, TOLERANCE), false);
    Check("a negative tolerance refuses the whole verdict",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   100.f, 100.f, -5.f), false);
}

// THE CHOKE POINT ON THE APPROACH TO THE CITY, which is the regression this
// half of the change exists for. All five characters stopped within 120 yards
// of each other on the Elwynn road, 924 to 1,045 yards from a staging point
// inside the city, every one of them refused. A route that long is always
// truncated by MAX_PATH_LENGTH, so PATHFIND_INCOMPLETE was set, so #203 threw
// it away before anything else could look at it.
//
// A NOTE ON THE HEIGHTS IN THAT MEASUREMENT, because they are easy to read as
// endpoint errors and they are not. The 7 to 16 yards recorded there is the
// difference between where each CHARACTER was standing and the aim's own
// height. This predicate never sees a character's height. What it measures is
// the aim against the end the PATHFINDER says it will reach, and for a
// truncated route the core leaves that alone, so the error is zero and the
// route is walkable at any tolerance at all - five included. The tolerance
// change and this one do not fight over this case; they do not meet in it.
void TheApproachToTheCityIsWalkableAgain()
{
    // Truncated at the polygon cap, actual end untouched, so no height error.
    Check("926 yards to a staging point, corridor truncated",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   87.48f, 87.48f, TOLERANCE), true);
    Check("1,045 yards, the furthest of the five, corridor truncated",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   87.48f, 87.48f, TOLERANCE), true);

    // And the same aim, but with the destination actually MOVED seven yards
    // down because the mesh could not reach it. That is not the truncation
    // case and it is right to refuse it: seven is past the bound the core
    // itself uses for a wrong-height destination. Kept next to the case above
    // so the difference between them is on the page.
    Check("the same aim, destination moved seven yards down",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   80.4f, 87.48f, TOLERANCE), false);
}

// Route LENGTH is deliberately not an input. A nine hundred yard route is not
// worse than a ninety yard one, it is just longer, and the flag that used to
// stand in for "too long" was the bug. Nothing here varies with distance, and
// these two cases differ only in the height, which is the whole point.
void LengthIsNotEvidence()
{
    Check("a short route with a moved destination is still refused",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   140.f, 100.f, TOLERANCE), false);
    Check("a long route that still ends at its aim is fine",
          RoutedPathGoesWhereAsked(ROUTED, ON_THE_MESH, REAL_PATH,
                                   100.f, 100.f, TOLERANCE), true);
}

}  // namespace

int main()
{
    ARouteThatFinishesWhereItWasAskedIsARoute();
    TheBoundaryIsInclusiveAndSymmetric();
    TheRedridgeHeightsAreRefused();
    TheTighteningBitesOnTheNearMiss();
    ANegativeToleranceIsRefusedRatherThanRead();
    ZeroToleranceDemandsTheExactHeight();
    TheFourCorners();
    TheVerdictKeepsTheBoundary();
    WhatIsStillRefusedOutright();
    TheApproachToTheCityIsWalkableAgain();
    LengthIsNotEvidence();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a route must end where it was asked to end\n");
    return EXIT_SUCCESS;
}
