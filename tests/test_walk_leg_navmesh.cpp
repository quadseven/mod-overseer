/*
 * A walk's next leg can be taken off the navmesh's own path (2026-10-03).
 *
 * Measured on the dev realm: guild vendor walks from the Darnassus terraces
 * and across the Durotar cliffs were refused on their first leg, by the
 * travel survey as well, because the straight leg's aim had no walkable
 * polygon under it. MailWalkPointAlongPath picks the point one leg along the
 * core's path instead, which is walkable by construction.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::MailWalkPoint;
using OverseerDecisions::MailWalkPointAlongPath;

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

bool Near(MailWalkPoint const& p, float x, float y, float z)
{
    return std::fabs(p.x - x) < 0.01f && std::fabs(p.y - y) < 0.01f && std::fabs(p.z - z) < 0.01f;
}

void ALegAlongAZigzag()
{
    // 100 east, then 100 north, then 100 east: 250 along ends half way into
    // the third segment, not 250 yards east of the start.
    std::vector<MailWalkPoint> const path = {
        {0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {200, 100, 0}};
    MailWalkPoint got;
    Check("found", MailWalkPointAlongPath(path, 250.f, got));
    Check("half way along the third segment", Near(got, 150.f, 100.f, 0.f));
}

void AShortPathEndsAtItsEnd()
{
    std::vector<MailWalkPoint> const path = {{0, 0, 0}, {30, 40, 0}};
    MailWalkPoint got;
    Check("found", MailWalkPointAlongPath(path, 250.f, got));
    Check("the end of a path shorter than a leg", Near(got, 30.f, 40.f, 0.f));
}

void HeightCountsInTheLength()
{
    std::vector<MailWalkPoint> const path = {{0, 0, 0}, {0, 0, 10}};
    MailWalkPoint got;
    Check("found", MailWalkPointAlongPath(path, 5.f, got));
    Check("half way up", Near(got, 0.f, 0.f, 5.f));
}

void NoPathNoPoint()
{
    MailWalkPoint got;
    Check("an empty path gives nothing", !MailWalkPointAlongPath({}, 250.f, got));
    Check("one point is not a path",
          !MailWalkPointAlongPath({MailWalkPoint{1, 2, 3}}, 250.f, got));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("a refused leg falls back to the navmesh",
          source.find("return NavmeshLegToward(who, ev, step);") != std::string::npos);
    Check("which takes its leg from the first waypoints of the core's path",
          source.find("OverseerDecisions::PickFirstLegWaypoint(") != std::string::npos);
}

}  // namespace

int main()
{
    ALegAlongAZigzag();
    AShortPathEndsAtItsEnd();
    HeightCountsInTheLength();
    NoPathNoPoint();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_walk_leg_navmesh\n");
    return 0;
}
