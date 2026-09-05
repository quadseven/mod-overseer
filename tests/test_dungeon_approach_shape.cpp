/*
 * "Above it" and "near it" are different facts (mod-overseer#217).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter measures the two halves of the gap; this test pins what
 * they mean, on the numbers two real dungeon approaches actually produced.
 *
 * EVERY COORDINATE AND EVERY PAIR BELOW WAS MEASURED, not chosen to make a
 * threshold look good. The Deadmines row is one leader's approach sampled as it
 * happened; the Wailing Caverns rows are the family standing on the high ground
 * over the door, on the night six of six deaths on that approach were falls.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using OverseerDecisions::ApproachDistance;
using OverseerDecisions::ApproachGap;
using OverseerDecisions::ApproachLimits;
using OverseerDecisions::ApproachShape;
using OverseerDecisions::ApproachShapeOf;
using OverseerDecisions::ApproachWhere;
using OverseerDecisions::DungeonRunBarrierBlockers;
using OverseerDecisions::DungeonRunBarrierMet;
using OverseerDecisions::DungeonRunMemberState;
using OverseerDecisions::RatchetLimits;
using OverseerDecisions::RatchetReading;
using OverseerDecisions::StagingNudge;
using OverseerDecisions::StagingStallState;
using OverseerDecisions::StagingWatchdog;

namespace
{

int failures = 0;

char const* Name(ApproachShape shape)
{
    switch (shape)
    {
        case ApproachShape::Unmeasured: return "Unmeasured";
        case ApproachShape::Arrived:    return "Arrived";
        case ApproachShape::Closing:    return "Closing";
        case ApproachShape::Overhead:   return "Overhead";
    }
    return "?";
}

// The adapter's own numbers: DUNGEON_BARRIER_RADIUS_YARDS, TRAVEL_STEP_YARDS
// and TRAVEL_STEP_VERTICAL_YARDS. Written here rather than invented, because
// the whole argument for the rule is that it extends the step bound the module
// already has rather than introducing a gradient of its own.
constexpr ApproachLimits LIMITS{10.f, 60.f, 20.f};

ApproachGap Gap(float horizontal, float vertical)
{
    ApproachGap gap;
    gap.horizontalYards = horizontal;
    gap.verticalYards = vertical;
    gap.measured = true;
    return gap;
}

void Check(char const* what, ApproachShape got, ApproachShape want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// THE DEADMINES APPROACH, sampled while it happened. Staging point
// (-11208.2, 1665.34, 24.66); each pair is horizontal distance from it and
// height above it. The first three are the leader crossing open country and
// they must stay walkable, or every long overland approach breaks. The last
// three are him on the quarry rim, where the party then sat out the backstop.
void TheDeadminesApproach()
{
    Check("1029y out, 22y up - open country",
          ApproachShapeOf(Gap(1029.f, 22.0f), LIMITS), ApproachShape::Closing);
    Check("534y out, 10y up - open country",
          ApproachShapeOf(Gap(534.f, 10.2f), LIMITS), ApproachShape::Closing);
    Check("205y out, 31y up - still on the valley floor",
          ApproachShapeOf(Gap(205.f, 30.9f), LIMITS), ApproachShape::Closing);

    Check("99y out, 88y up - on the rim",
          ApproachShapeOf(Gap(99.f, 88.3f), LIMITS), ApproachShape::Overhead);
    Check("52y out, 83y up - on the rim",
          ApproachShapeOf(Gap(52.f, 83.2f), LIMITS), ApproachShape::Overhead);
    Check("29y out, 43y up - part way down",
          ApproachShapeOf(Gap(29.f, 43.1f), LIMITS), ApproachShape::Overhead);
}

// WAILING CAVERNS, staging point (-733.71, -2214.91, 16.8). Two samples of the
// family on the high ground above the door, taken hours apart.
void TheWailingCavernsApproach()
{
    Check("10y out and 150y up is not 10 yards away",
          ApproachShapeOf(Gap(10.f, 150.2f), LIMITS), ApproachShape::Overhead);
    Check("80y out, 184y up", ApproachShapeOf(Gap(80.f, 184.2f), LIMITS),
          ApproachShape::Overhead);
    Check("99y out, 80y up", ApproachShapeOf(Gap(99.f, 80.2f), LIMITS),
          ApproachShape::Overhead);
    Check("210y out, 71y up", ApproachShapeOf(Gap(210.f, 71.2f), LIMITS),
          ApproachShape::Overhead);

    // The second sample, taken between the first and sixth fall death.
    Check("22y out, 151y up", ApproachShapeOf(Gap(22.f, 151.f), LIMITS),
          ApproachShape::Overhead);
    Check("32y out, 114y up", ApproachShapeOf(Gap(32.f, 114.f), LIMITS),
          ApproachShape::Overhead);
    Check("74y out, 194y up", ApproachShapeOf(Gap(74.f, 194.f), LIMITS),
          ApproachShape::Overhead);
    Check("282y out, 117y up", ApproachShapeOf(Gap(282.f, 117.f), LIMITS),
          ApproachShape::Overhead);
}

// THE ORDINARY CASE MUST STILL BE ORDINARY. A character standing at a staging
// point is a yard or two off in z, and a rule that called that a cliff would
// refuse every arrival there has ever been. Deadmines and Stockades are both
// approached across ground with no drop worth the name.
void ArrivingIsStillArriving()
{
    Check("on the point", ApproachShapeOf(Gap(0.f, 0.f), LIMITS),
          ApproachShape::Arrived);
    Check("3y out, 1y up", ApproachShapeOf(Gap(3.f, 1.f), LIMITS),
          ApproachShape::Arrived);
    Check("9y out, 2y below", ApproachShapeOf(Gap(9.f, -2.f), LIMITS),
          ApproachShape::Arrived);
    // The floor: a gap one step may bridge is never a cliff, at any range.
    Check("9y out, 19y up is still a step's worth",
          ApproachShapeOf(Gap(9.f, 19.f), LIMITS), ApproachShape::Arrived);
    Check("just outside the radius, level ground",
          ApproachShapeOf(Gap(11.f, 1.f), LIMITS), ApproachShape::Closing);
    // Stockades: the door is in a Stormwind courtyard and the approach is flat.
    Check("Stockades, 30y out across the canal district",
          ApproachShapeOf(Gap(30.f, 5.f), LIMITS), ApproachShape::Closing);
}

// AT ONE STEP'S REACH THE TWO RULES ARE THE SAME RULE. StepMayBridgeGap already
// refuses a step of sixty yards that climbs more than twenty; this must agree
// with it exactly there, or the module holds two opinions about one cliff.
void ItAgreesWithTheStepBound()
{
    for (float gapYards : {19.f, 20.f, 21.f, 25.f})
    {
        bool const stepAllows = OverseerDecisions::StepMayBridgeGap(
            60.f, gapYards, LIMITS.stepYards, LIMITS.stepVerticalYards);
        bool const walkable =
            ApproachShapeOf(Gap(60.f, gapYards), LIMITS) != ApproachShape::Overhead;
        CheckBool("one step's reach agrees with the step bound", walkable, stepAllows);
    }
}

// BELOW IS THE SAME DEFECT UPSIDE DOWN. A door on a ledge above the approach is
// as unreachable by walking toward it as one at the bottom of a pit.
void UnderneathIsAlsoOverhead()
{
    Check("52y out and 83y below", ApproachShapeOf(Gap(52.f, -83.2f), LIMITS),
          ApproachShape::Overhead);
    Check("10y out and 150y below", ApproachShapeOf(Gap(10.f, -150.2f), LIMITS),
          ApproachShape::Overhead);
}

// NO READING IS NOT AN ARRIVAL. A member on another map has no gap to this
// point at all, and the shape must fail to the answer that keeps a caller
// waiting rather than to the one that advances a phase.
void AnUnmeasuredGapIsNeitherThing()
{
    ApproachGap gap;
    gap.horizontalYards = 0.f;
    gap.verticalYards = 0.f;
    gap.measured = false;
    Check("nothing measured", ApproachShapeOf(gap, LIMITS), ApproachShape::Unmeasured);
    CheckBool("and no distance either", ApproachDistance(gap) < 0.f, true);
    CheckText("and it says so", ApproachWhere(gap), "no reading");
}

// A READING THAT IS NOT A NUMBER IS NOT AN ARRIVAL EITHER. Unreachable through
// the adapter - a staging point is checked before anything is measured against
// it - but the failure it would cause is the exact one this whole section
// exists to stop, so it is pinned rather than argued about.
void ANonNumberIsNotAnArrival()
{
    float const nan = std::numeric_limits<float>::quiet_NaN();
    float const inf = std::numeric_limits<float>::infinity();
    ApproachGap const cases[] = {
        Gap(nan, 5.f), Gap(5.f, nan), Gap(inf, 5.f), Gap(5.f, inf)};
    for (ApproachGap const& gap : cases)
    {
        Check("not a number is not a shape", ApproachShapeOf(gap, LIMITS),
              ApproachShape::Unmeasured);
        CheckBool("and not a distance", ApproachDistance(gap) < 0.f, true);
        CheckText("and not a sentence", ApproachWhere(gap), "no reading");
    }
}

// A CALLER WITH NO STEP BOUND GETS THE FLAT TEST IT USED TO HAVE. The zero is
// a statement that there is nothing to extend, not a request for a default.
void WithoutStepLimitsNothingIsOverhead()
{
    constexpr ApproachLimits flat{10.f, 0.f, 0.f};
    Check("10y out, 150y up, no step bound supplied",
          ApproachShapeOf(Gap(10.f, 150.2f), flat), ApproachShape::Arrived);
    Check("52y out, 83y up, no step bound supplied",
          ApproachShapeOf(Gap(52.f, 83.2f), flat), ApproachShape::Closing);
}

// THE READING THE RATCHET IS FED. Ten yards out and a hundred and fifty up is a
// hundred and fifty yard reading, which is the whole point: the horizontal span
// said ten and made a stalled climb look like an arrival.
void TheDistanceIsTheWholeGap()
{
    float const climbed = ApproachDistance(Gap(10.f, 150.2f));
    CheckBool("10/150 reads as about 150", climbed > 150.f && climbed < 151.f, true);
    float const level = ApproachDistance(Gap(52.f, 0.f));
    CheckBool("level ground reads as the span", level > 51.9f && level < 52.1f, true);
    float const rim = ApproachDistance(Gap(52.f, 83.2f));
    CheckBool("52/83 reads as about 98", rim > 97.f && rim < 99.f, true);
}

// THE WORDS AN OPERATOR READS. The staging failures for the whole of #217 said
// "924y from the staging point" and "80y away", which name the symptom.
void TheRefusalNamesTheCause()
{
    CheckText("above it", ApproachWhere(Gap(80.f, 184.2f)),
              "80y out and 184y above it");
    CheckText("below it", ApproachWhere(Gap(52.f, -83.2f)),
              "52y out and 83y below it");
    CheckText("level", ApproachWhere(Gap(534.f, 0.4f)), "534y out");
}

DungeonRunMemberState Member(char const* name, float horizontal, float vertical)
{
    DungeonRunMemberState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.distanceFromStage = horizontal;
    state.verticalFromStage = vertical;
    return state;
}

// THE BARRIER CANNOT BE SATISFIED FROM A CLIFFTOP. Five characters standing
// within ten yards of the staging point's x and y, and a hundred and fifty
// yards above it, used to be an assembled party.
void TheBarrierIsNotMetFromAbove()
{
    std::vector<DungeonRunMemberState> onTheRim;
    onTheRim.push_back(Member("one", 4.f, 150.2f));
    onTheRim.push_back(Member("two", 8.f, 148.f));
    CheckBool("a party on the rim is not assembled",
              DungeonRunBarrierMet(onTheRim, LIMITS), false);

    std::vector<DungeonRunMemberState> atTheDoor;
    atTheDoor.push_back(Member("one", 4.f, 1.f));
    atTheDoor.push_back(Member("two", 8.f, -2.f));
    CheckBool("a party at the door still is",
              DungeonRunBarrierMet(atTheDoor, LIMITS), true);

    CheckText("and the line says which it is",
              DungeonRunBarrierBlockers(onTheRim, LIMITS),
              "one (4y out and 150y above it), two (8y out and 148y above it)");
}

// THE WATCHDOG. A member above the point and no longer closing is a diagnosis,
// not a rung: none of the three corrections can move a cliff, and two of them
// restart movement at the edge of one.
void AStalledClimbIsStrandedAndNotNudged()
{
    constexpr RatchetLimits RATCHET{RatchetReading::DistanceToTarget, 10.f, 90};
    StagingStallState state;
    time_t now = 1000;

    // First poll on the rim: a mark, no clock elapsed, nothing said.
    Check("still overhead", ApproachShapeOf(Gap(52.f, 83.2f), LIMITS),
          ApproachShape::Overhead);
    CheckBool("first poll is quiet",
              StagingWatchdog(state, Gap(52.f, 83.2f), true, now, RATCHET, LIMITS) ==
                  StagingNudge::Nothing,
              true);

    // A whole patience window later, no nearer.
    now += 91;
    CheckBool("stalled on the rim is Stranded",
              StagingWatchdog(state, Gap(52.f, 83.2f), true, now, RATCHET, LIMITS) ==
                  StagingNudge::Stranded,
              true);

    // And said once. The ladder is never entered.
    now += 91;
    CheckBool("said once and then nothing",
              StagingWatchdog(state, Gap(52.f, 83.2f), true, now, RATCHET, LIMITS) ==
                  StagingNudge::Nothing,
              true);
}

// AND A MEMBER THAT IS MERELY STUCK STILL GETS THE LADDER IT ALWAYS HAD. The
// three corrections are for a character that has stopped walking on ground it
// could walk, and that case is unchanged.
void AStalledWalkStillClimbsTheLadder()
{
    constexpr RatchetLimits RATCHET{RatchetReading::DistanceToTarget, 10.f, 90};
    StagingStallState state;
    time_t now = 1000;

    StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET, LIMITS);
    now += 91;
    CheckBool("rung one", StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET,
                                          LIMITS) == StagingNudge::Restrategy,
              true);
    now += 91;
    CheckBool("rung two", StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET,
                                          LIMITS) == StagingNudge::Reaim,
              true);
    now += 91;
    CheckBool("rung three", StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET,
                                            LIMITS) == StagingNudge::ClearMovement,
              true);
    now += 91;
    CheckBool("give up", StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET,
                                         LIMITS) == StagingNudge::GiveUp,
              true);
}

// A PARTY DESCENDING A REAL RAMP IS NOT STRANDED, and this is the reason the
// verdict needs both halves. The Deadmines approach read Overhead at 99/+88,
// 52/+83 and 29/+43 on its way down to twelve yards out; a rule that acted on
// Overhead alone would have written off the one approach that got there.
void ADescentInProgressIsLeftAlone()
{
    constexpr RatchetLimits RATCHET{RatchetReading::DistanceToTarget, 10.f, 90};
    StagingStallState state;
    time_t now = 1000;

    struct Sample { float horizontal; float vertical; };
    Sample const descent[] = {
        {99.f, 88.3f}, {52.f, 83.2f}, {29.f, 43.1f}, {12.f, 8.f}};
    for (Sample const& sample : descent)
    {
        now += 91;
        StagingNudge const nudge = StagingWatchdog(
            state, Gap(sample.horizontal, sample.vertical), true, now, RATCHET, LIMITS);
        CheckBool("a descent in progress is never nudged",
                  nudge == StagingNudge::Nothing, true);
    }
}

// ARRIVING RESETS THE WATCH RATHER THAN ESCAPING IT. The old caller dropped the
// state of anybody inside a flat ten yard circle, which is how the member on
// the rim was the one member never watched.
void ArrivalResetsTheLadder()
{
    constexpr RatchetLimits RATCHET{RatchetReading::DistanceToTarget, 10.f, 90};
    StagingStallState state;
    time_t now = 1000;

    StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET, LIMITS);
    now += 91;
    StagingWatchdog(state, Gap(549.f, 3.f), true, now, RATCHET, LIMITS);  // rung one
    CheckBool("a rung was climbed", state.escalated == 1, true);

    now += 5;
    CheckBool("arriving is quiet",
              StagingWatchdog(state, Gap(2.f, 1.f), true, now, RATCHET, LIMITS) ==
                  StagingNudge::Nothing,
              true);
    CheckBool("and the ladder is back at the bottom", state.escalated == 0, true);

    // The rim is NOT an arrival, so it does not get that reset.
    StagingStallState rim;
    now = 1000;
    StagingWatchdog(rim, Gap(4.f, 150.2f), true, now, RATCHET, LIMITS);
    CheckBool("the rim is watched", rim.progress.seen, true);
    CheckBool("and its mark is the whole gap", rim.progress.best > 150.f, true);
}

}  // namespace

int main()
{
    TheDeadminesApproach();
    TheWailingCavernsApproach();
    ArrivingIsStillArriving();
    ItAgreesWithTheStepBound();
    UnderneathIsAlsoOverhead();
    AnUnmeasuredGapIsNeitherThing();
    ANonNumberIsNotAnArrival();
    WithoutStepLimitsNothingIsOverhead();
    TheDistanceIsTheWholeGap();
    TheRefusalNamesTheCause();
    TheBarrierIsNotMetFromAbove();
    AStalledClimbIsStrandedAndNotNudged();
    AStalledWalkStillClimbsTheLadder();
    ADescentInProgressIsLeftAlone();
    ArrivalResetsTheLadder();
    if (!failures)
        std::printf("above it is not near it: the approach shape holds\n");
    return failures ? 1 : 0;
}
