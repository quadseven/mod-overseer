/*
 * The unknown-hop scan names only nodes the character's own team can learn
 * (#501).
 *
 * Logged on the dev realm 2026-09-19: an Alliance family "could fly node 79 to
 * node 80, but has not discovered node 40" and was told `flight master:40`
 * would go learn it. Node 40 is Gadgetzan's HORDE flight point (creature 7824);
 * node 39, 180 yards away, is the Alliance one (creature 7823). Upstream's taxi
 * BFS is built once over the whole network for both sides, so the route went
 * through the other side's node and the scan named it.
 *
 * The fixture is that Gadgetzan pair. Mount creatures are the shape
 * TaxiNodes.dbc gives them: a node one side flies from carries a mount for
 * that side only, a neutral node carries both. The negative cases assert on
 * an EMPTY RESULT (no route, learnNode zero), not on the absence of a log
 * line, so a silently skipped branch cannot read as a pass.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::BuildTeamTaxiGraph;
using OverseerDecisions::FindTeamTaxiPath;
using OverseerDecisions::ScanTaxiRoute;
using OverseerDecisions::TaxiEdge;
using OverseerDecisions::TaxiHopRead;
using OverseerDecisions::TaxiNodeServesTeam;
using OverseerDecisions::TaxiRouteScan;
using OverseerDecisions::TaxiRouteVerdict;
using OverseerDecisions::TeamTaxiGraph;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

std::string Join(std::vector<uint32_t> const& path)
{
    std::string out;
    for (uint32_t node : path)
        out += (out.empty() ? "" : "->") + std::to_string(node);
    return out.empty() ? "(none)" : out;
}

void CheckPath(std::vector<uint32_t> const& got, std::vector<uint32_t> const& want,
               char const* what)
{
    if (got != want)
    {
        std::printf("FAIL %s: expected %s, got %s\n", what, Join(want).c_str(),
                    Join(got).c_str());
        ++failures;
    }
}

// --- the fixture -----------------------------------------------------------

struct FixtureNode
{
    uint32_t id;
    uint32_t hordeMount;     // MountCreatureID[0]
    uint32_t allianceMount;  // MountCreatureID[1]
};

constexpr uint32_t MARSHALS_REFUGE = 79;     // neutral
constexpr uint32_t RATCHET = 80;             // neutral
constexpr uint32_t GADGETZAN_ALLIANCE = 39;  // Bera Stonehammer, creature 7823
constexpr uint32_t GADGETZAN_HORDE = 40;     // Bulkrek Ragefist, creature 7824

std::vector<FixtureNode> const NODES = {
    {MARSHALS_REFUGE, 2224, 541},
    {RATCHET, 2224, 541},
    {GADGETZAN_ALLIANCE, 0, 541},
    {GADGETZAN_HORDE, 2224, 0},
};

std::vector<uint32_t> TeamNodes(bool alliance)
{
    std::vector<uint32_t> out;
    for (FixtureNode const& node : NODES)
    {
        if (TaxiNodeServesTeam(true, node.hordeMount, node.allianceMount, alliance))
            out.push_back(node.id);
    }
    return out;
}

std::vector<uint32_t> EveryNode()
{
    std::vector<uint32_t> out;
    for (FixtureNode const& node : NODES)
        out.push_back(node.id);
    return out;
}

bool Serves(uint32_t id, bool alliance)
{
    for (FixtureNode const& node : NODES)
    {
        if (node.id == id)
            return TaxiNodeServesTeam(true, node.hordeMount, node.allianceMount, alliance);
    }
    return false;
}

// TaxiPath.dbc rows are one-way; the network is two-way, so both directions.
void Link(std::vector<TaxiEdge>& edges, uint32_t a, uint32_t b)
{
    edges.push_back({a, b});
    edges.push_back({b, a});
}

// The hop reading ConsiderFlight builds for one route.
std::vector<TaxiHopRead> Read(std::vector<uint32_t> const& path,
                              std::vector<uint32_t> const& known, bool alliance)
{
    std::vector<TaxiHopRead> hops;
    for (uint32_t node : path)
    {
        TaxiHopRead hop;
        hop.node = node;
        for (uint32_t k : known)
            hop.known = hop.known || k == node;
        hop.servesTeam = Serves(node, alliance);
        hops.push_back(hop);
    }
    return hops;
}

// --- the cases -------------------------------------------------------------

// THE PREDICATE IS ObjectMgr::GetNearestTaxiNode'S, which is what
// SendLearnNewTaxiNode learns from. The Gadgetzan pair is the case that
// exposed this: each side holds exactly one of the two.
void EachGadgetzanNodeServesOneSide()
{
    Check(TaxiNodeServesTeam(true, 0, 541, true), "node 39 serves the Alliance");
    Check(!TaxiNodeServesTeam(true, 0, 541, false), "node 39 does not serve the Horde");
    Check(TaxiNodeServesTeam(true, 2224, 0, false), "node 40 serves the Horde");
    Check(!TaxiNodeServesTeam(true, 2224, 0, true), "node 40 does not serve the Alliance");
    Check(TaxiNodeServesTeam(true, 2224, 541, true) &&
              TaxiNodeServesTeam(true, 2224, 541, false),
          "a neutral node serves both sides");
    Check(!TaxiNodeServesTeam(false, 2224, 541, true),
          "a node off the taxi network mask serves nobody");
    Check(TaxiNodeServesTeam(true, OverseerDecisions::TAXI_DEATH_KNIGHT_MOUNT_CREATURE, 0,
                             true),
          "the death knight node serves the Alliance as the core allows");
}

// THE LOGGED CASE. The only road 79 -> 80 runs through Horde Gadgetzan. A
// faction-blind graph finds it (that is the route the module logged); the
// Alliance graph does not, and the scan of that empty route names nothing.
void AnAllianceRouteOnlyThroughHordeNodesSuggestsNothing()
{
    std::vector<TaxiEdge> edges;
    Link(edges, MARSHALS_REFUGE, GADGETZAN_HORDE);
    Link(edges, GADGETZAN_HORDE, RATCHET);
    Link(edges, MARSHALS_REFUGE, GADGETZAN_ALLIANCE);

    TeamTaxiGraph const blind = BuildTeamTaxiGraph(edges, EveryNode());
    CheckPath(FindTeamTaxiPath(blind, MARSHALS_REFUGE, RATCHET),
              {MARSHALS_REFUGE, GADGETZAN_HORDE, RATCHET},
              "a faction-blind search reproduces the logged route 79->40->80");

    // What the old scan did with that route, for an Alliance family holding
    // 79, 39 and 80: the first unknown hop was 40, and it was named. The new
    // scan refuses it outright.
    std::vector<uint32_t> const allianceKnown = {MARSHALS_REFUGE, GADGETZAN_ALLIANCE, RATCHET};
    TaxiRouteScan const blindScan =
        ScanTaxiRoute(Read({MARSHALS_REFUGE, GADGETZAN_HORDE, RATCHET}, allianceKnown, true));
    Check(blindScan.verdict == TaxiRouteVerdict::Unflyable,
          "a route through a Horde node is unflyable for the Alliance");
    Check(blindScan.learnNode == 0, "and the scan names no node to learn (learnNode == 0)");

    TeamTaxiGraph const alliance = BuildTeamTaxiGraph(edges, TeamNodes(true));
    std::vector<uint32_t> const route = FindTeamTaxiPath(alliance, MARSHALS_REFUGE, RATCHET);
    CheckPath(route, {}, "the Alliance graph has no route 79->80 without node 40");
    TaxiRouteScan const scan = ScanTaxiRoute(Read(route, allianceKnown, true));
    Check(scan.verdict == TaxiRouteVerdict::Unflyable, "no route is Unflyable");
    Check(scan.learnNode == 0, "no learnable node in reach suggests nothing (learnNode == 0)");

    // The Horde, on the same rows, still flies through its own node.
    TeamTaxiGraph const horde = BuildTeamTaxiGraph(edges, TeamNodes(false));
    CheckPath(FindTeamTaxiPath(horde, MARSHALS_REFUGE, RATCHET),
              {MARSHALS_REFUGE, GADGETZAN_HORDE, RATCHET}, "the Horde route keeps node 40");
}

// WHERE A SAME-SIDE ALTERNATIVE EXISTS THE ROUTE TAKES IT, and the suggestion
// is that node. Both Gadgetzan nodes reach Ratchet here, so a search that
// ignored the team could return either; each side must get its own.
void TheSameSideAlternativeIsPreferredAndSuggested()
{
    std::vector<TaxiEdge> edges;
    Link(edges, MARSHALS_REFUGE, GADGETZAN_HORDE);
    Link(edges, GADGETZAN_HORDE, RATCHET);
    Link(edges, MARSHALS_REFUGE, GADGETZAN_ALLIANCE);
    Link(edges, GADGETZAN_ALLIANCE, RATCHET);

    TeamTaxiGraph const alliance = BuildTeamTaxiGraph(edges, TeamNodes(true));
    std::vector<uint32_t> const route = FindTeamTaxiPath(alliance, MARSHALS_REFUGE, RATCHET);
    CheckPath(route, {MARSHALS_REFUGE, GADGETZAN_ALLIANCE, RATCHET},
              "the Alliance route goes through 39, not 40");

    // An Alliance character that has not yet learned 39 is told to learn 39.
    TaxiRouteScan const learn = ScanTaxiRoute(Read(route, {MARSHALS_REFUGE, RATCHET}, true));
    Check(learn.verdict == TaxiRouteVerdict::LearnHop, "a missing same-side hop is LearnHop");
    Check(learn.learnNode == GADGETZAN_ALLIANCE, "and the node named is 39");

    // One that holds all three flies.
    TaxiRouteScan const fly = ScanTaxiRoute(
        Read(route, {MARSHALS_REFUGE, GADGETZAN_ALLIANCE, RATCHET}, true));
    Check(fly.verdict == TaxiRouteVerdict::Fly && fly.learnNode == 0,
          "a route of known same-side nodes flies");

    // The direct leg the issue names: 79 -> 39 is one hop for the Alliance.
    CheckPath(FindTeamTaxiPath(alliance, MARSHALS_REFUGE, GADGETZAN_ALLIANCE),
              {MARSHALS_REFUGE, GADGETZAN_ALLIANCE}, "79->39 is direct");
    CheckPath(FindTeamTaxiPath(alliance, MARSHALS_REFUGE, GADGETZAN_HORDE), {},
              "and 40 is not a destination the Alliance graph holds");

    // The Horde mirror: same rows, its own node.
    TeamTaxiGraph const horde = BuildTeamTaxiGraph(edges, TeamNodes(false));
    std::vector<uint32_t> const hordeRoute = FindTeamTaxiPath(horde, MARSHALS_REFUGE, RATCHET);
    CheckPath(hordeRoute, {MARSHALS_REFUGE, GADGETZAN_HORDE, RATCHET},
              "the Horde route goes through 40, not 39");
    TaxiRouteScan const hordeLearn =
        ScanTaxiRoute(Read(hordeRoute, {MARSHALS_REFUGE, RATCHET}, false));
    Check(hordeLearn.verdict == TaxiRouteVerdict::LearnHop &&
              hordeLearn.learnNode == GADGETZAN_HORDE,
          "a Horde character missing 40 is told to learn 40");
}

// A LEARNABLE GAP BEFORE AN UNLEARNABLE ONE IS STILL NO SUGGESTION. Learning
// the first would only walk the character to the second dead end.
void AnEarlierLearnableGapDoesNotRescueALaterForeignHop()
{
    std::vector<TaxiHopRead> hops = {
        {MARSHALS_REFUGE, true, true},
        {GADGETZAN_ALLIANCE, false, true},
        {GADGETZAN_HORDE, false, false},
        {RATCHET, true, true},
    };
    TaxiRouteScan const scan = ScanTaxiRoute(hops);
    Check(scan.verdict == TaxiRouteVerdict::Unflyable && scan.learnNode == 0,
          "a foreign hop anywhere on the route condemns it, with nothing named");

    // The departure itself is not required to be known: the flight master
    // teaches it when the window opens.
    std::vector<TaxiHopRead> const fromUnknown = {
        {GADGETZAN_ALLIANCE, false, true},
        {MARSHALS_REFUGE, true, true},
    };
    Check(ScanTaxiRoute(fromUnknown).verdict == TaxiRouteVerdict::Fly,
          "an unknown departure node does not block the route");
    Check(ScanTaxiRoute({}).verdict == TaxiRouteVerdict::Unflyable &&
              ScanTaxiRoute({}).learnNode == 0,
          "an empty route is Unflyable with nothing named");
    CheckPath(FindTeamTaxiPath(BuildTeamTaxiGraph({}, EveryNode()), 39, 39), {},
              "a node to itself is no route");
}

}  // namespace

int main()
{
    EachGadgetzanNodeServesOneSide();
    AnAllianceRouteOnlyThroughHordeNodesSuggestsNothing();
    TheSameSideAlternativeIsPreferredAndSuggested();
    AnEarlierLearnableGapDoesNotRescueALaterForeignHop();

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_flight_route_faction: all checks passed\n");
    return EXIT_SUCCESS;
}
