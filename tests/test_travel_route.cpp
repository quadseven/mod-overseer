/*
 * The ground between here and there (#300, #267's out-of-scope half).
 *
 * #234 named two things. #250 fixed the first. #267 fixed the second HALF of
 * the second - a destination standing in hostile ground - and put the route
 * itself out of scope on the stated grounds that aiming only the party leader
 * bounds the risk. It does not: the four followers walk the same ground behind
 * it, and a level 27 died four times in eight minutes crossing Searing Gorge
 * while the level 31 leader carried the only aim.
 *
 * THE FIXTURES ARE REAL PROFILES, sampled on 2026-09-07 against the dev
 * realm's own `creature`, `creature_template` and `factiontemplate_dbc` rows
 * while the family was dying on these exact lines - not numbers invented to
 * make the predicate fire. Each one is the level of the worst thing hostile to
 * the character and able to fight it within 60 yards of a point, taken every
 * 30 yards along the straight line from where the character stood to where it
 * was aimed, and written run-length encoded because 135 samples do not fit on
 * a page.
 *
 * Compiled without AzerothCore, like every other test here, so the rule stays
 * a pure decision.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ChooseTravelTarget;
using OverseerDecisions::RouteLimits;
using OverseerDecisions::RouteReading;
using OverseerDecisions::RouteVerdict;
using OverseerDecisions::JudgeRoute;
using OverseerDecisions::PlanRouteSamples;
using OverseerDecisions::RouteSampleAt;
using OverseerDecisions::RouteSampling;
using OverseerDecisions::TravelTargetCandidate;
using OverseerDecisions::TravelTargetChoice;
using OverseerDecisions::TravelTargetExplanation;
using OverseerDecisions::TravelTargetVerdict;

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

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const slack = 1.0f;   // whole yards; the sample spacing is thirty
    if (got >= want - slack && got <= want + slack)
        return;
    std::printf("FAIL %s: got %.1f, wanted %.1f\n", what, double(got), double(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    '%s'\n  wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

struct Run
{
    uint32_t level;
    std::size_t times;
};

RouteReading Reading(uint32_t level, float spacing, std::vector<Run> const& runs)
{
    RouteReading reading;
    reading.characterLevel = level;
    reading.sampleSpacingYards = spacing;
    for (Run const& run : runs)
        for (std::size_t i = 0; i < run.times; ++i)
            reading.worstLevelAtSample.push_back(run.level);
    return reading;
}

// THE WALK THAT KILLED THEM. Level 28, from the spot in Searing Gorge where
// `overseer_death` recorded six deaths to one level 48 rare elite, along the
// line to the aim the roster was carrying, 4,009 yards to the west.
std::vector<Run> const SEARING_GORGE_TO_THE_AIM = {
    {65, 4}, {49, 8}, {47, 1}, {49, 7}, {0, 3}, {48, 3}, {0, 10}, {54, 4},
    {0, 12}, {57, 4}, {58, 9}, {57, 1}, {0, 13}, {10, 18}, {0, 9}, {10, 12},
    {20, 6}, {0, 1}, {30, 3}, {32, 3}, {0, 4},
};

// The same aim from where the level 31 leader was standing in Burning Steppes,
// 2,954 yards out. It is the mildest of the lethal set and the one that decides
// whether the threshold is worth anything.
std::vector<Run> const BURNING_STEPPES_TO_THE_AIM = {
    {59, 2}, {0, 13}, {56, 3}, {58, 4}, {0, 17}, {10, 18}, {0, 3}, {8, 1},
    {10, 2}, {9, 4}, {0, 1}, {10, 13}, {19, 1}, {20, 2}, {22, 4}, {20, 1},
    {30, 3}, {32, 4}, {0, 4},
};

// The 2,012 yard errand #267 measured, endorsed and sent the family on. Nothing
// on it is within nineteen levels of a level 31, and the family took four
// deaths near that town in the whole history of the table.
std::vector<Run> const THE_ERRAND_267_ENDORSED = {
    {0, 4}, {16, 4}, {0, 2}, {15, 6}, {0, 8}, {15, 4}, {0, 5}, {13, 7},
    {14, 1}, {15, 4}, {14, 1}, {16, 5}, {18, 8}, {23, 2}, {18, 3}, {0, 5},
};

// The rest of the measured set, so the two populations below are the whole of
// what was read and not a flattering slice of it.
std::vector<Run> const SEARING_GORGE_TO_BURNING_STEPPES = {
    {49, 1}, {48, 3}, {49, 4}, {47, 7}, {0, 8}, {54, 1}, {55, 4}, {54, 2},
    {0, 13}, {57, 3},
};
std::vector<Run> const THE_VENDOR_267_REFUSED = {
    {0, 12}, {15, 2}, {65, 4},
};
std::vector<Run> const GOLDSHIRE_TO_STORMWIND = {
    {6, 4}, {7, 8},
};
std::vector<Run> const GOLDSHIRE_TO_EASTVALE = {
    {6, 1}, {0, 2}, {6, 4}, {0, 4}, {6, 1}, {7, 10}, {8, 1}, {9, 2}, {8, 2},
    {9, 4}, {8, 2}, {9, 4}, {8, 1}, {10, 3}, {9, 5}, {0, 7}, {10, 1},
};
std::vector<Run> const STORMWIND_TO_KHARANOS = {
    {7, 1}, {0, 108}, {8, 3}, {0, 2}, {6, 3}, {0, 3},
};

// Kharanos to the gates of Ironforge, 626 yards, read twice off the same rows.
// The only difference between them is whether a spawn that cannot be fought is
// counted: twenty-four "Headless Horseman Flame Bunny" at level 70, faction 14,
// UNIT_FLAG_NOT_SELECTABLE, stand in Kharanos all year round.
std::vector<Run> const KHARANOS_COUNTING_EVERYTHING = {
    {0, 1}, {70, 3}, {6, 4}, {0, 2}, {10, 8}, {0, 4},
};
std::vector<Run> const KHARANOS_COUNTING_WHAT_CAN_FIGHT = {
    {0, 4}, {6, 4}, {0, 2}, {10, 8}, {0, 4},
};

// THE LINE THE FAMILY WAS ACTUALLY WALKING, AND THE GATE REFUSES IT.
void TheWalkThatKilledThemIsRefused()
{
    RouteVerdict const verdict =
        JudgeRoute(Reading(28, 29.7f, SEARING_GORGE_TO_THE_AIM), RouteLimits{});

    Check("the crossing is refused", verdict.survivable, false);
    CheckNear("twenty samples of unbroken `??` ground at the start of it",
              verdict.longestLethalRunYards, 594.0f);
    CheckNear("and 1,218 yards of it over the whole line",
              verdict.lethalYards, 1217.7f);
    CheckInt("the worst thing on the line is named for the log",
             long(verdict.worstLevel), 65);
}

// The mildest of the three, and the one the threshold has to catch to be worth
// having: 207 yards unbroken against a limit of 200.
void TheMildestLethalOneIsStillRefused()
{
    RouteVerdict const verdict =
        JudgeRoute(Reading(31, 29.5f, BURNING_STEPPES_TO_THE_AIM), RouteLimits{});

    Check("still refused", verdict.survivable, false);
    CheckNear("by seven yards of margin, which is where the threshold sits",
              verdict.longestLethalRunYards, 206.5f);
}

// THE OTHER HALF OF THE ARGUMENT, AND THE MORE IMPORTANT ONE. A gate that
// refuses everything is not a fix. The errand #267 endorsed is two thousand
// yards long and reads perfectly clean, for the leader and for the weakest
// member of the family alike.
void TheErrandItShouldStillRunIsUntouched()
{
    RouteVerdict const leader =
        JudgeRoute(Reading(31, 29.6f, THE_ERRAND_267_ENDORSED), RouteLimits{});
    Check("the 2,012 yard errand is crossable for the leader", leader.survivable, true);
    CheckNear("with no lethal ground on it at all", leader.lethalYards, 0.0f);

    RouteVerdict const weakest =
        JudgeRoute(Reading(25, 29.6f, THE_ERRAND_267_ENDORSED), RouteLimits{});
    Check("and for the level 25 that walks it behind him", weakest.survivable, true);
    CheckNear("still nothing", weakest.lethalYards, 0.0f);
}

// THE LEVEL IS HALF THE QUESTION. The same ground, the same day, the same
// reading - a character ten levels higher may cross it, and that is the point
// of asking about a character rather than about a zone.
void TheSameGroundReadsDifferentlyForADifferentCharacter()
{
    Check("a level 31 may not cross it",
          JudgeRoute(Reading(31, 29.5f, BURNING_STEPPES_TO_THE_AIM), RouteLimits{}).survivable,
          false);
    Check("a level 50 may",
          JudgeRoute(Reading(50, 29.5f, BURNING_STEPPES_TO_THE_AIM), RouteLimits{}).survivable,
          true);
}

// TEN SMALL CAMPS ARE NOT ONE LONG ONE, and this is why the measure is the
// unbroken run and not the total. Six hundred yards of trouble arriving in ten
// separate sixty-yard pieces is an ordinary walk through a contested zone; a
// party is chased out of each one and keeps going. Refusing that would strand a
// family anywhere worth being.
void ScatteredTroubleIsNotACrossing()
{
    std::vector<Run> scattered;
    for (int i = 0; i < 10; ++i)
    {
        scattered.push_back({60, 2});
        scattered.push_back({0, 8});
    }
    RouteVerdict const verdict = JudgeRoute(Reading(27, 30.0f, scattered), RouteLimits{});

    Check("six hundred yards of lethal ground, and it is still crossable",
          verdict.survivable, true);
    CheckNear("because none of it is longer than sixty yards at a time",
              verdict.longestLethalRunYards, 60.0f);
    CheckNear("the total is carried anyway, for the operator",
              verdict.lethalYards, 600.0f);
}

// AND ONE LONG ONE IS REFUSED EVEN WHEN THE TOTAL IS SMALLER.
void OneLongStretchIsRefusedOnLessTotalGround()
{
    RouteVerdict const verdict =
        JudgeRoute(Reading(27, 30.0f, {{0, 20}, {60, 9}, {0, 20}}), RouteLimits{});
    Check("270 yards in one piece is refused", verdict.survivable, false);
    CheckNear("and it is less lethal ground than the scattered case",
              verdict.lethalYards, 270.0f);
}

// A READING NOBODY TOOK IS NOT A CLEAN BILL. The caller reads a shortlist in
// distance order and leaves the rest alone, exactly as it already does for the
// guard sweep, so "no samples" has to mean "not asked" rather than "safe" -
// and it does, because an unmeasured candidate is always farther away than the
// one chosen and could not have won anyway.
void AnUnmeasuredWalkIsNotAClaim()
{
    Check("an empty reading is crossable", JudgeRoute(RouteReading{}, RouteLimits{}).survivable, true);
    CheckNear("and claims nothing about the ground",
              JudgeRoute(RouteReading{}, RouteLimits{}).lethalYards, 0.0f);

    RouteReading noSpacing = Reading(27, 0.0f, {{70, 40}});
    Check("nor is a reading with no spacing to measure with",
          JudgeRoute(noSpacing, RouteLimits{}).survivable, true);
}

// A SPAWN NOTHING CAN FIGHT IS NOT A THREAT, and the same line reads two ways
// depending on whether it is counted. Neither reading refuses this walk - 89
// yards is well under the limit - but the level the log would print differs by
// sixty, and the same false positive is what makes the DESTINATION gate refuse
// 60 of the 1,163 service spawns on the two continents on grounds of a
// Hallow's End decoration.
void WhatCannotFightIsNotWhatIsMeasured()
{
    RouteVerdict const everything =
        JudgeRoute(Reading(27, 29.8f, KHARANOS_COUNTING_EVERYTHING), RouteLimits{});
    RouteVerdict const canFight =
        JudgeRoute(Reading(27, 29.8f, KHARANOS_COUNTING_WHAT_CAN_FIGHT), RouteLimits{});

    Check("the walk into Ironforge is crossable either way", everything.survivable, true);
    Check("either way", canFight.survivable, true);
    CheckInt("but counting a level 70 decoration says the ground is level 70",
             long(everything.worstLevel), 70);
    CheckInt("and counting only what can fight says it is level 10",
             long(canFight.worstLevel), 10);
    CheckNear("which is 89 yards of a lie", everything.lethalYards, 89.4f);
    CheckNear("against none", canFight.lethalYards, 0.0f);
}

// ------------------------------------------------------- where the samples go --

// THE POPULATION GAP, ASSERTED RATHER THAN ONLY WRITTEN DOWN.
//
// This is the strongest part of the case for the threshold and the easiest
// thing for a later reader to erode by feel, so it is a test. Every walk that
// was measured is here, both sides of it. The walks the family DIED on carry
// 209 to 598 yards of unbroken `??` ground; the walks it MADE, including a
// 2,012 yard errand and a 3,562 yard cross-country line, carry exactly zero.
// There is nothing in between. RouteLimits::lethalRunYards is 200 because it
// sits in that empty gap, not because 200 is a round number.
//
// If a future change makes one of these cross the line, it is this test that
// says so, and the honest response is to re-measure rather than to move the
// number.
void TheTwoPopulationsDoNotOverlap()
{
    struct Walk
    {
        char const* name;
        std::vector<Run> const* profile;
        uint32_t level;
        float spacing;
    };

    // Everything the family died on. Every one of these must refuse, and every
    // one must clear the threshold by a real margin rather than by rounding.
    Walk const died[] = {
        {"Searing Gorge -> the aim",     &SEARING_GORGE_TO_THE_AIM,        28, 29.7f},
        {"Searing Gorge -> B. Steppes",  &SEARING_GORGE_TO_BURNING_STEPPES, 27, 29.6f},
        {"Burning Steppes -> the aim",   &BURNING_STEPPES_TO_THE_AIM,      31, 29.5f},
    };
    for (Walk const& walk : died)
    {
        RouteVerdict const verdict = JudgeRoute(Reading(walk.level, walk.spacing, *walk.profile),
                                                OverseerDecisions::RouteLimits{});
        Check(walk.name, verdict.survivable, false);
        Check("and it is above the threshold, not at it",
              verdict.longestLethalRunYards > 205.f, true);
    }

    // Everything it walked without a creature death. Every one must be clean,
    // and CLEAN rather than merely under the line: the gap is what is being
    // asserted, so zero is the claim.
    Walk const survived[] = {
        {"the 2,012 yard errand #267 endorsed", &THE_ERRAND_267_ENDORSED, 31, 29.6f},
        {"the same, for the level 25 behind him", &THE_ERRAND_267_ENDORSED, 25, 29.6f},
        {"Kharanos -> the gates of Ironforge",  &KHARANOS_COUNTING_WHAT_CAN_FIGHT, 27, 29.8f},
        {"Goldshire -> Stormwind",              &GOLDSHIRE_TO_STORMWIND,  27, 27.6f},
        {"Goldshire -> Eastvale",               &GOLDSHIRE_TO_EASTVALE,   27, 29.4f},
        {"Stormwind -> Kharanos, 3,562 yards",  &STORMWIND_TO_KHARANOS,   27, 29.9f},
    };
    for (Walk const& walk : survived)
    {
        RouteVerdict const verdict = JudgeRoute(Reading(walk.level, walk.spacing, *walk.profile),
                                                OverseerDecisions::RouteLimits{});
        Check(walk.name, verdict.survivable, true);
        CheckNear("and carries no lethal ground at all", verdict.lethalYards, 0.0f);
    }

    // The one #267's destination gate already refuses, kept beside the two
    // populations because it belongs to neither: 118 yards is under this
    // threshold, and that candidate never reaches this gate because the guard
    // sweep took it out first. It is here so a reader does not mistake its
    // being under the line for this rule disagreeing with #267.
    RouteVerdict const refusedElsewhere =
        JudgeRoute(Reading(31, 29.5f, THE_VENDOR_267_REFUSED), OverseerDecisions::RouteLimits{});
    Check("the vendor #267 refused is not refused AGAIN by this rule",
          refusedElsewhere.survivable, true);
    CheckNear("its 118 yards is under the line", refusedElsewhere.longestLethalRunYards, 118.0f);
}

// THE EXACT MULTIPLE IS THE ONE SPAN THAT NEVER LOST A SAMPLE, and it is pinned
// because it was proposed as the case that did.
//
// A review of #301 read `std::size_t(read / SPACING)` and called an off-by-one
// at the exact multiple. The direction of the concern was right and is why the
// rounding is now up: an error here cannot make a false refusal, only a missed
// one, and it lands at the end of the line, nearest the destination, which is
// exactly where an unbroken run beginning at the destination sits. The TRIGGER
// was wrong. At an exact multiple, truncation removes nothing: 6000 / 30 is
// 200, twenty times 30 is 600, and the last sample lands on the destination.
// The shortfall is at every span that is NOT a multiple, where up to one whole
// spacing of line had no sample standing in it.
void TheExactMultipleNeverLostASample()
{
    RouteSampling const atCap = PlanRouteSamples(6000.f, 30.f, 6000.f);
    CheckInt("6000 yards at 30 is 200 samples, not 201", long(atCap.samples), 200);
    CheckNear("and the spacing is exactly the nominal one", atCap.spacingYards, 30.0f);
    CheckNear("200 spacings are the whole 6000 yards",
              float(atCap.samples) * atCap.spacingYards, 6000.0f);

    RouteSampling const small = PlanRouteSamples(600.f, 30.f, 6000.f);
    CheckInt("600 yards at 30 is 20 samples", long(small.samples), 20);
    CheckNear("20 spacings are the whole 600 yards",
              float(small.samples) * small.spacingYards, 600.0f);
}

// AND EVERY OTHER SPAN IS WHERE THE TAIL WAS ACTUALLY LOST. 4,029.9 yards
// truncates to 134 samples of 30, which is 4,020: nine and a bit yards of line,
// at the destination end, with nothing standing in it. Rounding up gives 135
// slightly shorter spacings that add up to the whole line.
void EverySpanIsSpokenForEndToEnd()
{
    RouteSampling const odd = PlanRouteSamples(4029.9f, 30.f, 6000.f);
    CheckInt("4,029.9 yards is 135 samples, not the 134 truncation gave",
             long(odd.samples), 135);
    Check("and the spacing is under the nominal one, never over",
          odd.spacingYards <= 30.0f, true);
    CheckNear("135 spacings are the whole line", float(odd.samples) * odd.spacingYards, 4029.9f);

    // The property, over every span rather than over one: the samples add up to
    // the line, the spacing never exceeds the nominal, the first sample is
    // inside the line and the last one is inside it too - never past the
    // destination, where it would be reading ground nobody walks.
    for (float span = 30.f; span <= 8000.f; span += 7.3f)
    {
        RouteSampling const plan = PlanRouteSamples(span, 30.f, 6000.f);
        float const read = span > 6000.f ? 6000.f : span;
        if (float(plan.samples) * plan.spacingYards < read - 0.05f ||
            float(plan.samples) * plan.spacingYards > read + 0.05f)
        {
            std::printf("FAIL span %.1f: %zu x %.4f does not add up to %.1f\n",
                        double(span), plan.samples, double(plan.spacingYards), double(read));
            ++failures;
            break;
        }
        if (plan.spacingYards > 30.0f + 1e-4f)
        {
            std::printf("FAIL span %.1f: spacing %.4f exceeds the nominal\n",
                        double(span), double(plan.spacingYards));
            ++failures;
            break;
        }
        float const first = RouteSampleAt(plan, 0);
        float const last = RouteSampleAt(plan, plan.samples - 1);
        if (!(first > 0.f) || !(last < read))
        {
            std::printf("FAIL span %.1f: samples run %.2f .. %.2f outside (0, %.1f)\n",
                        double(span), double(first), double(last), double(read));
            ++failures;
            break;
        }
    }
}

// WHAT THE RECOVERED SAMPLE IS WORTH, which is the only reason to have taken
// the finding at all. A 205 yard hostile run that ends AT the destination, on a
// line of 4,029.9 yards: truncation gave 134 samples of 30 and read six of them
// as lethal, 180 yards, under the line and not refused. Rounding up gives 135
// of 29.85 and reads seven, 209 yards, refused. Same ground, same rule, and the
// difference is the sample that used to fall off the end.
void ARunThatEndsAtTheDestinationIsCountedToItsEnd()
{
    RouteSampling const plan = PlanRouteSamples(4029.9f, 30.f, 6000.f);
    RouteVerdict const now =
        JudgeRoute(Reading(27, plan.spacingYards, {{0, 128}, {60, 7}}), OverseerDecisions::RouteLimits{});
    Check("seven samples of unbroken `??` ground at the end is refused", now.survivable, false);
    CheckNear("209 yards of it", now.longestLethalRunYards, 209.0f);

    RouteVerdict const truncated =
        JudgeRoute(Reading(27, 30.f, {{0, 128}, {60, 6}}), OverseerDecisions::RouteLimits{});
    Check("the six samples truncation left would have let it through",
          truncated.survivable, true);
    CheckNear("at 180 yards", truncated.longestLethalRunYards, 180.0f);
}

// A shop inside one spacing is not a walk. The ground around it is swept by the
// destination gate at the same radius, so there is nothing for this to add.
void AShopInsideOneSpacingIsNotAWalk()
{
    CheckInt("twenty yards is no samples", long(PlanRouteSamples(20.f, 30.f, 6000.f).samples), 0);
    CheckInt("and neither is nothing at all", long(PlanRouteSamples(0.f, 30.f, 6000.f).samples), 0);
    Check("which JudgeRoute reads as unmeasured, not as safe",
          JudgeRoute(Reading(27, PlanRouteSamples(20.f, 30.f, 6000.f).spacingYards, {}),
                     OverseerDecisions::RouteLimits{}).lethalYards == 0.f,
          true);
}

// A walk longer than the cap is read up to the cap and no further, and the
// unread tail is safe in one direction only: it can hide a refusal, never
// invent one.
void ALongWalkIsReadOnlyToTheCap()
{
    RouteSampling const plan = PlanRouteSamples(12000.f, 30.f, 6000.f);
    CheckNear("only the first 6,000 yards are read", plan.readYards, 6000.0f);
    CheckInt("in 200 samples", long(plan.samples), 200);
    Check("and the last one is inside the read length",
          RouteSampleAt(plan, plan.samples - 1) < plan.readYards, true);
}

// ---------------------------------------------------------- the third gate --

// The nearest counter this character may use, standing in safe ground, and the
// walk to it is the walk that killed them. A farther one wins - which is the
// same answer #250 and #267 already give for their own reasons, reached for a
// third one.
void ALethalWalkLosesToASaferOneFartherAway()
{
    std::vector<TravelTargetCandidate> const candidates = {
        {14964, 502.0f,  true, 0, 0, false, 594.0f, 65},
        {3495,  2012.7f, true, 0, 0, true,  0.0f,   0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(candidates);

    Check("a target is still chosen", choice.verdict == TravelTargetVerdict::Chosen, true);
    CheckInt("the one 1,500 yards farther on wins", choice.index, 1);
    CheckInt("one was refused for the ground on the way", long(choice.lethalRoutes), 1);
    CheckInt("and it is remembered", choice.nearestLethalRoute, 0);

    CheckText("and the log says which walk it would not make",
              TravelTargetExplanation(choice, candidates),
              "chose entry 3495 at 2013 yards over 1 nearer one(s) it cannot reach "
              "alive - the nearest of those is entry 14964 at 502 yards, across 594 "
              "yards of unbroken ground held to level 65");
}

// NOWHERE TO GO IS A FOURTH FACT AND NOT A SHADE OF THE THIRD. This is the
// state the family was actually in on the night: every counter it could use
// stood in clean ground, and every one of them was on the far side of Burning
// Steppes. "Pick a different shop" is not the answer to that; "this party
// cannot leave on foot" is.
void NowhereReachableIsARefusalOfItsOwn()
{
    std::vector<TravelTargetCandidate> const candidates = {
        {14964, 502.0f,  true, 0, 0, false, 594.0f, 65},
        {3495,  2012.7f, true, 0, 0, false, 444.0f, 58},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(candidates);

    Check("nothing is chosen", choice.verdict == TravelTargetVerdict::EveryRouteIsLethal, true);
    CheckInt("and it names no index", choice.index, -1);
    CheckInt("both were refused for their walks", long(choice.lethalRoutes), 2);

    CheckText("the refusal is its own sentence",
              TravelTargetExplanation(choice, candidates),
              "2 of them on this map are ones this character may use and stand in safe "
              "ground, and the walk to every one of them crosses ground it cannot "
              "survive - the nearest is entry 14964 at 502 yards, across 594 yards of "
              "unbroken ground held to level 65");
}

// THE THREE OLDER REFUSALS ARE UNCHANGED, which is the regression this gate is
// most likely to cause. A guard is still counted before a walk is, so the two
// tallies can never both claim the same spawn, and a guarded destination still
// names its guard rather than its route.
void AGuardIsStillAnsweredBeforeAWalkIs()
{
    std::vector<TravelTargetCandidate> const candidates = {
        {14964, 502.0f, true, 10, 65, false, 594.0f, 65},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(candidates);

    Check("the older verdict wins", choice.verdict == TravelTargetVerdict::EveryOneIsGuarded, true);
    CheckInt("the guard tally has it", long(choice.guarded), 1);
    CheckInt("and the walk tally does not", long(choice.lethalRoutes), 0);
}

// An unfriendly counter is still out before either of the other two questions
// is asked about it, and a candidate nobody measured is still a candidate.
void TheOlderGatesKeepTheirOrder()
{
    std::vector<TravelTargetCandidate> const candidates = {
        {14754, 100.0f,  false, 0, 0, false, 900.0f, 65},
        {3495,  2012.7f, true,  0, 0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(candidates);

    CheckInt("the unusable one is refused on interaction alone", long(choice.refused), 1);
    CheckInt("and is never counted as a lethal walk", long(choice.lethalRoutes), 0);
    CheckInt("the unmeasured one is chosen", choice.index, 1);
}

}  // namespace

int main()
{
    TheWalkThatKilledThemIsRefused();
    TheMildestLethalOneIsStillRefused();
    TheErrandItShouldStillRunIsUntouched();
    TheSameGroundReadsDifferentlyForADifferentCharacter();
    ScatteredTroubleIsNotACrossing();
    OneLongStretchIsRefusedOnLessTotalGround();
    AnUnmeasuredWalkIsNotAClaim();
    WhatCannotFightIsNotWhatIsMeasured();
    TheTwoPopulationsDoNotOverlap();
    TheExactMultipleNeverLostASample();
    EverySpanIsSpokenForEndToEnd();
    ARunThatEndsAtTheDestinationIsCountedToItsEnd();
    AShopInsideOneSpacingIsNotAWalk();
    ALongWalkIsReadOnlyToTheCap();
    ALethalWalkLosesToASaferOneFartherAway();
    NowhereReachableIsARefusalOfItsOwn();
    AGuardIsStillAnsweredBeforeAWalkIs();
    TheOlderGatesKeepTheirOrder();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a travel errand refuses a walk it cannot survive\n");
    return 0;
}
