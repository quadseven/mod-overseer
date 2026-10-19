/*
 * Which hostiles make a planned leg guarded ground, and the lone walker's grey
 * density rule (mod-overseer#502).
 *
 * Since #326 a leg was marked only by the other side's people at the `??` gap,
 * so neither of the two deaths #502 measured could be seen:
 *
 *   * Frostmaul Preserver (entry 7429), a level 59 to 60 rank 1 elite that
 *     killed a level 60 walker in real combat. Neutral, so never marked. Option
 *     B, on by default, marks and prices it.
 *   * The Bork case: survey node 3487 in Tanaris, with Soriid the Devourer
 *     (entry 8204, level 50, rank 4, detection range 20) 23 yards off it and a
 *     Centipaar Stinger (level 49, rank 0) 0 yards off it. Every spawn there is
 *     grey to a level 60 walker. Option A, behind
 *     Overseer.Route.LoneGreyDensity and off by default, catches it at N = 2.
 *
 * The distances are the ones measured on #502; the coordinates are a local
 * frame with the node at its origin. Compiled against src/overseer_decisions.cpp
 * and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::ConBand;
using OverseerDecisions::DangerLimits;
using OverseerDecisions::DangerSpawn;
using OverseerDecisions::GroundDanger;
using OverseerDecisions::GuardedGroundRule;
using OverseerDecisions::GuardedGroundSpawn;
using OverseerDecisions::LoneGreyDensity;
using OverseerDecisions::LoneGreyDensityRule;
using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::ScoreGroundDanger;
using OverseerDecisions::ScoreLoneGreyDensity;
using OverseerDecisions::SpawnMarksGuardedGround;
using OverseerDecisions::SpawnPricedOnGuardedGround;

namespace
{

int failures = 0;
int checks = 0;

void Check(char const* what, bool got, bool want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    ++checks;
    if (std::fabs(got - want) <= 0.05f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

constexpr std::uint32_t WALKER = 60;
constexpr std::uint32_t MAP = 1;

GuardedGroundSpawn Spawn(std::uint32_t level, std::uint32_t rank, bool otherSide)
{
    GuardedGroundSpawn spawn;
    spawn.level = level;
    spawn.rank = rank;
    spawn.otherSide = otherSide;
    return spawn;
}

DangerSpawn At(float x, float y, std::uint32_t level, float detectionRange = 20.f)
{
    DangerSpawn spawn;
    spawn.x = x;
    spawn.y = y;
    spawn.level = level;
    spawn.detectionRange = detectionRange;
    return spawn;
}

RoutePoint Point(float x, float y)
{
    RoutePoint point;
    point.x = x;
    point.y = y;
    return point;
}

// Waypoints every `step` yards along y = 0, both ends included.
std::vector<RoutePoint> LegAlongX(float fromX, float toX, float step)
{
    std::vector<RoutePoint> points;
    float const dir = toX >= fromX ? 1.f : -1.f;
    for (float x = fromX; dir * (toX - x) > 0.f; x += dir * step)
        points.push_back(Point(x, 0.f));
    points.push_back(Point(toX, 0.f));
    return points;
}

// ------------------------------------------------- B: neutral elites ------

void FrostmaulPreserverMarksTheLegAndIsPriced()
{
    GuardedGroundRule const rule;
    Check("B is on by default", rule.neutralElites, true);

    // Entry 7429: level 59 to 60, rank 1, neutral. maxlevel is what the sweep
    // reads, and 60 is yellow to a level 60 walker.
    GuardedGroundSpawn const preserver = Spawn(60, 1, false);
    Check("the Frostmaul Preserver marks the leg", SpawnMarksGuardedGround(preserver, WALKER, rule),
          true);
    Check("...and is handed to the scorer", SpawnPricedOnGuardedGround(preserver, rule), true);
    Check("a rare elite (rank 2) marks too",
          SpawnMarksGuardedGround(Spawn(58, 2, false), WALKER, rule), true);

    // Priced at the existing weights: yellow, 1 yard of detour per yard inside
    // its aggro radius. 20 detection, level gap 0, so a 20 yard circle; a leg
    // passing 10 yards off its centre crosses 2 * sqrt(400 - 100) of it.
    std::vector<DangerSpawn> const spawns = {At(0.f, 10.f, 60)};
    std::vector<std::uint32_t> const levels = {WALKER};
    GroundDanger const danger =
        ScoreGroundDanger(spawns, LegAlongX(-100.f, 100.f, 5.f), levels, DangerLimits{});
    Check("...as yellow", danger.worstBand == ConBand::Yellow, true);
    CheckNear("...at one yard of detour per yard inside its reach", danger.detourYards,
              2.f * std::sqrt(300.f));

    // And the old rule is one switch away.
    GuardedGroundRule off;
    off.neutralElites = false;
    Check("with NeutralElites off the Preserver is invisible again",
          SpawnMarksGuardedGround(preserver, WALKER, off), false);
    Check("...and unpriced", SpawnPricedOnGuardedGround(preserver, off), false);
}

void OnlyEliteRanksThatAreNotGreyCount()
{
    GuardedGroundRule const rule;
    // Soriid the Devourer is rank 4, a rare, not a rare elite.
    Check("a rare (rank 4) does not mark", SpawnMarksGuardedGround(Spawn(50, 4, false), WALKER, rule),
          false);
    Check("...and is not priced", SpawnPricedOnGuardedGround(Spawn(50, 4, false), rule), false);
    // The Centipaar hive and the second death's Swarmer are rank 0.
    Check("a normal neutral (rank 0) does not mark, even yellow",
          SpawnMarksGuardedGround(Spawn(60, 0, false), WALKER, rule), false);
    Check("a boss (rank 3) is not what B means",
          SpawnMarksGuardedGround(Spawn(63, 3, false), WALKER, rule), false);
    // GreyLevel(60) is 51: 51 is grey and 52 is green.
    Check("a grey neutral elite does not mark",
          SpawnMarksGuardedGround(Spawn(51, 1, false), WALKER, rule), false);
    Check("a green neutral elite does", SpawnMarksGuardedGround(Spawn(52, 1, false), WALKER, rule),
          true);
}

void TheOtherSideKeepsItsOwnRule()
{
    GuardedGroundRule const rule;
    // Marked at the `??` gap exactly as #326 did: level + 10 and up.
    Check("the other side at level + 9 does not mark",
          SpawnMarksGuardedGround(Spawn(69, 0, true), WALKER, rule), false);
    Check("the other side at level + 10 marks",
          SpawnMarksGuardedGround(Spawn(70, 0, true), WALKER, rule), true);
    Check("...a level 40 guard for a level 28 walker",
          SpawnMarksGuardedGround(Spawn(40, 1, true), 28, rule), true);
    Check("...and is priced at any level", SpawnPricedOnGuardedGround(Spawn(30, 0, true), rule),
          true);
    GuardedGroundRule off;
    off.neutralElites = false;
    Check("...whether or not B is on", SpawnMarksGuardedGround(Spawn(70, 0, true), WALKER, off),
          true);
}

// ------------------------------------------- A: the Bork case, node 3487 --

// Node 3487 at the origin, the leg arriving at it from 500 yards west.
RoutePoint const NODE_3487 = Point(0.f, 0.f);
RoutePoint const WEST_NODE = Point(-500.f, 0.f);

std::vector<DangerSpawn> TheHive()
{
    return {
        At(0.f, 23.f, 50, 20.f),  // Soriid the Devourer, 23 yards off the node
        At(0.f, 0.f, 49, 20.f),   // a Centipaar Stinger, on the node
        At(0.f, 90.f, 49, 20.f),  // one further out: beyond 20 + 5, not counted
    };
}

LoneGreyDensity BorkLeg(std::uint32_t threshold, bool hasGroup)
{
    LoneGreyDensityRule rule;
    rule.threshold = threshold;
    return ScoreLoneGreyDensity(TheHive(), WEST_NODE, NODE_3487,
                                LegAlongX(-500.f, 0.f, 5.f), hasGroup, rule, DangerLimits{});
}

void TheBorkSpawnsAreInvisibleToB()
{
    GuardedGroundRule const rule;
    Check("Soriid does not mark under B", SpawnMarksGuardedGround(Spawn(50, 4, false), WALKER, rule),
          false);
    Check("the Stinger does not mark under B",
          SpawnMarksGuardedGround(Spawn(49, 0, false), WALKER, rule), false);
    // And grey prices at nothing under #400, which is why B cannot reach it.
    std::vector<std::uint32_t> const levels = {WALKER};
    GroundDanger const danger =
        ScoreGroundDanger(TheHive(), LegAlongX(-500.f, 0.f, 5.f), levels, DangerLimits{});
    CheckNear("...and grey prices at zero under #400", danger.detourYards, 0.f);
}

void ALoneWalkerAtTwoSeesTheHive()
{
    LoneGreyDensityRule const defaults;
    CheckUInt("A is off by default", defaults.threshold, 0);
    CheckNear("...and its pad is five yards", defaults.reachPadYards, 5.f);

    LoneGreyDensity const at2 = BorkLeg(2, false);
    Check("at N = 2 the leg into node 3487 is guarded", at2.guarded, true);
    CheckUInt("...by Soriid and the Stinger, not the one 90 yards out", at2.spawns, 2);
    // The Stinger's 25 yard reach covers the last 25 yards of the leg; Soriid's
    // covers the last sqrt(25^2 - 23^2) of it. Yellow weight, one for one.
    float const want = 25.f + std::sqrt(625.f - 529.f);
    CheckNear("...exposed for the yards inside those reaches", at2.exposedYards, want);
    CheckNear("...and priced at the yellow weight", at2.detourYards, want);
}

void AOffOrAPartyOrTooFewChangesNothing()
{
    LoneGreyDensity const off = BorkLeg(0, false);
    Check("at 0 the rule is off", off.guarded, false);
    CheckNear("...and prices nothing", off.detourYards, 0.f);

    LoneGreyDensity const party = BorkLeg(2, true);
    Check("a walker with a group is never asked", party.guarded, false);
    CheckNear("...and pays nothing", party.detourYards, 0.f);

    LoneGreyDensity const three = BorkLeg(3, false);
    Check("two spawns do not meet N = 3", three.guarded, false);
    CheckUInt("...though the count says how close it came", three.spawns, 2);
    CheckNear("...and nothing is priced", three.detourYards, 0.f);

    // A spawn that cannot start a fight is not counted, whatever stands near.
    std::vector<DangerSpawn> hive = TheHive();
    hive[1].civilian = true;
    LoneGreyDensityRule rule;
    rule.threshold = 2;
    LoneGreyDensity const civil = ScoreLoneGreyDensity(
        hive, WEST_NODE, NODE_3487, LegAlongX(-500.f, 0.f, 5.f), false, rule, DangerLimits{});
    Check("a civilian on the node does not count", civil.guarded, false);
}

// -------------------- end to end: the plan with A at 0 and at 2 ------------

// From the west node to an aim 500 yards east of node 3487. Through 3487 is
// 1000 yards; round it to the north is 1020, twenty yards longer.
std::vector<RouteNode> BorkGraph()
{
    return {
        {1, MAP, -500.f, 0.f, 0.f},   // the west node, where the walker stands
        {3487, MAP, 0.f, 0.f, 0.f},   // the Noxious Lair node
        {3, MAP, 500.f, 0.f, 0.f},    // the aim
        {4, MAP, 0.f, 100.f, 0.f},    // round, north
    };
}

RouteLink Walk(std::uint32_t from, std::uint32_t to, float yards)
{
    RouteLink link;
    link.from = from;
    link.to = to;
    link.yards = yards;
    link.onFoot = true;
    return link;
}

RoutePlan PlanBork(std::uint32_t threshold)
{
    std::vector<RouteLink> links = {
        Walk(1, 3487, 500.f), Walk(3487, 3, 500.f),
        Walk(1, 4, 510.f), Walk(4, 3, 510.f),
    };
    LoneGreyDensityRule rule;
    rule.threshold = threshold;
    // Each leg touching node 3487 measured exactly as the adapter does it.
    LoneGreyDensity const in = ScoreLoneGreyDensity(
        TheHive(), WEST_NODE, NODE_3487, LegAlongX(-500.f, 0.f, 5.f), false, rule, DangerLimits{});
    LoneGreyDensity const out = ScoreLoneGreyDensity(
        TheHive(), NODE_3487, Point(500.f, 0.f), LegAlongX(0.f, 500.f, 5.f), false, rule,
        DangerLimits{});
    links[0].guardedGround = in.guarded;
    links[0].detourWorthYards = in.detourYards;
    links[1].guardedGround = out.guarded;
    links[1].detourWorthYards = out.detourYards;
    return PlanFootRoute(BorkGraph(), links, MAP, -500.f, 0.f, 500.f, 0.f, RoutePlanLimits{});
}

void WithAAtZeroThePlanIsTodays()
{
    RoutePlan const plan = PlanBork(0);
    Check("at 0 the walk still plans", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("...through node 3487, as today", plan.wentRound, false);
    CheckUInt("...with no guarded leg", plan.guardedLegs, 0);
    CheckNear("...the short way", plan.yards, 1000.f);
}

void WithAAtTwoTheWalkGoesRound()
{
    RoutePlan const plan = PlanBork(2);
    Check("at 2 the walk plans", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("...and goes round the hive", plan.wentRound, true);
    CheckUInt("...with no guarded leg left on it", plan.guardedLegs, 0);
    CheckNear("...twenty yards longer", plan.yards, 1020.f);
}

}  // namespace

int main()
{
    FrostmaulPreserverMarksTheLegAndIsPriced();
    OnlyEliteRanksThatAreNotGreyCount();
    TheOtherSideKeepsItsOwnRule();
    TheBorkSpawnsAreInvisibleToB();
    ALoneWalkerAtTwoSeesTheHive();
    AOffOrAPartyOrTooFewChangesNothing();
    WithAAtZeroThePlanIsTodays();
    WithAAtTwoTheWalkGoesRound();

    if (failures)
    {
        std::printf("%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    std::printf("route guarded population: all %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
