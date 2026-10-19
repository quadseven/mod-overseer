/*
 * A measured corridor is walked leg by leg, and its walk is read (#759).
 *
 * At Ragefire Chasm on the dev realm a leader joined the measured corridor at
 * point 3 of 11, 0 yards away, and within seventy seconds stood 230 yards off
 * every point of it on Orgrimmar's upper walkways. Two rules decide whether
 * that can happen, and this file is both of them.
 *
 * THE CURSOR. The nearest-point cursor in RouteLegStep handed a character the
 * next point as soon as it was nearer the point it was walking to than the one
 * it left: halfway along the leg, which on a measured corridor is the corner
 * the corridor was written down to stop anybody cutting. With
 * RouteLegLimits::advanceOnArrival the cursor is the point being walked to and
 * moves past it only on arrival, so a character that strays is sent back to
 * the point it has not reached.
 *
 * THE PATH. JudgeCorridorLegPath reads the walk the navmesh gave for a leg and
 * says when, and where, it leaves the leg, which is what the log line names.
 *
 * THE FIXTURE IS THE REAL CORRIDOR: points 3 to 10 of the 'ragefire' row in
 * src/mod_overseer.cpp and the staging point after them, which is the route
 * PlanStagingCorridor hands a leader joined at point 3. The stray position is
 * the one measured at 04:52:50 on 2026-09-27.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::CorridorLegPathLimits;
using OverseerDecisions::CorridorLegPathReading;
using OverseerDecisions::JudgeCorridorLegPath;
using OverseerDecisions::RouteAim;
using OverseerDecisions::RouteCursor;
using OverseerDecisions::RouteLegLimits;
using OverseerDecisions::RouteLegStep;
using OverseerDecisions::RoutePoint;

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

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

std::vector<RoutePoint> Ragefire()
{
    return {
        {1737.1f, -4458.7f, 37.83f},   // corridor point 3, where the leader joined
        {1769.5f, -4516.4f, 26.63f},
        {1834.7f, -4512.3f, 22.36f},
        {1881.3f, -4481.7f, 20.50f},
        {1889.2f, -4421.7f, 13.70f},
        {1837.1f, -4392.8f, 2.10f},
        {1795.7f, -4357.3f, -12.84f},
        {1807.4f, -4407.8f, -18.43f},  // the staging point
    };
}

// The limits RouteLimitsFor gives a measured corridor, and the ones it gave
// before #759, which is the same thing without the arrival rule.
RouteLegLimits Measured()
{
    RouteLegLimits limits;
    limits.lookaheadYards = 250.f;
    limits.arrivedYards = 12.f;
    limits.maxPointsAhead = 1;
    limits.advanceOnArrival = true;
    return limits;
}

RouteLegLimits MeasuredBefore759()
{
    RouteLegLimits limits = Measured();
    limits.advanceOnArrival = false;
    return limits;
}

RoutePoint Along(RoutePoint const& a, RoutePoint const& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

void TheCursorMovesOnArrival()
{
    std::vector<RoutePoint> const route = Ragefire();

    // Joined standing on the first point: it is already reached, so the aim
    // is the next one. The same answer as before #759.
    {
        RouteCursor cursor;
        RouteAim const aim = RouteLegStep(cursor, route, route[0].x, route[0].y, Measured());
        Check("joined on point 3: has an aim", aim.hasAim);
        CheckUInt("joined on point 3: aims at the next point", aim.index, 1);
    }

    // Sixty per cent along the first leg, nearer its end than its start and
    // still 26 yards short of it. The old rule had already moved on and aimed
    // past the point between the legs; the arrival rule keeps walking to it.
    {
        RoutePoint const at = Along(route[0], route[1], 0.6f);
        RouteCursor before;
        RouteAim const old = RouteLegStep(before, route, at.x, at.y, MeasuredBefore759());
        CheckUInt("mid-leg, before #759: cut the corner to the point after next",
                  old.index, 2);

        // Its cursor is where the first poll, standing on point 3, left it.
        RouteCursor cursor;
        cursor.at = 1;
        RouteAim const aim = RouteLegStep(cursor, route, at.x, at.y, Measured());
        CheckUInt("mid-leg: still aims at the end of the leg", aim.index, 1);
        CheckUInt("mid-leg: the cursor is the point walked to", cursor.at, 1);
    }

    // Joined short of the first point, which PlanStagingCorridor allows up to
    // DUNGEON_CORRIDOR_JOIN_YARDS: the join hop is walked to that point first.
    {
        RouteCursor cursor;
        RouteAim const aim = RouteLegStep(cursor, route, route[0].x - 40.f, route[0].y,
                                          Measured());
        CheckUInt("joined 40 yards short: walks to the join point", aim.index, 0);
    }

    // Within the arrival radius of the end of the leg, the next leg starts.
    {
        RoutePoint const at = Along(route[0], route[1], 0.9f);
        RouteCursor cursor;
        cursor.at = 1;
        RouteAim const aim = RouteLegStep(cursor, route, at.x, at.y, Measured());
        CheckUInt("arrived at the end of the leg: aims at the next one", aim.index, 2);
    }

    // Off the corridor where the leader was measured, sent to point 4 and not there.
    // The old rule moved the cursor to whichever point lay nearest, which is
    // four points on and never walked to; the arrival rule sends it back to
    // the point he was walking to.
    {
        RouteCursor before;
        before.at = 1;
        RouteAim const old = RouteLegStep(before, route, 1997.9f, -4237.0f, MeasuredBefore759());
        Check("off the corridor, before #759: skipped points never walked to",
              old.index > 2);

        RouteCursor cursor;
        cursor.at = 1;
        RouteAim const aim = RouteLegStep(cursor, route, 1997.9f, -4237.0f, Measured());
        CheckUInt("off the corridor: sent back to the point not yet reached", aim.index, 1);
        CheckUInt("off the corridor: the cursor does not move", cursor.at, 1);
    }

    // Forward only, still: a cursor at point 3 of the route is not sent back
    // to the start by standing there.
    {
        RouteCursor cursor;
        cursor.at = 3;
        RouteAim const aim = RouteLegStep(cursor, route, route[0].x, route[0].y, Measured());
        CheckUInt("never backward", aim.index, 3);
    }

    // Arriving on two points at once passes both. Point 3's successor here is
    // 5 yards on.
    {
        std::vector<RoutePoint> close = route;
        close.insert(close.begin() + 1, RoutePoint{1740.0f, -4462.0f, 37.0f});
        RouteCursor cursor;
        RouteAim const aim = RouteLegStep(cursor, close, close[0].x, close[0].y, Measured());
        CheckUInt("two points underfoot: both passed", aim.index, 2);
    }

    // The route is spent on its last point, exactly as before.
    {
        RouteCursor cursor;
        cursor.at = 7;
        RouteAim const aim = RouteLegStep(cursor, route, route[7].x, route[7].y, Measured());
        Check("on the staging point: arrived", aim.arrived);
        Check("on the staging point: no aim", !aim.hasAim);
    }

    // Standing on the second-last point aims at the last, never past it.
    {
        RouteCursor cursor;
        cursor.at = 6;
        RouteAim const aim = RouteLegStep(cursor, route, route[6].x, route[6].y, Measured());
        CheckUInt("on the last corner: aims at the staging point", aim.index, 7);
    }
}

void ThePathIsRead()
{
    std::vector<RoutePoint> const route = Ragefire();
    CorridorLegPathLimits limits;
    limits.widthYards = 30.f;

    // A walk down the leg itself stays on it.
    {
        std::vector<RoutePoint> const path = {route[0], Along(route[0], route[1], 0.5f),
                                              route[1]};
        CorridorLegPathReading const r = JudgeCorridorLegPath(route[0], route[1], path, limits);
        Check("the leg itself: judged", r.judged);
        Check("the leg itself: does not stray", !r.strays);
        Check("the leg itself: path as long as the leg",
              r.pathYards > r.legYards - 0.5f && r.pathYards < r.legYards + 0.5f);
    }

    // A walk up and round by where the leader was measured strays, and names that
    // point as the worst, with its height over the leg.
    {
        std::vector<RoutePoint> const path = {route[0], {1850.0f, -4300.0f, 60.0f},
                                              {1997.9f, -4237.0f, 90.3f}, route[1]};
        CorridorLegPathReading const r = JudgeCorridorLegPath(route[0], route[1], path, limits);
        Check("round the city: judged", r.judged);
        Check("round the city: strays", r.strays);
        CheckUInt("round the city: the worst point is the far one", r.worstIndex, 2);
        Check("round the city: over 200 yards off the leg", r.worstOffYards > 200.f);
        Check("round the city: 52 yards above the leg's higher end",
              r.worstAboveYards > 52.0f && r.worstAboveYards < 53.0f);
        Check("round the city: path far longer than the leg", r.pathYards > 5.f * r.legYards);
    }

    // A point beyond the end of the leg is measured to that end, not to the
    // line drawn on past it.
    {
        RoutePoint const beyond = Along(route[0], route[1], 1.6f);
        std::vector<RoutePoint> const path = {route[0], beyond};
        CorridorLegPathReading const r = JudgeCorridorLegPath(route[0], route[1], path, limits);
        Check("beyond the end: strays", r.strays);
        Check("beyond the end: measured to the end",
              r.worstOffYards > 0.6f * r.legYards - 0.5f &&
                  r.worstOffYards < 0.6f * r.legYards + 0.5f);
    }

    // A character already off the leg is the first point of its own path.
    {
        std::vector<RoutePoint> const path = {{1997.9f, -4237.0f, 37.0f}, route[1]};
        CorridorLegPathReading const r = JudgeCorridorLegPath(route[0], route[1], path, limits);
        Check("standing off the leg: strays", r.strays);
        CheckUInt("standing off the leg: the feet are the worst point", r.worstIndex, 0);
    }

    // Nothing to judge.
    {
        Check("empty path: not judged",
              !JudgeCorridorLegPath(route[0], route[1], {}, limits).judged);
        CorridorLegPathLimits none;
        Check("no width: not judged",
              !JudgeCorridorLegPath(route[0], route[1], {route[0]}, none).judged);
        Check("a leg of no length: not judged",
              !JudgeCorridorLegPath(route[0], route[0], {route[0]}, limits).judged);
    }
}

}  // namespace

int main()
{
    TheCursorMovesOnArrival();
    ThePathIsRead();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("corridor leg following: all checks passed\n");
    return 0;
}
