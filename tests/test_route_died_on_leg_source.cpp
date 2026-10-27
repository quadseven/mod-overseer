/*
 * Source contract for the died-on-leg memory (mod-overseer#502, folded from
 * #1040).
 *
 * The rule is proved in test_route_died_on_leg.cpp. What only the adapter can
 * get wrong is whether it is asked: that DriveTravel reads deaths against the
 * route each walker was last planned, that PlanRoute keeps each leg's points
 * and plans on links priced for the walker rather than on the bare survey, and
 * that a plan that still walks a died-on leg says so.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const note = Between(text, "static void NoteDeathsOnRoutes()",
                                     "THE POINTS OF ONE LEG, IN ORDER");
    if (note.empty())
        return Fail("cannot find NoteDeathsOnRoutes");
    if (note.find("OverseerDecisions::LegsOfDeath(") == std::string::npos)
        return Fail("NoteDeathsOnRoutes must ask LegsOfDeath");
    if (note.find("OverseerDecisions::RememberDiedOnLeg(") == std::string::npos)
        return Fail("NoteDeathsOnRoutes must remember the leg");
    if (note.find("bot->GetCorpseLocation()") == std::string::npos)
        return Fail("a released walker's death must be read at its corpse");
    if (note.find("!memory.plannedAlive") == std::string::npos)
        return Fail("a route planned for a ghost must not be blamed for its death");

    if (note.find("++memory.deathsOnLegs;") == std::string::npos)
        return Fail("a death on a planned leg must be counted, so errands plan again");

    // An errand to a fixed place keeps its aim through a death: its route must
    // be planned again once the walker lives, or the price is never asked.
    std::string const once = Between(text, "static void PlanRouteOnce(",
                                     "THE ROAD A LONG ERRAND TO A PLACE WAS PLANNED ALONG");
    if (once.empty())
        return Fail("cannot find PlanRouteOnce");
    if (once.find("DeathsOnLegsOf(bot->GetName())") == std::string::npos ||
        once.find("OverseerDecisions::PlanErrandRouteAgain(state.routePlanned,") ==
            std::string::npos ||
        once.find("bot->IsAlive()") == std::string::npos)
        return Fail("PlanRouteOnce must plan a route again after a death on it, once alive");
    if (once.find("state.routePlannedAtDeaths = deaths;") == std::string::npos)
        return Fail("PlanRouteOnce must record the death count its plan was made at");
    if (once.find("if (state.routePlanned)\n            return;") != std::string::npos)
        return Fail("PlanRouteOnce must not keep a planned route whatever happened since");

    std::string const drive = Between(text, "void DriveTravel()", "SweepLeftoverAims() ||");
    if (drive.find("NoteDeathsOnRoutes();") == std::string::npos)
        return Fail("DriveTravel must read deaths first, before any errand is looked at");

    std::string const plan = Between(text, "static std::vector<OverseerDecisions::RoutePoint> PlanRoute(",
                                     "static OverseerDecisions::RouteLegLimits RouteLimitsFor(");
    if (plan.empty())
        return Fail("cannot find PlanRoute");
    if (plan.find("survey.nodes, linksForThisWalker(), bot->GetMapId()") == std::string::npos)
        return Fail("PlanRoute must plan on the links priced for this walker");
    if (plan.find("survey.nodes, survey.links, bot->GetMapId(), planFromX") != std::string::npos)
        return Fail("PlanRoute must not plan on the bare survey links");
    if (plan.find("OverseerDecisions::PriceDiedOnLegs(pricedLinks, diedOn.died") ==
        std::string::npos)
        return Fail("PlanRoute must price the walker's died-on legs on a copy");
    if (plan.find("diedOn.legs = std::move(spans);") == std::string::npos)
        return Fail("PlanRoute must keep which legs own which points");
    if (plan.find("OverseerDecisions::DiedOnLegsOnPlan(") == std::string::npos ||
        plan.find("its route still walks {} leg(s)") == std::string::npos)
        return Fail("a plan that still walks a died-on leg must say so");

    std::cout << "route died on leg source: all checks passed\n";
    return EXIT_SUCCESS;
}
