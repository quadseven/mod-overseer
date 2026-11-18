/*
 * When the travel survey has no route, a dungeon's exit walk asks the navmesh
 * directly and walks its path only if it is complete; otherwise it fails as
 * before and the exit hearth follows.
 *
 * Seen live in Lower Blackrock Spire and Dire Maul East: the survey had no node
 * near the party, the walk to the exit areatrigger never started, and the family
 * hearthed out, which spends the thirty minute hearth cooldown.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::ExitRouteChoice;
using OverseerDecisions::ExitRouteWithoutSurvey;
using OverseerDecisions::NavmeshExitPathLimits;
using OverseerDecisions::NavmeshExitPathReading;

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

NavmeshExitPathReading Complete()
{
    NavmeshExitPathReading r;
    r.computed = true;
    r.everyPointOnMesh = true;
    r.points = 6;
    r.endGapYards = 0.5f;
    r.longestLegYards = 90.f;
    return r;
}

void TheDecision()
{
    NavmeshExitPathLimits const limits;
    Check("a survey route is used whatever the navmesh says",
          ExitRouteWithoutSurvey(true, NavmeshExitPathReading{}, limits) == ExitRouteChoice::Survey);
    Check("no survey route and a complete navmesh path walks",
          ExitRouteWithoutSurvey(false, Complete(), limits) == ExitRouteChoice::Navmesh);

    NavmeshExitPathReading partial = Complete();
    partial.partial = true;
    Check("a partial path falls back to the hearth",
          ExitRouteWithoutSurvey(false, partial, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading shortcut = Complete();
    shortcut.shortcut = true;
    Check("the core's two-point shortcut is no route",
          ExitRouteWithoutSurvey(false, shortcut, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading none = Complete();
    none.noPath = true;
    Check("no path at all is no route",
          ExitRouteWithoutSurvey(false, none, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading notComputed = Complete();
    notComputed.computed = false;
    Check("an uncomputed path is no route",
          ExitRouteWithoutSurvey(false, notComputed, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading offMesh = Complete();
    offMesh.everyPointOnMesh = false;
    Check("a point off the mesh is no route",
          ExitRouteWithoutSurvey(false, offMesh, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading endOff = Complete();
    endOff.endOffMesh = true;
    Check("an aim off the mesh is no route",
          ExitRouteWithoutSurvey(false, endOff, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading short_ = Complete();
    short_.endGapYards = 40.f;
    Check("a path that stops short of the aim is no route",
          ExitRouteWithoutSurvey(false, short_, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading longLeg = Complete();
    longLeg.longestLegYards = 400.f;
    Check("a leg longer than a route may span is no route",
          ExitRouteWithoutSurvey(false, longLeg, limits) == ExitRouteChoice::NoRoute);

    NavmeshExitPathReading single = Complete();
    single.points = 1;
    Check("one point is not a route",
          ExitRouteWithoutSurvey(false, single, limits) == ExitRouteChoice::NoRoute);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    std::size_t const fn = source.find("static bool NavmeshExitRoute(Player* bot");
    Check("the navmesh exit route exists", fn != std::string::npos);
    std::size_t const end = source.find("static void SayDangerZones(", fn);
    std::string const body = fn == std::string::npos ? "" : source.substr(fn, end - fn);
    Check("it asks the core's PathGenerator for a straight path",
          body.find("PathGenerator path(bot)") != std::string::npos &&
              body.find("SetUseStraightPath(true)") != std::string::npos);
    Check("it checks every point against the mesh",
          body.find("findNearestPoly") != std::string::npos);
    Check("it decides through the pure function",
          body.find("OverseerDecisions::ExitRouteWithoutSurvey(") != std::string::npos);
    Check("it never teleports", body.find("TeleportTo") == std::string::npos);

    std::size_t const plan = source.find("static std::vector<OverseerDecisions::RoutePoint> PlanRoute(");
    std::size_t const planEnd = source.find("static bool NavmeshExitRoute(", plan);
    std::string const planBody = plan == std::string::npos ? "" : source.substr(plan, planEnd - plan);
    Check("PlanRoute falls back when the survey is empty and when it plans no way",
          planBody.find("NavmeshExitRoute(bot, want, name, target, route)") != std::string::npos);
    std::size_t first = planBody.find("NavmeshExitRoute(bot, want");
    Check("and at both places",
          first != std::string::npos &&
              planBody.find("NavmeshExitRoute(bot, want", first + 1) != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_exit_navmesh_fallback\n");
    return 0;
}
