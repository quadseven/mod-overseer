/*
 * What the ground costs to walk across, and what avoiding it is worth
 * (mod-overseer#400).
 *
 * The route planner has known which legs the other side guards since #326 and
 * has priced them at zero ever since: a way round was adopted only when it was
 * not ONE YARD longer than the way through. This file is the price, and the two
 * halves of it that matter are proved here rather than argued:
 *
 *   * a level 40 guard post buys a detour of thousands of yards for a party of
 *     28, which is the party that kept dying on it, and
 *   * the same guard post buys NOTHING for a party of 60, so the planner is
 *     returned to today's distance-only answer without a special case.
 *
 * Every constant the model is made of comes from the pinned core rather than
 * from a tuning session, and the tests below assert against the core's own
 * arithmetic: Creature::GetAggroRange (Creature.cpp:3402), Acore::XP::
 * GetColorCode and GetGrayLevel (Formulas.h), CREATURE_FLAG_EXTRA_TRIGGER and
 * CREATURE_FLAG_EXTRA_CIVILIAN (CreatureData.h:53 and :47).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::AggroRadiusYards;
using OverseerDecisions::ConBand;
using OverseerDecisions::ConBandName;
using OverseerDecisions::ConBandOf;
using OverseerDecisions::ConBandWeight;
using OverseerDecisions::DangerLimits;
using OverseerDecisions::DangerSpawn;
using OverseerDecisions::GreyLevel;
using OverseerDecisions::GroundDanger;
using OverseerDecisions::PlanFootRoute;
using OverseerDecisions::RouteLink;
using OverseerDecisions::RouteNode;
using OverseerDecisions::RoutePlan;
using OverseerDecisions::RoutePlanLimits;
using OverseerDecisions::RoutePlanVerdict;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::ScoreGroundDanger;
using OverseerDecisions::SpawnCanAggro;

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

void CheckBand(char const* what, ConBand got, ConBand want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, ConBandName(got),
                ConBandName(want));
    ++failures;
}

// A tenth of a yard. Every quantity here is a distance in yards and the
// arithmetic runs through a hand-rolled square root, so an exact comparison
// would be asserting on the last bit of a float rather than on the rule.
void CheckNear(char const* what, float got, float want, float tolerance = 0.1f)
{
    float const off = got > want ? got - want : want - got;
    if (off <= tolerance)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f (within %.3f)\n", what, got, want,
                tolerance);
    ++failures;
}

void CheckAtLeast(char const* what, float got, float least)
{
    if (got >= least)
        return;
    std::printf("FAIL %s: got %.3f, wanted at least %.3f\n", what, got, least);
    ++failures;
}

// --------------------------------------------------------------- fixtures --

// The party the whole of #400 is about: the five characters that walked into
// the Crossroads guard post over and over. Levels read off the roster.
std::vector<std::uint32_t> TheFamily()
{
    return {28, 28, 29, 31, 34};
}

// The same five, if they had ever got to sixty. Used to prove the model gets
// out of the way rather than merely getting quieter.
std::vector<std::uint32_t> TheFamilyAtSixty()
{
    return {60, 60, 60, 60, 60};
}

DangerSpawn Guard(float x, float y, std::uint32_t level)
{
    DangerSpawn spawn;
    spawn.x = x;
    spawn.y = y;
    spawn.level = level;
    return spawn;
}

// The measured guard post, five Horde Guard at level 40 and Thork at 42, strung
// along the leg the route kept choosing. Placed 4 to 6 yards off the walked
// line, which is the order the survey's own waypoints pass real guards at: the
// leg this family died on runs 5 yards from a level 40 spawn.
std::vector<DangerSpawn> TheCrossroads()
{
    return {
        Guard(120.f, 5.f, 40), Guard(200.f, -4.f, 40), Guard(260.f, 6.f, 40),
        Guard(330.f, -5.f, 40), Guard(400.f, 4.f, 40), Guard(460.f, -5.f, 42),
    };
}

// A straight leg of open ground, as a surveyed leg's own waypoints.
std::vector<RoutePoint> LegAlongX(float fromX, float toX, float step)
{
    std::vector<RoutePoint> points;
    for (float x = fromX; x < toX; x += step)
    {
        RoutePoint point;
        point.x = x;
        points.push_back(point);
    }
    RoutePoint last;
    last.x = toX;
    points.push_back(last);
    return points;
}

// ------------------------------------------- the core's own aggro radius --

void TheAggroRadiusIsTheCoresOwnArithmetic()
{
    // Creature.cpp:3416 and :3423. levelDiff is player minus creature, so a
    // party BELOW the creature is reached further, one yard per level.
    CheckNear("a level 40 guard reaches a level 28 party", AggroRadiusYards(40, 28, 20.f, 1.f), 32.f);
    CheckNear("a level 42 guard reaches a level 28 party", AggroRadiusYards(42, 28, 20.f, 1.f), 34.f);
    CheckNear("an even level creature reaches twenty", AggroRadiusYards(28, 28, 20.f, 1.f), 20.f);
    CheckNear("a level 18 crab reaches a level 28 party", AggroRadiusYards(18, 28, 20.f, 1.f), 10.f);

    // And this is the whole of why the model needs no rule about guards: the
    // SAME guard post, seen by a party that has outgrown it, reaches combat
    // range and no further. 32 to 5 is 41 times the area.
    CheckNear("the same guard post barely reaches a level 60 party",
              AggroRadiusYards(40, 60, 20.f, 1.f), 5.f);
}

void AnAggroRadiusIsClampedAtBothEnds()
{
    // Creature.cpp:3417-3418 - the level difference itself is clamped at 25, so
    // a level 60 elite reaches a level 28 and a level 5 identically.
    CheckNear("the level gap is clamped at 25 under", AggroRadiusYards(60, 28, 20.f, 1.f), 45.f);
    CheckNear("...and no further under than that", AggroRadiusYards(60, 20, 20.f, 1.f), 45.f);

    // Creature.cpp:3434, MAX_AGGRO_RADIUS at Unit.h:44. A generous detection
    // range cannot push past it.
    CheckNear("MAX_AGGRO_RADIUS caps a long detection range",
              AggroRadiusYards(40, 28, 40.f, 1.f), 45.f);
    // ...but a detection range under the cap is honoured, so this is data and
    // not the literal 20 that GetAttackDistance uses.
    CheckNear("a detection range under the cap is honoured",
              AggroRadiusYards(40, 28, 30.f, 1.f), 42.f);

    // Creature.cpp:3439-3442.
    CheckNear("combat range is the floor", AggroRadiusYards(18, 34, 20.f, 1.f), 5.f);
    CheckNear("...however far the party has outgrown it",
              AggroRadiusYards(5, 80, 20.f, 1.f), 5.f);

    // Rate.Creature.Aggro multiplies the answer, Creature.cpp:3444.
    CheckNear("the realm's aggro rate multiplies it", AggroRadiusYards(40, 28, 20.f, 2.f), 64.f);
}

void ADetectionRangeUnderOneReadsAsTheCoreReadsIt()
{
    // Creature.cpp:3421 - asked of the detection range BEFORE the level term,
    // which is why a row with no detection_range is zero rather than 5.
    CheckNear("a detection range under one reaches nothing",
              AggroRadiusYards(40, 28, 0.5f, 1.f), 0.f);
    CheckNear("...and zero is under one", AggroRadiusYards(40, 28, 0.f, 1.f), 0.f);
    // Creature.cpp:3407.
    CheckNear("an aggro rate of zero reaches nothing",
              AggroRadiusYards(40, 28, 20.f, 0.f), 0.f);
    CheckNear("a negative rate is refused rather than flipped",
              AggroRadiusYards(40, 28, 20.f, -1.f), 0.f);
}

// ------------------------------------------------ the core's own con bands --

void TheGreyLevelIsTheCoresOwn()
{
    // Formulas.h Acore::XP::GetGrayLevel, branch for branch.
    CheckUInt("grey level at 5", GreyLevel(5), 0);
    CheckUInt("grey level at 1", GreyLevel(1), 0);
    CheckUInt("grey level at 6", GreyLevel(6), 1);
    CheckUInt("grey level at 10", GreyLevel(10), 4);
    CheckUInt("grey level at 28", GreyLevel(28), 21);
    CheckUInt("grey level at 34", GreyLevel(34), 26);
    CheckUInt("grey level at 39", GreyLevel(39), 31);
    // The branch edge at 39/40, which is where a copied formula goes wrong.
    CheckUInt("grey level at 40", GreyLevel(40), 31);
    CheckUInt("grey level at 59", GreyLevel(59), 47);
    CheckUInt("grey level at 60", GreyLevel(60), 51);
    CheckUInt("grey level at 80", GreyLevel(80), 71);
}

void TheConBandsAreTheCoresOwn()
{
    // Formulas.h GetColorCode, at every edge. A level 28 character.
    CheckBand("plus ten is the client's ??", ConBandOf(28, 38), ConBand::Skull);
    CheckBand("plus nine is still red", ConBandOf(28, 37), ConBand::Red);
    CheckBand("plus five is red", ConBandOf(28, 33), ConBand::Red);
    CheckBand("plus four is orange", ConBandOf(28, 32), ConBand::Orange);
    CheckBand("plus three is orange", ConBandOf(28, 31), ConBand::Orange);
    CheckBand("plus two is yellow", ConBandOf(28, 30), ConBand::Yellow);
    CheckBand("even is yellow", ConBandOf(28, 28), ConBand::Yellow);
    CheckBand("minus two is yellow", ConBandOf(28, 26), ConBand::Yellow);
    CheckBand("minus three is green", ConBandOf(28, 25), ConBand::Green);
    // One above the grey level is the last green, and the grey level itself is
    // grey. GetGrayLevel(28) is 21.
    CheckBand("one above grey is green", ConBandOf(28, 22), ConBand::Green);
    CheckBand("the grey level itself is grey", ConBandOf(28, 21), ConBand::Grey);
    CheckBand("well under is grey", ConBandOf(28, 18), ConBand::Grey);

    // THE CASE THE WHOLE MODEL TURNS ON, from both ends.
    CheckBand("a level 40 guard is skull to a 28", ConBandOf(28, 40), ConBand::Skull);
    CheckBand("the same guard is GREY to a 60", ConBandOf(60, 40), ConBand::Grey);
}

void ALowLevelCharacterDoesNotUnderflowIntoYellow()
{
    // `playerLevel - 2` on an unsigned level 1 is four billion, and a model
    // that computed it that way would read every creature in the world as
    // yellow to a fresh character. Signed throughout, so this is ordinary.
    CheckBand("a level 1 against a level 1", ConBandOf(1, 1), ConBand::Yellow);
    CheckBand("a level 1 against a level 11", ConBandOf(1, 11), ConBand::Skull);
    CheckBand("a level 2 against a level 1", ConBandOf(2, 1), ConBand::Yellow);
}

void GreyIsFreeAndTheBandsDouble()
{
    CheckNear("grey costs nothing", ConBandWeight(ConBand::Grey), 0.f);
    CheckNear("green is a quarter", ConBandWeight(ConBand::Green), 0.25f);
    CheckNear("yellow is the unit", ConBandWeight(ConBand::Yellow), 1.f);
    CheckNear("orange doubles", ConBandWeight(ConBand::Orange), 2.f);
    CheckNear("red doubles again", ConBandWeight(ConBand::Red), 4.f);
    CheckNear("skull doubles again", ConBandWeight(ConBand::Skull), 8.f);
}

// ------------------------------------------------------ what can pull at all --

void ATriggerACivilianAndAnUnfightableSpawnAreNotThreats()
{
    DangerSpawn ordinary = Guard(0.f, 0.f, 40);
    Check("an ordinary guard can pull", SpawnCanAggro(ordinary), true);

    // CREATURE_FLAG_EXTRA_TRIGGER, CreatureData.h:53. The "OLDWorld Trigger (DO
    // NOT DELETE)" at level 60 that the sweep near the flight master found.
    DangerSpawn trigger = ordinary;
    trigger.trigger = true;
    trigger.level = 60;
    Check("a trigger cannot pull", SpawnCanAggro(trigger), false);

    // CREATURE_FLAG_EXTRA_CIVILIAN, CreatureData.h:47. Creature::CanStartAttack
    // opens with this refusal, ahead of faction, level and distance, which is
    // what a level 80 holiday event host in a neutral town is.
    DangerSpawn civilian = ordinary;
    civilian.civilian = true;
    civilian.level = 80;
    Check("a civilian cannot pull", SpawnCanAggro(civilian), false);

    // The three unit_flags #302 already reads, handed over by the adapter's own
    // CanBeFought so the two callers cannot drift apart.
    DangerSpawn decoration = ordinary;
    decoration.canBeFought = false;
    Check("a fire effect cannot pull", SpawnCanAggro(decoration), false);
}

// ------------------------------------------------------------ the exposure --

void ExposureIsMeasuredAlongThePolylineAndNotSampled()
{
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 100.f, 25.f);
    DangerLimits const limits;
    std::vector<std::uint32_t> const party = TheFamily();

    // A level 40 guard reaches 32 yards for a level 28. Standing 5 yards off
    // the line, the chord through its circle is 2 * sqrt(32^2 - 5^2).
    GroundDanger const close =
        ScoreGroundDanger({Guard(50.f, 5.f, 40)}, leg, party, limits);
    CheckUInt("the near guard reaches the leg", close.spawns, 1);
    CheckNear("the chord five yards off", close.exposedYards, 63.21f, 0.5f);
    CheckNear("the nearest approach is reported", close.closestYards, 5.f);

    // Thirty one yards off, the same guard clips the leg and the exposure falls
    // with it. A sampled reading at thirty yard spacing would answer this one
    // yes or no depending on where the samples happened to land.
    GroundDanger const grazing =
        ScoreGroundDanger({Guard(50.f, 31.f, 40)}, leg, party, limits);
    CheckUInt("the grazing guard still reaches", grazing.spawns, 1);
    CheckNear("the chord thirty one yards off", grazing.exposedYards, 15.87f, 0.5f);

    // Forty yards off is outside a 32 yard reach and is not on this ground at
    // all. It may be the next leg's business.
    GroundDanger const past =
        ScoreGroundDanger({Guard(50.f, 40.f, 40)}, leg, party, limits);
    CheckUInt("the far guard reaches nothing", past.spawns, 0);
    CheckNear("...and costs nothing", past.detourYards, 0.f);
}

void TwoGuardsOnOneStretchCostTwice()
{
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 100.f, 25.f);
    DangerLimits const limits;
    std::vector<std::uint32_t> const party = TheFamily();

    GroundDanger const one = ScoreGroundDanger({Guard(50.f, 5.f, 40)}, leg, party, limits);
    GroundDanger const two = ScoreGroundDanger(
        {Guard(50.f, 5.f, 40), Guard(50.f, -5.f, 40)}, leg, party, limits);

    CheckUInt("both reach the leg", two.spawns, 2);
    // Deliberately NOT a union of the two circles: two guards standing on one
    // stretch of road really are twice the fight, and the model says so.
    CheckNear("two guards cost twice one", two.detourYards, one.detourYards * 2.f, 1.f);
}

void TheCrabBesideTheRoadIsFree()
{
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 100.f, 25.f);
    DangerLimits const limits;

    // The brief's own false positive: three level 18 Slimeshell Makrura beside
    // a flight master, which a sweep with a level FLOOR reports as hostile and
    // which a level 34 warrior walks past every day. GetGrayLevel(28) is 21.
    std::vector<DangerSpawn> const crabs = {
        Guard(30.f, 3.f, 18), Guard(50.f, 2.f, 18), Guard(70.f, 4.f, 18),
    };
    GroundDanger const scored = ScoreGroundDanger(crabs, leg, TheFamily(), limits);
    CheckUInt("no crab is counted", scored.spawns, 0);
    CheckNear("a crab buys no detour at all", scored.detourYards, 0.f);

    // And the boundary is the core's, not a rounding of it: one level above the
    // grey line is green and does cost something, small.
    GroundDanger const green =
        ScoreGroundDanger({Guard(50.f, 3.f, 22)}, leg, TheFamily(), limits);
    CheckUInt("a green spawn is counted", green.spawns, 1);
    CheckBand("...as green", green.worstBand, ConBand::Green);
    CheckAtLeast("...and costs a little", green.detourYards, 1.f);
}

void TheLowestMemberIsWhatItIsMeasuredAgainst()
{
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 100.f, 25.f);
    DangerLimits const limits;
    std::vector<DangerSpawn> const guard = {Guard(50.f, 5.f, 40)};

    // A party walks as far as its weakest member survives. The 34 in this
    // family does not make the 28 safe.
    GroundDanger const mixed = ScoreGroundDanger(guard, leg, TheFamily(), limits);
    GroundDanger const lowestAlone = ScoreGroundDanger(guard, leg, {28}, limits);
    CheckNear("the party is priced at its lowest member",
              mixed.detourYards, lowestAlone.detourYards, 0.01f);

    // A level 0 is a row that has not been read, not a character, and must not
    // be mistaken for the weakest member.
    GroundDanger const withHole = ScoreGroundDanger(guard, leg, {0, 28, 34}, limits);
    CheckNear("an unread level is not the weakest member",
              withHole.detourYards, lowestAlone.detourYards, 0.01f);

    GroundDanger const noParty = ScoreGroundDanger(guard, leg, {0, 0}, limits);
    CheckNear("a party of nobody scores nothing", noParty.detourYards, 0.f);
}

void TheGuardPostDominatesForTheFamilyAndIsFreeAtSixty()
{
    // The leg the route kept choosing, with the measured post strung along it.
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 500.f, 25.f);
    DangerLimits const limits;

    GroundDanger const atTwentyEight =
        ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), limits);
    CheckUInt("every guard reaches the leg", atTwentyEight.spawns, 6);
    CheckUInt("the worst of them is level 42", atTwentyEight.worstLevel, 42);
    CheckBand("...and it is skull to this family", atTwentyEight.worstBand, ConBand::Skull);
    // THE HEADLINE. The brief asked that avoiding a guard post beat being 500
    // yards shorter. It beats it several times over, and it does so as
    // arithmetic on the party's own levels rather than as a chosen constant.
    CheckAtLeast("the post is worth far more than 500 yards of detour",
                 atTwentyEight.detourYards, 2500.f);

    // AND THE PROPERTY THAT MAKES IT SAFE TO SHIP. The same six spawns, the
    // same leg, a party that has outgrown them: every band grey, nothing
    // counted, and the planner is handed back its old distance-only answer with
    // no special case anywhere saying so.
    GroundDanger const atSixty =
        ScoreGroundDanger(TheCrossroads(), leg, TheFamilyAtSixty(), limits);
    CheckUInt("no guard is counted for a party of sixty", atSixty.spawns, 0);
    CheckNear("the post is free at sixty", atSixty.detourYards, 0.f);
}

void TheBudgetIsCappedAndNonsenseLimitsScoreNothing()
{
    std::vector<RoutePoint> const leg = LegAlongX(0.f, 500.f, 25.f);

    DangerLimits capped;
    capped.maxDetourYards = 400.f;
    GroundDanger const bounded =
        ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), capped);
    CheckNear("the budget cannot exceed its ceiling", bounded.detourYards, 400.f);

    // Refused rather than clamped, the same way PlanFootRoute refuses nonsense
    // limits: a sign typo must not quietly become a rule LOOSER than the one
    // written, and scoring nothing keeps today's distance-only answer.
    DangerLimits negativeRate;
    negativeRate.aggroRate = -1.f;
    CheckNear("a negative aggro rate scores nothing",
              ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), negativeRate).detourYards, 0.f);

    DangerLimits negativePrice;
    negativePrice.yardsPerExposedYard = -1.f;
    CheckNear("a negative price scores nothing",
              ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), negativePrice).detourYards, 0.f);

    // The one knob a caller is meant to turn, and it is linear. The ceiling is
    // lifted on both sides of the comparison, because the measured post at
    // double price is over the shipped 6000 and the cap would otherwise be what
    // is being measured here.
    DangerLimits doubled;
    doubled.yardsPerExposedYard = 2.f;
    doubled.maxDetourYards = 100000.f;
    DangerLimits plain;
    plain.maxDetourYards = 100000.f;
    CheckNear("the price scales the budget",
              ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), doubled).detourYards,
              ScoreGroundDanger(TheCrossroads(), leg, TheFamily(), plain).detourYards * 2.f,
              1.f);
}

void APathOfOnePointIsNoGround()
{
    DangerLimits const limits;
    std::vector<RoutePoint> one(1);
    CheckNear("one point is not a stretch of ground",
              ScoreGroundDanger(TheCrossroads(), one, TheFamily(), limits).detourYards, 0.f);
    CheckNear("and neither is none",
              ScoreGroundDanger(TheCrossroads(), {}, TheFamily(), limits).detourYards, 0.f);
    CheckNear("an empty world costs nothing",
              ScoreGroundDanger({}, LegAlongX(0.f, 100.f, 25.f), TheFamily(), limits).detourYards,
              0.f);
}

// -------------------------------------------------------------- the trade --
//
// The fixture is the shape #400 is about, reduced to the smallest graph that
// carries it. The way through is 1000 yards and crosses the guard post; the way
// round is 1500, so going round is EXACTLY 500 YARDS LONGER, which is the
// number the brief named. Under the shipped rule the way round loses, because
// the shipped rule takes it only when it is not one yard longer.

constexpr std::uint32_t MAP = 1;

std::vector<RouteNode> TwoWaysGraph()
{
    return {
        {1, MAP, 0.f, 0.f, 0.f},        // where the character stands
        {2, MAP, 500.f, 0.f, 0.f},      // through the post
        {3, MAP, 1000.f, 0.f, 0.f},     // the aim
        {4, MAP, 0.f, 250.f, 0.f},      // round, north
        {5, MAP, 1000.f, 250.f, 0.f},   // round, north
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

// `worth` is what ScoreGroundDanger said about each guarded leg. Zero is the
// unmeasured graph, which must produce today's plan.
std::vector<RouteLink> TwoWaysLinks(float worth)
{
    std::vector<RouteLink> links = {
        Walk(1, 2, 500.f), Walk(2, 3, 500.f),          // 1000 through the post
        Walk(1, 4, 250.f), Walk(4, 5, 1000.f), Walk(5, 3, 250.f),  // 1500 round
    };
    // Only the two legs of the way through are guarded.
    for (std::size_t i = 0; i < 2; ++i)
    {
        links[i].guardedGround = true;
        links[i].detourWorthYards = worth;
    }
    return links;
}

RoutePlan PlanWith(float worth)
{
    RoutePlanLimits const limits;
    return PlanFootRoute(TwoWaysGraph(), TwoWaysLinks(worth), MAP, 0.f, 0.f, 1000.f, 0.f,
                         limits);
}

void AnUnmeasuredGraphGetsTodaysAnswer()
{
    // The whole backward compatibility argument, as a test rather than a claim:
    // a graph nobody has priced walks through the guards exactly as it did
    // before #400, and says so.
    RoutePlan const plan = PlanWith(0.f);
    Check("an unpriced graph still plans", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("...and does not go round", plan.wentRound, false);
    CheckUInt("...and reports its guarded legs", plan.guardedLegs, 2);
    CheckNear("...having had nothing to spend", plan.detourBudgetYards, 0.f);
    CheckNear("...and walks the short way", plan.yards, 1000.f);
}

void AWayRoundThatIsFiveHundredYardsLongerIsRefusedWhenTheGuardsAreCheap()
{
    // 200 yards a leg, 400 in total, against a detour that costs 500. Still not
    // worth it, and the point of the test is that the rule is a COMPARISON and
    // not a switch that flips as soon as anything is priced at all.
    RoutePlan const plan = PlanWith(200.f);
    Check("a cheap guard does not buy the detour", plan.wentRound, false);
    CheckUInt("...so the guarded legs are still walked", plan.guardedLegs, 2);
    CheckNear("...and the budget it fell short with is reported",
              plan.detourBudgetYards, 400.f);
}

void AWayRoundThatIsLongerIsTakenWhenTheGuardsAreWorthIt()
{
    // 300 yards a leg, 600 in total, against a detour that costs 500. THIS IS
    // THE FIX: the way round is longer and is taken anyway, which the shipped
    // rule could never do.
    RoutePlan const plan = PlanWith(300.f);
    Check("the way round is planned", plan.verdict == RoutePlanVerdict::Planned, true);
    Check("...and it is taken though it is longer", plan.wentRound, true);
    CheckUInt("...so no leg of it crosses guarded ground", plan.guardedLegs, 0);
    CheckNear("...and it really is the longer way", plan.yards, 1500.f);
    CheckNear("...bought with the budget it reports", plan.detourBudgetYards, 600.f);
    CheckUInt("...by way of the northern nodes", static_cast<unsigned>(plan.nodes.size()), 4);
}

void TheRealGuardPostBuysTheRealDetour()
{
    // END TO END, with no hand-written price anywhere: the guard post is scored
    // by ScoreGroundDanger against the leg's own points and the party's own
    // levels, and the answer is handed to the planner exactly as the adapter
    // will hand it over.
    std::vector<RoutePoint> const throughThePost = LegAlongX(0.f, 500.f, 25.f);
    DangerLimits const limits;
    GroundDanger const measured =
        ScoreGroundDanger(TheCrossroads(), throughThePost, TheFamily(), limits);

    // Only the first of the two guarded legs carries the post, which is the
    // honest shape: a post sits on one leg.
    std::vector<RouteLink> links = TwoWaysLinks(0.f);
    links[0].detourWorthYards = measured.detourYards;

    RoutePlanLimits const planLimits;
    RoutePlan const plan =
        PlanFootRoute(TwoWaysGraph(), links, MAP, 0.f, 0.f, 1000.f, 0.f, planLimits);
    Check("the measured post buys the way round", plan.wentRound, true);
    CheckUInt("...leaving no guarded leg on the plan", plan.guardedLegs, 0);

    // And the same graph, the same post, for a party that has outgrown it: the
    // family walks the short way through, because there is nothing there for
    // them any more.
    GroundDanger const atSixty =
        ScoreGroundDanger(TheCrossroads(), throughThePost, TheFamilyAtSixty(), limits);
    links[0].detourWorthYards = atSixty.detourYards;
    RoutePlan const plainer =
        PlanFootRoute(TwoWaysGraph(), links, MAP, 0.f, 0.f, 1000.f, 0.f, planLimits);
    Check("a party of sixty walks the short way", plainer.wentRound, false);
    CheckNear("...with nothing to spend", plainer.detourBudgetYards, 0.f);
}

void ANegativePriceCannotShrinkTheBudget()
{
    // Nothing should ever write one. If something does, it must not quietly buy
    // the party a walk through a guard post by making the budget smaller than
    // the legs it is summed from.
    std::vector<RouteLink> links = TwoWaysLinks(300.f);
    links[1].detourWorthYards = -10000.f;
    RoutePlanLimits const limits;
    RoutePlan const plan =
        PlanFootRoute(TwoWaysGraph(), links, MAP, 0.f, 0.f, 1000.f, 0.f, limits);
    CheckNear("a negative price is not a discount", plan.detourBudgetYards, 300.f);
    Check("...and the leg that is left is not enough on its own", plan.wentRound, false);
}

}  // namespace

int main()
{
    TheAggroRadiusIsTheCoresOwnArithmetic();
    AnAggroRadiusIsClampedAtBothEnds();
    ADetectionRangeUnderOneReadsAsTheCoreReadsIt();

    TheGreyLevelIsTheCoresOwn();
    TheConBandsAreTheCoresOwn();
    ALowLevelCharacterDoesNotUnderflowIntoYellow();
    GreyIsFreeAndTheBandsDouble();

    ATriggerACivilianAndAnUnfightableSpawnAreNotThreats();

    ExposureIsMeasuredAlongThePolylineAndNotSampled();
    TwoGuardsOnOneStretchCostTwice();
    TheCrabBesideTheRoadIsFree();
    TheLowestMemberIsWhatItIsMeasuredAgainst();
    TheGuardPostDominatesForTheFamilyAndIsFreeAtSixty();
    TheBudgetIsCappedAndNonsenseLimitsScoreNothing();
    APathOfOnePointIsNoGround();

    AnUnmeasuredGraphGetsTodaysAnswer();
    AWayRoundThatIsFiveHundredYardsLongerIsRefusedWhenTheGuardsAreCheap();
    AWayRoundThatIsLongerIsTakenWhenTheGuardsAreWorthIt();
    TheRealGuardPostBuysTheRealDetour();
    ANegativePriceCannotShrinkTheBudget();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a guard post costs what it is worth, and nothing at sixty\n");
    return EXIT_SUCCESS;
}
