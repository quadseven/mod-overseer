/*
 * Source contract for #1228: a death is charged to the leg actually walked,
 * and every walk kind avoids a leg its walker just died on, or says it could
 * not.
 *
 * The rules are proved in test_route_died_on_leg_walked.cpp. What only the
 * adapter can get wrong is whether they are asked: that the death reading
 * charges only a route stepped along a moment ago, that RouteLeg records each
 * step against the plan it belongs to, that the road join is priced for the
 * walker and every return that keeps a surveyed leg says what it walks, and
 * that a creature (trainer) walk is steered along the priced route while its
 * walker holds a died-on leg.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find(to, start + 1);
    if (end == std::string::npos)
        return std::string();
    return text.substr(start, end - start);
}

std::size_t Count(std::string const& text, std::string const& what)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos;
         at = text.find(what, at + 1))
        ++n;
    return n;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    // Charged only to the leg being walked.
    std::string const note = Between(text, "static void NoteDeathsOnRoutes()",
                                     "THE POINTS OF ONE LEG, IN ORDER");
    if (note.empty())
        return Fail("cannot find NoteDeathsOnRoutes");
    if (note.find("OverseerDecisions::LegsWalkedAtDeath(memory.legs, memory.points,") ==
            std::string::npos ||
        note.find("memory.cursorAt, memory.steppedAt, now,") == std::string::npos)
        return Fail("NoteDeathsOnRoutes must charge only the leg the walker was walking");
    if (note.find("std::vector<OverseerDecisions::RouteLegSpan> const on =\n"
                  "                OverseerDecisions::LegsOfDeath(") != std::string::npos)
        return Fail("NoteDeathsOnRoutes must not charge every remembered leg near the body");

    // RouteLeg records each step against the plan it walks.
    std::string const leg = Between(text, "static WorldPosition RouteLeg(Player* bot,",
                                    "static void ReportCorridorLegPath(");
    if (leg.empty())
        return Fail("cannot find RouteLeg");
    if (leg.find("NoteRouteStep(bot->GetName(), state.routeSerial, state.routeCursor.at);") ==
        std::string::npos)
        return Fail("RouteLeg must record the walker's step along its route");
    std::string const once = Between(text, "static void PlanRouteOnce(",
                                     "THE ROAD A LONG ERRAND TO A PLACE WAS PLANNED ALONG");
    if (once.find("DiedOnLegBook()[bot->GetName()].routeSerial = state.routeSerial;") ==
        std::string::npos)
        return Fail("PlanRouteOnce must tie the remembered plan to its route serial");
    std::string const step = Between(text, "static void NoteRouteStep(",
                                     "static std::map<std::string, DiedOnLegMemory>& DiedOnLegBook()");
    if (step.find("it->second.routeSerial != routeSerial") == std::string::npos)
        return Fail("a step along another plan must not make this one look walked");

    // The road join is priced for this walker.
    std::string const join = Between(text, "static SurveyedRoadJoin JoinSurveyedRoad(",
                                     "THE WHOLE ROUTE FOR ONE ERRAND");
    if (join.empty())
        return Fail("cannot find JoinSurveyedRoad");
    if (join.find("OverseerDecisions::DiedOnLegPrice(link.from, link.to, died, now,") ==
        std::string::npos)
        return Fail("the road join must price a road the walker died on");
    if (join.find("survey.nodes, survey.links, bot->GetMapId()") != std::string::npos)
        return Fail("the road join must plan onward on the walker's priced links");
    std::string const plan = Between(text, "static std::vector<OverseerDecisions::RoutePoint> PlanRoute(",
                                     "static OverseerDecisions::RouteLegLimits RouteLimitsFor(");
    if (plan.find("road = JoinSurveyedRoad(bot, survey, linksForThisWalker(), diedOn.died,") ==
        std::string::npos)
        return Fail("PlanRoute must hand the road join the walker's priced links");
    // Both returns that keep a surveyed leg say what it walks.
    if (Count(plan, "SayDiedOnLegs(name, target, diedOn, diedOnHeld, plannedAt);") != 2)
        return Fail("every PlanRoute return with a surveyed leg must say what it died on");

    // A creature walk is steered while a died-on leg is held.
    std::string const drive = Between(text, "void DriveTravel()", "EndTravelPoll(stillAimed);");
    if (drive.empty())
        return Fail("cannot find DriveTravel");
    if (drive.find("OverseerDecisions::SteerCreatureWalkAlongRoute(") == std::string::npos ||
        drive.find("DiedOnLegsHeldBy(bot->GetName(), std::time(nullptr))") == std::string::npos)
        return Fail("DriveTravel must ask whether a creature walk is steered");
    if (drive.find("entry && !steered ? pos : RouteLeg(bot, pos, state, name, target);") ==
        std::string::npos)
        return Fail("a steered creature walk must follow RouteLeg");
    if (drive.find("WorldPosition const leg = entry ? pos : RouteLeg(") != std::string::npos)
        return Fail("a creature walk must not skip the route whatever its walker died on");
    if (drive.find("botAI->rpgInfo.ChangeToWanderNpc(moverEntry, aimAt);") == std::string::npos)
        return Fail("a steered walk on its route must be handed the route point, not the creature");

    std::cout << "route died on leg walked source: all checks passed\n";
    return EXIT_SUCCESS;
}
