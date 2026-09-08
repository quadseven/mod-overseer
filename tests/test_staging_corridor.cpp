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

// The place the campaign already aims a staging walk at: the approach terrace
// this module's portal table has carried since #242. The corridor below is how
// to get there without crossing the guard post, and it ends ON it.
constexpr float AIM_X = -705.0f;
constexpr float AIM_Y = -2045.0f;

// The corridor as shipped, twelve points, in walking order. The clearance after
// each is the distance from that point to the nearest hostile spawn of any
// kind; the widest gap between two of them is 193 yards.
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
        { -705.0f, -2045.0f,  66.45f},   // the terrace itself, 579
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
        PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, BIND_X, BIND_Y, limits);

    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);
    CheckUInt("at its first point", static_cast<unsigned>(plan.joinIndex), 0u);
    CheckNear("which is where he already stands", plan.joinYards, 0.f, 1.f);
    CheckUInt("and the whole of it is the route",
              static_cast<unsigned>(plan.route.size()), 12u);
    CheckNear("its widest leg is under one lookahead", plan.longestLegYards, 193.f, 2.f);
}

// The corridor is indexed by where it ENDS, so it cannot be used for a walk
// somewhere else. This is the whole of what stops measured ground being borrowed
// for ground nobody measured.
void ACorridorIsOnlyForTheWalkItWasMeasuredFor()
{
    StagingCorridorLimits const limits;
    // The dungeon's own staging point, a couple of hundred yards past the
    // terrace and down a ravine. A real place, and not this corridor's.
    StagingCorridorPlan const plan =
        PlanStagingCorridor(Corridor(), -733.71f, -2214.91f, BIND_X, BIND_Y, limits);
    CheckVerdict("a corridor that ends elsewhere is not this walk's",
                 plan.verdict, StagingCorridorVerdict::NotThisAim);
    CheckUInt("and nothing is offered to walk",
              static_cast<unsigned>(plan.route.size()), 0u);
}

// Three of the four doors in the portal table carry no corridor, and that is the
// right answer for them: they stage successfully without one.
void NoCorridorIsAnAnswerAndNotAnError()
{
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor({}, AIM_X, AIM_Y, BIND_X, BIND_Y, limits);
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
        PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, -820.0f, -2613.0f, limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);
    CheckUInt("at the point he is nearest", static_cast<unsigned>(plan.joinIndex), 7u);
    CheckUInt("and the route is the rest of it",
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
        PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, -360.0f, -2634.0f, limits);
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
        PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, -360.0f, -2634.0f, limits);
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
        PlanStagingCorridor(sparse, AIM_X, AIM_Y, BIND_X, BIND_Y, limits);
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
        PlanStagingCorridor(corridor, AIM_X, AIM_Y, -847.2f, -2050.8f, limits);
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
                 PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, BIND_X, BIND_Y, limits).verdict,
                 StagingCorridorVerdict::BadLimits);

    StagingCorridorLimits second;
    second.maxLegYards = 0.f;
    CheckVerdict("a leg bound of nothing is refused",
                 PlanStagingCorridor(Corridor(), AIM_X, AIM_Y, BIND_X, BIND_Y, second).verdict,
                 StagingCorridorVerdict::BadLimits);
}

// THE ONE THAT PROVES THE CORRIDOR IS WALKABLE BY THE THING THAT WALKS ROUTES.
// A corridor is only useful if RouteLegStep can actually get a character along
// it, and the failure mode being guarded against is silent: an aim that never
// advances looks exactly like a character standing still.
void TheCorridorIsWalkedEndToEndByTheOrdinaryRouteStep()
{
    std::vector<RoutePoint> const corridor = Corridor();
    StagingCorridorLimits const limits;
    StagingCorridorPlan const plan =
        PlanStagingCorridor(corridor, AIM_X, AIM_Y, BIND_X, BIND_Y, limits);
    CheckVerdict("the corridor is joined", plan.verdict, StagingCorridorVerdict::Joined);

    RouteLegLimits legs;   // the module's own 250 yard lookahead and 12 yard arrival
    RouteCursor cursor;
    float x = BIND_X;
    float y = BIND_Y;
    unsigned polls = 0;
    unsigned advanced = 0;
    bool arrived = false;
    // Walked as the stepper walks: straight at whatever it is aimed at, sixty
    // yards a poll, which is one TRAVEL_STEP_YARDS.
    for (; polls < 400; ++polls)
    {
        RouteAim const aim = RouteLegStep(cursor, plan.route, x, y, legs);
        if (aim.arrived)
        {
            arrived = true;
            break;
        }
        Check("every poll of a joined corridor has an aim", aim.hasAim, true);
        if (!aim.hasAim)
            break;
        float const dx = aim.x - x;
        float const dy = aim.y - y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d <= 0.5f)
            continue;   // handed its own feet: the deadlock, and it must not happen
        ++advanced;
        float const step = d < 60.f ? d : 60.f;
        x += dx / d * step;
        y += dy / d * step;
    }
    Check("the corridor is walked to its end", arrived, true);
    CheckUInt("and every poll of it moved the character", advanced, polls);
    Check("in fewer polls than the staging window allows", polls < 100, true);
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
    TheCorridorIsWalkedEndToEndByTheOrdinaryRouteStep();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a measured staging corridor is walked instead of a route through a guard post\n");
    return 0;
}
