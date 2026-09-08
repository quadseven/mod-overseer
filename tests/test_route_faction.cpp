/*
 * A route that walks the family into the other side (mod-overseer#326).
 *
 * #316 gave the planner a way round terrain and it worked: watched live, an
 * Alliance family of 26 to 32 closed on the Wailing Caverns door for the first
 * time in this project's history, 2236 yards down to 674. Then it reversed and
 * walked back out to 2451 and into the next zone. The death table says why:
 * `Barrens Guard` and `Horde Guard`, both level 40, twice each, on the same two
 * spots. The party revived at a graveyard behind them, walked the same route,
 * and died in the same place. A hundred run campaign sat at zero.
 *
 * The survey does not know about factions and is not wrong not to. It records
 * where a character can physically WALK, and its legs follow roads, because a
 * road is where somebody walked. Roads are patrolled.
 *
 * THE FIXTURE IS THE REAL SUB-GRAPH, not a shape invented to make the rule
 * fire. Every node id, name and coordinate below was read out of the realm's
 * `playerbots_travelnode` on 2026-09-07 and every link distance out of
 * `playerbots_travelnode_link`; the guarded flags are which of those legs pass
 * within 60 yards of a spawn that is hostile to an Alliance character, at least
 * ten levels above it, and whose own faction group carries the Horde bit,
 * sampled along the survey's own waypoints for the leg. The one marked leg
 * passes within 5 yards of a level 40 `Barrens Guard`, and the party died on it
 * at 03:01 and again at 03:16.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePlanVerdictName;

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

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void CheckVerdict(char const* what, RoutePlanVerdict got, RoutePlanVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RoutePlanVerdictName(got),
                RoutePlanVerdictName(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const slack = 2.0f;   // whole yards, over journeys of thousands
    if (got >= want - slack && got <= want + slack)
        return;
    std::printf("FAIL %s: got %.1f, wanted %.1f\n", what, double(got), double(want));
    ++failures;
}

std::string Sequence(RoutePlan const& plan)
{
    std::string out;
    for (std::uint32_t node : plan.nodes)
    {
        if (!out.empty())
            out += ",";
        out += std::to_string(node);
    }
    return out;
}

void CheckRoute(char const* what, RoutePlan const& plan, char const* want)
{
    std::string const got = Sequence(plan);
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    '%s'\n  wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// ------------------------------------------------------------ the fixture --

constexpr std::uint32_t KALIMDOR = 1;

// The Wailing Caverns staging terrace the family is aimed at, which is the
// literal target string in every row of the death table for this journey.
constexpr float DOOR_X = -705.0f;
constexpr float DOOR_Y = -2045.0f;

// Where the walk that produced the 2236 yard trace began.
constexpr float FROM_X = -251.864f;
constexpr float FROM_Y = 104.318f;

std::vector<RouteNode> Nodes()
{
    return {
        {2745, KALIMDOR,  -90.397f,   259.903f,  96.789f},  // Boulderslide Cavern
        {3486, KALIMDOR, -188.424f,  -347.647f,   8.874f},  // Malakajin
        {3056, KALIMDOR,  -36.532f,  -517.285f, -45.844f},  // Camp Aparaje
        {2510, KALIMDOR, -269.229f, -1229.430f,  74.784f},  // Honors Stand
        {3491, KALIMDOR,   90.275f, -1880.740f,  94.338f},  // The Forgotten Pools
        {3517, KALIMDOR, -390.928f, -2182.540f, 158.680f},  // Shrine of the Fallen Warrior
    };
}

RouteLink Walk(std::uint32_t from, std::uint32_t to, float yards, bool guarded)
{
    RouteLink link;
    link.from = from;
    link.to = to;
    link.yards = yards;
    link.onFoot = true;
    link.guardedGround = guarded;
    return link;
}

// The real rows, including the two places the survey is one-way and the two
// where the distance differs by direction. Only 2510 to 3517 is guarded: it is
// the leg down the Barrens road past the Honors Stand, and it is the one they
// died on.
std::vector<RouteLink> Links(bool mark)
{
    return {
        Walk(2745, 3056, 1123.25f, false), Walk(3056, 2745, 1123.25f, false),
        Walk(2745, 3486, 1016.49f, false), Walk(3486, 2745, 1016.49f, false),
        Walk(3056, 3486,  553.575f, false), Walk(3486, 3056, 449.764f, false),
        Walk(3056, 2510,  971.509f, false),
        Walk(2510, 3486, 1153.58f, false),
        Walk(3486, 3491, 1976.68f, false),
        Walk(3491, 2510,  938.706f, false),
        Walk(2510, 3517, 1499.65f, mark), Walk(3517, 2510, 1499.65f, mark),
    };
}

RoutePlanLimits Limits()
{
    RoutePlanLimits limits;
    limits.entryNodeYards = 600.f;
    limits.minGainYards = 200.f;
    limits.roundHandoverYards = 1000.f;
    return limits;
}

RoutePlan Plan(bool mark, RoutePlanLimits const& limits)
{
    return PlanFootRoute(Nodes(), Links(mark), KALIMDOR, FROM_X, FROM_Y,
                         DOOR_X, DOOR_Y, limits);
}

// ------------------------------------------------------------- the rule ----

// THE CONTROL, AND IT IS FIRST BECAUSE IT IS THE THING THAT MUST NOT MOVE. The
// same graph with nothing marked is the route that shipped: three legs down
// through Camp Aparaje and the Honors Stand to the Shrine, 3594 yards, ending
// 343 yards from the door. Every number in this test is that route's.
void AnUnmarkedGraphIsTheRouteThatShipped()
{
    RoutePlan const plan = Plan(false, Limits());
    CheckVerdict("it plans", plan.verdict, RoutePlanVerdict::Planned);
    CheckRoute("the route is the one #316 shipped", plan, "2745,3056,2510,3517");
    CheckNear("and its yards", plan.yards, 3594.4f);
    CheckNear("and where it hands over", plan.endsFromAimYards, 342.9f);
    Check("nothing was gone round", plan.wentRound, false);
    CheckUInt("and nothing is reported guarded", plan.guardedLegs, 0u);
}

// THE DEFECT. One leg marked, and the planner stops choosing the goal that can
// only be reached across it.
void TheLegTheyDiedOnIsNotWalked()
{
    RoutePlan const plan = Plan(true, Limits());
    CheckVerdict("it still plans", plan.verdict, RoutePlanVerdict::Planned);
    CheckRoute("it goes round by the Forgotten Pools", plan, "2745,3486,3491");
    Check("and says so", plan.wentRound, true);
    CheckUInt("no leg of it is guarded", plan.guardedLegs, 0u);
}

// AND GOING ROUND IS NOT A DETOUR HERE, WHICH IS THE MEASUREMENT THAT MADE THIS
// RULE SHIPPABLE. 2993 yards against 3594: the way round is SHORTER, and it is
// only the leftover that grows.
void TheWayRoundIsShorterThanTheWayThroughThem()
{
    RoutePlan const through = Plan(false, Limits());
    RoutePlan const round = Plan(true, Limits());
    CheckNear("the way round walks fewer yards of leg", round.yards, 2993.2f);
    Check("fewer than the route it replaces", round.yards < through.yards, true);
    CheckNear("it hands over more", round.endsFromAimYards, 812.1f);
    Check("but less ground in total", round.yards + round.endsFromAimYards <
                                          through.yards + through.endsFromAimYards, true);
}

// THE OTHER HALF OF THE RULE, FIRST BRANCH: nothing clean gets near the aim.
// This is the shape the real Wailing Caverns approach takes once the Forgotten
// Pools is out too, and it is the shape of every journey where the answer is
// honestly "there is no way round this". The party gets the route it has today,
// with the count that says what is on it, rather than getting nothing.
void WhenNothingCleanGetsNearTheAimTodaysRouteIsKept()
{
    std::vector<RouteLink> links = Links(true);
    for (RouteLink& link : links)
        if ((link.from == 3486 && link.to == 3491) ||
            (link.from == 3056 && link.to == 2510))
            link.guardedGround = true;
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, Limits());
    CheckVerdict("it still plans rather than refusing", plan.verdict,
                 RoutePlanVerdict::Planned);
    CheckRoute("and it is today's route", plan, "2745,3056,2510,3517");
    Check("nothing was gone round", plan.wentRound, false);
    CheckUInt("and both guarded legs are reported, not hidden", plan.guardedLegs, 2u);
}

// THE OTHER HALF OF THE RULE, SECOND BRANCH: a clean node IS near enough, and
// it is refused on reach alone.
//
// ONE NUMBER IN THIS FIXTURE IS NOT THE REALM'S, and it is said out loud rather
// than buried. On the real graph the way round to the Forgotten Pools is 1976
// yards and is CHEAPER than the guarded route, which is the finding this whole
// issue turns on and is asserted two tests above. That means the real graph
// never exercises the reach comparison for this journey, so this one link is
// lengthened to 2500 yards to reach the branch. Everything else, the node
// coordinates and the other eleven distances, is still the realm's own.
void AWayRoundThatIsFartherIsNotTaken()
{
    std::vector<RouteLink> links = Links(true);
    for (RouteLink& link : links)
    {
        if (link.from == 3486 && link.to == 3491)
            link.yards = 2500.f;
        // And the cheap clean way to the Honors Stand is closed, or that node
        // wins on reach and the branch under test is never taken.
        if (link.from == 3056 && link.to == 2510)
            link.guardedGround = true;
    }
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, Limits());
    // The Forgotten Pools is 812 yards from the door, well inside the handover
    // ceiling, so the ceiling does not refuse it. Reach does: 1016 to Malakajin
    // plus 2500 to the Pools is 3516 yards of leg and 812 left over, which is
    // 4329 against the 3937 the guarded route reaches the door in.
    CheckRoute("the guarded route is kept", plan, "2745,3056,2510,3517");
    Check("nothing was gone round", plan.wentRound, false);
    CheckUInt("and both guarded legs are reported, not hidden", plan.guardedLegs, 2u);
}

// THE CEILING IS LOAD BEARING AND THIS IS WHAT IT HOLDS BACK. At 600 yards the
// Forgotten Pools is out of reach of the rule and the party keeps walking into
// the guards; the fix needs 812. Written as a test so anybody re-tuning that
// number finds out immediately what it costs.
void TheHandoverCeilingDecidesWhetherThereIsAWayRoundAtAll()
{
    RoutePlanLimits tight = Limits();
    tight.roundHandoverYards = 600.f;
    RoutePlan const cramped = Plan(true, tight);
    CheckRoute("under the ceiling the guarded route is kept", cramped,
               "2745,3056,2510,3517");
    CheckUInt("and it is reported guarded", cramped.guardedLegs, 1u);

    RoutePlanLimits loose = Limits();
    loose.roundHandoverYards = 2000.f;
    CheckRoute("and anywhere from 1000 to 2000 gives the same answer",
               Plan(true, loose), "2745,3486,3491");
}

// A MARK ON A LEG NOBODY WALKS CHANGES NOTHING. The guarded flag is a property
// of ground, so most of what carries it is nowhere near any given journey, and
// a planner that noticed would be doing work for every errand in the world.
void AGuardedLegOffTheRouteIsNotOnTheRoute()
{
    std::vector<RouteLink> links = Links(false);
    for (RouteLink& link : links)
        if (link.from == 3491 && link.to == 2510)
            link.guardedGround = true;
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, Limits());
    CheckRoute("the route is untouched", plan, "2745,3056,2510,3517");
    Check("and no second search was worth reporting", plan.wentRound, false);
    CheckUInt("with nothing guarded on it", plan.guardedLegs, 0u);
}

// EVERY LEG MARKED IS THE SAME ANSWER AS NONE MARKED, which is worth asserting
// separately from the case above: it is the shape a party standing inside a
// hostile town reads as, and "there is no clean ground anywhere" must give it
// today's route rather than nothing at all.
void NowhereCleanIsStillARoute()
{
    std::vector<RouteLink> links = Links(true);
    for (RouteLink& link : links)
        link.guardedGround = true;
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, Limits());
    CheckVerdict("it plans", plan.verdict, RoutePlanVerdict::Planned);
    CheckRoute("and it is today's route", plan, "2745,3056,2510,3517");
    Check("nothing was gone round", plan.wentRound, false);
    CheckUInt("and all three of its legs are counted", plan.guardedLegs, 3u);
}

// THE COUNT IS OF THE LEGS THE PARTY WALKS, not of the legs the caller marked.
// A caller logging this is telling an operator how much guarded ground is on
// THIS journey, and a number about the whole world would be useless for that.
void TheCountIsOfTheRouteAndNotOfTheGraph()
{
    std::vector<RouteLink> links = Links(true);
    for (RouteLink& link : links)
        if (link.from == 2745 && link.to == 3056)
            link.guardedGround = true;
    RoutePlanLimits tight = Limits();
    tight.roundHandoverYards = 600.f;   // so no way round is available
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, tight);
    CheckRoute("today's route", plan, "2745,3056,2510,3517");
    CheckUInt("two of its three legs are guarded", plan.guardedLegs, 2u);
}

// A NONSENSE CEILING REFUSES rather than clamping, exactly as the other two
// limits do. A sign typo must not quietly become a rule looser than the one
// written.
void ANonsenseCeilingIsRefusedAndNotClamped()
{
    RoutePlanLimits bad = Limits();
    bad.roundHandoverYards = -1.f;
    CheckVerdict("a negative ceiling refuses", Plan(true, bad).verdict,
                 RoutePlanVerdict::BadLimits);
    RoutePlanLimits zero = Limits();
    zero.roundHandoverYards = 0.f;
    CheckVerdict("and zero is a real answer, not a refusal", Plan(true, zero).verdict,
                 RoutePlanVerdict::Planned);
}

// THE ENTRY NODE IS NOT AN EXCEPTION. A character standing next to a guarded
// leg still gets the search; the flag is about legs, and the first one is a leg
// like any other.
void TheFirstLegIsJudgedLikeAnyOther()
{
    std::vector<RouteLink> links = Links(false);
    for (RouteLink& link : links)
        if (link.from == 2745 && link.to == 3056)
            link.guardedGround = true;
    RoutePlan const plan = PlanFootRoute(Nodes(), links, KALIMDOR, FROM_X, FROM_Y,
                                         DOOR_X, DOOR_Y, Limits());
    CheckRoute("it leaves by the clean leg instead", plan, "2745,3486,3491");
    Check("and says it went round", plan.wentRound, true);
    CheckUInt("with nothing guarded left on it", plan.guardedLegs, 0u);
}

}  // namespace

int main()
{
    AnUnmarkedGraphIsTheRouteThatShipped();
    TheLegTheyDiedOnIsNotWalked();
    TheWayRoundIsShorterThanTheWayThroughThem();
    WhenNothingCleanGetsNearTheAimTodaysRouteIsKept();
    AWayRoundThatIsFartherIsNotTaken();
    TheHandoverCeilingDecidesWhetherThereIsAWayRoundAtAll();
    AGuardedLegOffTheRouteIsNotOnTheRoute();
    NowhereCleanIsStillARoute();
    TheCountIsOfTheRouteAndNotOfTheGraph();
    ANonsenseCeilingIsRefusedAndNotClamped();
    TheFirstLegIsJudgedLikeAnyOther();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a route goes round the other side's ground when going round is not farther\n");
    return 0;
}
