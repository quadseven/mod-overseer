/*
 * A staging walk that is measured rather than routed (mod-overseer#342).
 *
 * #326 gave the route planner a faction reading and it is correct: the legs it
 * marks really are patrolled by the other side, at least ten levels over this
 * family. What it does about them is the defect. When every surveyed way to a
 * door runs down the patrolled road, the planner reports the guarded legs and
 * walks them, and six dungeon runs in a row ended `staging_failed` with the
 * leader dead on that road at level 30 against level 40 guards.
 *
 * A safe way exists and the survey graph cannot hold it, because its nodes
 * follow roads. So the way is written down instead, and this file is the rule
 * for using one.
 *
 * AND IT IS USED IN BOTH DIRECTIONS (mod-overseer#356). The first draft of the
 * rule refused any walk whose aim lay behind where the character would join,
 * which sounds like a small corner and was not: this campaign's inn is the
 * corridor's own first point, so the errand that corrects a wrongly bound
 * member aims at index 0 and was refused for everybody not already standing on
 * it. Over one ten minute window on the dev realm the corridor was refused five
 * times and used none; over the 72 minutes after that deploy, 8 of 11 deaths
 * were faction guards on the road it fell back to. The tests below marked #356
 * are the walks that were being refused.
 *
 * THE FIXTURE IS THE REAL CORRIDOR, not a shape invented to make the rule fire.
 * Every point below was read off the shipped navmesh tiles for map 1 - grids
 * 33/34 through 33/39 - by walking their polygon adjacency, and the clearances
 * quoted beside them are the distance to the nearest of the 458 spawns on that
 * map between x -2000..3000 and y -5000..-1500 whose template faction is
 * hostile to this family, read out of the world database on 2026-09-08.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::PlanStagingCorridor;
using OverseerDecisions::RouteCursor;
using OverseerDecisions::RouteAim;
using OverseerDecisions::RouteLegLimits;
using OverseerDecisions::RouteLegStep;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::StagingCorridorLimits;
using OverseerDecisions::StagingCorridorPlan;
using OverseerDecisions::StagingCorridorVerdict;
using OverseerDecisions::StagingCorridorVerdictName;

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

void CheckVerdict(char const* what, StagingCorridorVerdict got,
                  StagingCorridorVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what,
                StagingCorridorVerdictName(got), StagingCorridorVerdictName(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want, float slack)
{
    if (got >= want - slack && got <= want + slack)
        return;
    std::printf("FAIL %s: got %.1f, wanted %.1f\n", what, double(got), double(want));
    ++failures;
}

// ------------------------------------------------------------ the fixture --

// THE TWO AIMS OF ONE APPROACH. A door with a measured descent is walked at
// twice: at the top of the descent first, and at the staging point second. Both
// stand on the corridor below, which is the whole reason one written corridor
// can serve them (#344).
constexpr float TERRACE_X = -705.0f;
constexpr float TERRACE_Y = -2045.0f;
// Derived rather than chosen: 20 yards off areatrigger 228 along the bearing to
// where areatrigger 226 lands.
constexpr float STAGE_X = -733.7098f;
constexpr float STAGE_Y = -2214.9101f;

// The corridor as shipped, seventeen points, in walking order. The clearance
// after each is the distance from that point to the nearest hostile spawn of
// any kind; the widest gap between two of them is 193 yards.
std::vector<RoutePoint> Corridor()
{
    return {
        {-1050.0f, -3664.8f,  24.36f},   // the leader's bind, 588 yards clear
        { -927.3f, -3639.4f,  14.85f},   // 489
        { -906.7f, -3466.7f,  68.27f},   // 348
        { -864.0f, -3317.3f,  91.87f},   // 237
        { -864.0f, -3125.3f,  93.91f},   // 230
        { -864.0f, -2933.3f,  92.05f},   // 327
        { -885.3f, -2762.7f,  93.96f},   // 330
        { -840.6f, -2613.8f,  91.77f},   // 274
        { -842.7f, -2421.3f,  92.21f},   // 307
        { -842.7f, -2229.3f,  92.12f},   // 404
        { -847.2f, -2050.8f,  83.75f},   // 540
        { -705.0f, -2045.0f,  66.45f},   // the terrace, 579 - the FIRST aim
        { -627.4f, -2030.2f,  64.96f},   // and then the descent: east, 549
        { -577.5f, -2088.4f,  51.50f},   // south, 474
        { -603.8f, -2162.3f,  52.61f},   // south again, 424
        { -665.2f, -2184.3f,  38.82f},   // west along the ravine, 439
        { -733.7f, -2214.9f,  17.30f},   // the staging point, 445 - the SECOND
    };
}

// Where the leader stands when a run opens: his own bind, which is the
// corridor's first point.
constexpr float BIND_X = -1050.04f;
constexpr float BIND_Y = -3664.8f;

// ---------------------------------------------------------------- the rules --

// The case this exists for. A leader at his bind joins at the mouth and gets the
// whole corridor, and nothing about the survey graph was consulted to do it.
void TheLeaderAtHisBindWalksTheWholeCorridor()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, limits);

    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);
    CheckUInt("at its first point", static_cast<unsigned>(plan.joinIndex), 0u);
    CheckNear("which is where he already stands", plan.joinYards, 0.f, 1.f);
    CheckUInt("and the route runs to the aim and stops there",
              static_cast<unsigned>(plan.route.size()), 12u);
    CheckUInt("which is the terrace", static_cast<unsigned>(plan.endIndex), 11u);
    CheckNear("its widest leg is under one lookahead", plan.longestLegYards, 193.f, 2.f);
}

// The corridor is indexed by where it ENDS, so it cannot be used for a walk
// somewhere else. This is the whole of what stops measured ground being borrowed
// for ground nobody measured.
void ACorridorIsOnlyForTheWalkItWasMeasuredFor()
{
    StagingCorridorLimits const limits;
    // The Deadmines staging point, on the other continent entirely. A real
    // place, and nowhere on this corridor.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), -11208.2f, 1665.34f, BIND_X, BIND_Y, limits);
    CheckVerdict("a walk that goes nowhere on it is not this corridor's",
                 plan.verdict, StagingCorridorVerdict::NotThisAim);
    CheckUInt("and nothing is offered to walk",
              static_cast<unsigned>(plan.route.size()), 0u);
}

// THE ONE THE FIRST DRAFT GOT WRONG. The second aim of the same approach is the
// staging point at the bottom of the ravine, and it stands on the same written
// corridor, so the descent is what gets walked rather than a bearing off a rim.
void TheSecondAimOfTheApproachWalksTheDescent()
{
    StagingCorridorLimits const limits;
    // The leader has arrived at the terrace and been handed the staging point.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), STAGE_X, STAGE_Y, TERRACE_X, TERRACE_Y, limits);
    CheckVerdict("the same corridor serves the second aim", plan.verdict,
                 StagingCorridorVerdict::Joined);
    CheckUInt("joined at the terrace", static_cast<unsigned>(plan.joinIndex), 11u);
    CheckUInt("and ending at the staging point",
              static_cast<unsigned>(plan.endIndex), 16u);
    CheckUInt("which is the five legs of the descent",
              static_cast<unsigned>(plan.route.size()), 6u);
    Check("the first of them goes EAST, away from the door, which is the whole "
          "reason a bearing cannot find it",
          plan.route[1].x > plan.route[0].x && STAGE_X < TERRACE_X, true);
}

// ------------------------------------------ and it is walked both ways (#356) --

// THE ONE THAT WAS REFUSED AND SHOULD NOT HAVE BEEN. Standing at the door and
// asked for the terrace is the walk the coordinator's own note calls being
// "walked out and round": out of the ravine floor and back to the top of the
// corridor, which is where the next run's approach starts and where a member
// that resurrected at the graveyard has to be escorted from. It used to be
// refused for being an aim behind the join, and the road taken instead is the
// one the party dies on.
void TheWalkBackOutOfTheRavineIsTheCorridorToo()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, STAGE_X, STAGE_Y, limits);
    CheckVerdict("the corridor serves the walk back up it", plan.verdict,
                 StagingCorridorVerdict::Joined);
    Check("and says it is running against the order it was written in",
          plan.reversed, true);
    CheckUInt("joined at the staging point", static_cast<unsigned>(plan.joinIndex), 16u);
    CheckUInt("and ending at the terrace", static_cast<unsigned>(plan.endIndex), 11u);
    CheckUInt("which is the five legs of the descent climbed",
              static_cast<unsigned>(plan.route.size()), 6u);
    Check("the route begins where the character stands",
          plan.route[0].x == -733.7f && plan.route[0].y == -2214.9f, true);
    Check("and ends on the point the aim stands on",
          plan.route[5].x == TERRACE_X && plan.route[5].y == TERRACE_Y, true);
}

// THE ONE THAT BLOCKED THE CAMPAIGN (#356). The town this campaign binds in is
// the corridor's own FIRST point, so the walk that corrects a wrongly bound
// member (#349) has its aim at index 0 and every character not already standing
// on it read a join past the aim. The refusal was therefore unconditional for
// that errand: the bind was never corrected, the coordinator held the next run
// waiting for it, and the walk it fell back to killed two members at 188 and
// 379 yards from the innkeeper.
void TheWalkToTheCampaignsInnIsTheCorridorRunBackwards()
{
    StagingCorridorLimits const limits;
    // Standing at the door, sent to the inn at the corridor's mouth.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), BIND_X, BIND_Y, STAGE_X, STAGE_Y, limits);
    CheckVerdict("the inn is on this corridor and the corridor is used",
                 plan.verdict, StagingCorridorVerdict::Joined);
    Check("walked against the order it was written in", plan.reversed, true);
    CheckUInt("joined at the staging point", static_cast<unsigned>(plan.joinIndex), 16u);
    CheckUInt("and ending at the inn", static_cast<unsigned>(plan.endIndex), 0u);
    CheckUInt("which is the whole corridor, every point of it",
              static_cast<unsigned>(plan.route.size()), 17u);
    Check("its first point is the door end", plan.route[0].y == -2214.9f, true);
    Check("and its last is the inn", plan.route[16].x == -1050.0f, true);
    CheckNear("the widest leg is the same one either way", plan.longestLegYards,
              193.f, 2.f);
}

// AND THE DIRECTION IS NOT A NEW WAY TO BORROW SOMEBODY ELSE'S GROUND. The two
// bounds that decide whether this corridor is this walk's corridor at all are
// asked before the direction is, and they are unchanged: an aim that stands
// nowhere on it is still NotThisAim however near the character is, and a
// character too far off it is still TooFarToJoin however the aim lies.
void WalkingItBackwardsStillObeysBothBounds()
{
    StagingCorridorLimits const limits;
    // At the door, aimed at a place on another continent that is behind
    // nothing, because it is not on this corridor at all.
    CheckVerdict("a walk that goes nowhere on it is still not this corridor's",
                 PlanStagingCorridor(Corridor(), -11208.2f, 1665.34f, STAGE_X, STAGE_Y,
                                     limits).verdict,
                 StagingCorridorVerdict::NotThisAim);
    // At the guard post, aimed BACK at the inn. The aim is on the corridor and
    // the character is 540 yards off it, so the join hop is what refuses.
    CheckVerdict("and a character nowhere near it still does not join it backwards",
                 PlanStagingCorridor(Corridor(), BIND_X, BIND_Y, -360.0f, -2634.0f,
                                     limits).verdict,
                 StagingCorridorVerdict::TooFarToJoin);
}

// STANDING ON THE AIM IS NEITHER DIRECTION, and it must not become one. A join
// index equal to the end index is one point of route, which RouteLegStep
// answers `arrived` for, and `reversed` says false because nothing was walked.
void StandingOnTheAimIsOnePointAndNoDirection()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, TERRACE_X, TERRACE_Y,
                            limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);
    Check("and no direction is claimed", plan.reversed, false);
    CheckUInt("the route is the one point", static_cast<unsigned>(plan.route.size()), 1u);
    CheckUInt("which is both the join and the end",
              static_cast<unsigned>(plan.joinIndex), 11u);
    CheckUInt("the same point", static_cast<unsigned>(plan.endIndex), 11u);
}

// Three of the four doors in the portal table carry no corridor, and that is the
// right answer for them: they stage successfully without one.
void NoCorridorIsAnAnswerAndNotAnError()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor({}, TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, limits);
    CheckVerdict("an empty corridor says so plainly", plan.verdict,
                 StagingCorridorVerdict::NoCorridor);
    CheckNear("and reports no join", plan.joinYards, -1.f, 0.01f);
}

// A leader who set out, was interrupted, and set out again rejoins where he
// stands rather than being sent back to the mouth.
void AnInterruptedWalkRejoinsWhereItStopped()
{
    StagingCorridorLimits const limits;
    // Thirty yards off the eighth point, which is the eighth leg of the walk.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, -820.0f, -2613.0f, limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);
    CheckUInt("at the point he is nearest", static_cast<unsigned>(plan.joinIndex), 7u);
    CheckUInt("and the route is the rest of the way to the aim",
              static_cast<unsigned>(plan.route.size()), 5u);
    Check("beginning at that point, because the cursor only moves forward",
          plan.route[0].x == -840.6f && plan.route[0].y == -2613.8f, true);
}

// The join hop is the one stretch of a corridor walk that nobody measured, so it
// is bounded. Past the bound the honest answer is "not this character's
// corridor" and today's routing stands.
void ACharacterNowhereNearItDoesNotJoinIt()
{
    StagingCorridorLimits const limits;
    // The guard post itself, which is where the survey route puts the party and
    // is 540 yards from the nearest corridor point.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, -360.0f, -2634.0f, limits);
    CheckVerdict("too far off to join", plan.verdict,
                 StagingCorridorVerdict::TooFarToJoin);
    CheckUInt("and nothing is offered to walk",
              static_cast<unsigned>(plan.route.size()), 0u);
}

// ...and the bound is a bound, not a suggestion: widen it and the same character
// joins.
void TheJoinBoundIsTheOnlyThingRefusingHim()
{
    StagingCorridorLimits limits;
    limits.joinYards = 600.f;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, -360.0f, -2634.0f, limits);
    CheckVerdict("a wider bound joins him", plan.verdict,
                 StagingCorridorVerdict::Joined);
    CheckUInt("at the point nearest the guard post",
              static_cast<unsigned>(plan.joinIndex), 7u);
}

// The deadlock this rule exists to refuse. Two points more than one lookahead
// apart cannot be walked between, because RouteLegStep would hand the character
// its own feet every poll - so it is named rather than walked into.
void PointsMoreThanOneLookaheadApartAreRefused()
{
    std::vector<RoutePoint> sparse = {
        {-1050.0f, -3664.8f, 24.36f},
        { -864.0f, -3317.3f, 91.87f},   // 393 yards on, over the lookahead
        { -705.0f, -2045.0f, 66.45f},
    };
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(sparse, TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, limits);
    CheckVerdict("a corridor with a gap in it is refused", plan.verdict,
                 StagingCorridorVerdict::LegTooLong);
    CheckNear("and the gap that did it is reported", plan.longestLegYards, 1284.f, 3.f);
    CheckUInt("with nothing offered to walk",
              static_cast<unsigned>(plan.route.size()), 0u);
}

// And it is asked of the WHOLE corridor, not only of the part being walked, so a
// malformed table fails wherever a character joins it.
void TheWholeCorridorIsMeasuredAndNotOnlyTheTail()
{
    std::vector<RoutePoint> corridor = Corridor();
    // Break the second leg, a long way behind a character standing at the end.
    corridor[1].y = -3200.0f;
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(corridor, TERRACE_X, TERRACE_Y, -847.2f, -2050.8f, limits);
    CheckVerdict("a break behind the join is still a break", plan.verdict,
                 StagingCorridorVerdict::LegTooLong);
}

// Nonsense limits refuse rather than clamp, for the reason PlanFootRoute already
// refuses them: a sign typo must not quietly become a looser rule.
void NonsenseLimitsAreRefusedAndNotClamped()
{
    StagingCorridorLimits limits;
    limits.joinYards = -1.f;
    CheckVerdict("a negative join bound is refused",
                 PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, limits).verdict,
                 StagingCorridorVerdict::BadLimits);

    StagingCorridorLimits second;
    second.maxLegYards = 0.f;
    CheckVerdict("a leg bound of nothing is refused",
                 PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, second).verdict,
                 StagingCorridorVerdict::BadLimits);
}

// THE ONE THAT PROVES THE CORRIDOR IS WALKABLE BY THE THING THAT WALKS ROUTES.
// A corridor is only useful if RouteLegStep can actually get a character along
// it, and the failure mode being guarded against is silent: an aim that never
// advances looks exactly like a character standing still.
// Walks a route the way the stepper does - straight at whatever it is aimed
// at, sixty yards a poll, which is one TRAVEL_STEP_YARDS - and reports what
// happened. `worstAim` is the furthest the aim was ever placed from the
// character, which is the number that decides whether the navmesh can answer.
struct Walked
{
    bool arrived{false};
    unsigned polls{0};
    unsigned advanced{0};
    float worstAim{0.f};
    bool everyPollHadAnAim{true};
};

Walked Walk(std::vector<RoutePoint> const& route, RouteLegLimits const& legs,
            float x, float y)
{
    Walked out;
    RouteCursor cursor;
    for (; out.polls < 400; ++out.polls)
    {
        RouteAim const aim = RouteLegStep(cursor, route, x, y, legs);
        if (aim.arrived)
        {
            out.arrived = true;
            break;
        }
        if (!aim.hasAim)
        {
            out.everyPollHadAnAim = false;
            break;
        }
        float const dx = aim.x - x;
        float const dy = aim.y - y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d > out.worstAim)
            out.worstAim = d;
        if (d <= 0.5f)
            continue;   // handed its own feet: the deadlock, and it must not happen
        ++out.advanced;
        float const step = d < 60.f ? d : 60.f;
        x += dx / d * step;
        y += dy / d * step;
    }
    return out;
}

// THE ONE THAT MATTERS MOST, because its failure mode is silent: an aim that
// never advances looks exactly like a character standing still.
void TheCorridorIsWalkedEndToEndByTheOrdinaryRouteStep()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), TERRACE_X, TERRACE_Y, BIND_X, BIND_Y, limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);

    RouteLegLimits legs;
    legs.maxPointsAhead = 1;   // what the travel layer passes for a measured route
    Walked const w = Walk(plan.route, legs, BIND_X, BIND_Y);
    Check("every poll of a joined corridor has an aim", w.everyPollHadAnAim, true);
    Check("the approach is walked to its end", w.arrived, true);
    CheckUInt("and every poll of it moved the character", w.advanced, w.polls);
    Check("in fewer polls than the staging window allows", w.polls < 100, true);
}

// AND THE DESCENT IS WALKED RATHER THAN CUT ACROSS. This is the rule the whole
// of #344 turns on. The descent leaves the terrace EAST and comes back WEST
// along the ravine floor, so its 465 yards of walking cover 172 yards of
// straight line and every one of its points lies within one lookahead of the
// start. Left to the lookahead alone the aim jumps to the bottom of the ravine
// on the first poll, and that aim is 465 yards of navmesh - past the 296 yards
// PathGenerator will smooth, so it comes back as a refused shortcut and the
// party stands on the rim.
void TheDescentIsWalkedPointByPointAndNotCutAcross()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), STAGE_X, STAGE_Y, TERRACE_X, TERRACE_Y, limits);
    CheckVerdict("the descent is joined", plan.verdict, StagingCorridorVerdict::Joined);

    RouteLegLimits bounded;
    bounded.maxPointsAhead = 1;
    Walked const good = Walk(plan.route, bounded, TERRACE_X, TERRACE_Y);
    Check("point by point, the descent is walked to the bottom", good.arrived, true);
    Check("and no aim on it is ever further than one leg",
          good.worstAim < 100.f, true);

    // The same route with the ordinary lookahead, which is what shipped first.
    RouteLegLimits unbounded;
    Walked const bad = Walk(plan.route, unbounded, TERRACE_X, TERRACE_Y);
    Check("the ordinary lookahead aims across the ravine instead",
          bad.worstAim > 150.f, true);
    Check("which is an aim the navmesh cannot answer, and is the bug",
          bad.worstAim > good.worstAim, true);
}

// A REVERSED ROUTE IS STILL WALKED BY THE THING THAT WALKS ROUTES (#356), and
// this is the assertion that matters most about it for the same reason the
// forward one does: an aim that never advances looks exactly like a character
// standing still. RouteLegStep's cursor only ever moves forward THROUGH THE
// ROUTE IT IS HANDED, which is exactly why the points are handed to it already
// in the order they are to be walked rather than with a direction flag it would
// have to read.
void TheReversedCorridorIsWalkedEndToEndByTheOrdinaryRouteStep()
{
    StagingCorridorLimits const limits;
    // The whole corridor backwards: standing at the door, sent to the inn.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), BIND_X, BIND_Y, STAGE_X, STAGE_Y, limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);

    RouteLegLimits legs;
    legs.maxPointsAhead = 1;   // what the travel layer passes for a measured route
    Walked const w = Walk(plan.route, legs, STAGE_X, STAGE_Y);
    Check("every poll of it has an aim", w.everyPollHadAnAim, true);
    Check("the inn is reached", w.arrived, true);
    CheckUInt("and every poll of it moved the character", w.advanced, w.polls);
    Check("no aim on it is further off than one written leg",
          w.worstAim < 250.f, true);
    Check("in fewer polls than the staging window allows", w.polls < 100, true);
}

// The bound is off by default, so every route that is not a measured corridor
// keeps the aim it has always had.
void ASurveyedRouteKeepsItsLookahead()
{
    // Five points ten yards apart, the way the survey stores a leg.
    std::vector<RoutePoint> leg;
    for (int i = 0; i < 5; ++i)
        leg.push_back(RoutePoint{0.f, float(i) * 10.f, 0.f});

    RouteCursor cursor;
    RouteLegLimits legs;   // maxPointsAhead left at zero
    RouteAim const aim = RouteLegStep(cursor, leg, 0.f, 0.f, legs);
    Check("the aim is still the furthest point inside the lookahead", aim.hasAim, true);
    CheckUInt("which is the last of them", aim.index, 4u);

    RouteLegLimits bounded;
    bounded.maxPointsAhead = 1;
    RouteCursor second;
    RouteAim const near = RouteLegStep(second, leg, 0.f, 0.f, bounded);
    CheckUInt("and one point ahead is the next one", near.index, 1u);
}

}  // namespace

int main()
{
    TheLeaderAtHisBindWalksTheWholeCorridor();
    ACorridorIsOnlyForTheWalkItWasMeasuredFor();
    NoCorridorIsAnAnswerAndNotAnError();
    AnInterruptedWalkRejoinsWhereItStopped();
    ACharacterNowhereNearItDoesNotJoinIt();
    TheJoinBoundIsTheOnlyThingRefusingHim();
    PointsMoreThanOneLookaheadApartAreRefused();
    TheWholeCorridorIsMeasuredAndNotOnlyTheTail();
    NonsenseLimitsAreRefusedAndNotClamped();
    TheSecondAimOfTheApproachWalksTheDescent();
    TheWalkBackOutOfTheRavineIsTheCorridorToo();
    TheWalkToTheCampaignsInnIsTheCorridorRunBackwards();
    WalkingItBackwardsStillObeysBothBounds();
    StandingOnTheAimIsOnePointAndNoDirection();
    TheCorridorIsWalkedEndToEndByTheOrdinaryRouteStep();
    TheDescentIsWalkedPointByPointAndNotCutAcross();
    TheReversedCorridorIsWalkedEndToEndByTheOrdinaryRouteStep();
    ASurveyedRouteKeepsItsLookahead();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a measured staging corridor is walked instead of a route through a guard post\n");
    return 0;
}
