/*
 * A far walk does not walk a character down a road through ground far above
 * its level.
 *
 * MEASURED ON THE DEV REALM, 2026-10-07 TO 2026-10-10. Natural guild members at
 * levels 11 to 24 died in Searing Gorge (creatures 43 to 50) about 400 times a
 * day, Mok alone 152 times in 22 hours. Of 72 times a member came into the gorge
 * from somewhere else, 51 were the guild jobs' own far walks ending "died on the
 * way to the spawn": a level walk to the Sentinel Hill flight master or a class
 * quest walk to a Stormwind trainer, set off from Dun Morogh or Loch Modan. The
 * travel survey's road between the two halves of the Eastern Kingdoms runs
 * through Searing Gorge and Burning Steppes, and the far walk followed it on
 * foot. Its first death in the gorge ended the walk; the member hearthed home
 * to Dun Morogh, and the next walk took the same road.
 *
 * A player of level 13 does not walk through Searing Gorge to reach Stormwind.
 * So a far walk reads the road it was given, the way the roster reads a
 * destination's ground: the worst creature within the threat radius of points
 * along it, judged by JudgeRoute's own `??` rule (ten levels over, two hundred
 * yards unbroken). A road that fails it is not walked. The flight logic still
 * gets its turn first, so a walker that can fly round it does.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings, plus a read of the module source for the wiring.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;
namespace M = OverseerDecisions::MailWalkRefusal;
namespace E = OverseerDecisions::ErrandWalkRefusal;
namespace S = OverseerDecisions::SpawnWalkRefusal;

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

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    if (std::fabs(got - want) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// THE ROAD IS READ WHERE IT GOES, not on the straight line. Samples stand at
// the middle of equal spacings laid end to end along the route's own points,
// so each one speaks for one spacing of road, as JudgeRoute counts them.
void TheRoadIsSampledAlongItsPoints()
{
    std::vector<RoutePoint> straight{{0.f, 0.f, 0.f}, {300.f, 0.f, 0.f}};
    RoadSamples road = RoadSamplesAlong(straight, 0, 30.f, 400);
    CheckNumber("300 yards at 30 is ten samples", road.points.size(), 10);
    CheckNear("each speaks for 30 yards", road.spacingYards, 30.f);
    if (road.points.size() == 10)
    {
        CheckNear("the first sits at the middle of the first spacing", road.points[0].x, 15.f);
        CheckNear("the last at the middle of the last", road.points[9].x, 285.f);
    }

    // An L: 120 yards east, then 120 north. The sample past the corner is on
    // the second leg, not on the line between the ends.
    std::vector<RoutePoint> bend{{0.f, 0.f, 0.f}, {120.f, 0.f, 0.f}, {120.f, 120.f, 0.f}};
    road = RoadSamplesAlong(bend, 0, 30.f, 400);
    CheckNumber("240 yards round a corner is eight samples", road.points.size(), 8);
    if (road.points.size() == 8)
    {
        CheckNear("past the corner it walks north", road.points[5].x, 120.f);
        CheckNear("...45 yards up the second leg", road.points[5].y, 45.f);
    }

    // From the cursor on: road already walked is not read again.
    road = RoadSamplesAlong(bend, 1, 30.f, 400);
    CheckNumber("from the corner, only the second leg", road.points.size(), 4);

    // A very long road is still read end to end, with the spacing widened to
    // stay inside the sample budget.
    std::vector<RoutePoint> far{{0.f, 0.f, 0.f}, {12000.f, 0.f, 0.f}};
    road = RoadSamplesAlong(far, 0, 30.f, 200);
    CheckNumber("a 12,000 yard road keeps to the budget", road.points.size(), 200);
    CheckNear("...at 60 yards a sample", road.spacingYards, 60.f);
    if (!road.points.empty())
        CheckNear("...and reaches its far end", road.points.back().x, 11970.f);

    CheckNumber("no road, no samples", RoadSamplesAlong({}, 0, 30.f, 400).points.size(), 0);
    CheckNumber("one point is no road",
                RoadSamplesAlong({{5.f, 5.f, 0.f}}, 0, 30.f, 400).points.size(), 0);
    CheckNumber("a cursor past the end reads nothing",
                RoadSamplesAlong(bend, 7, 30.f, 400).points.size(), 0);
}

RouteReading Reading(uint32_t level, std::vector<uint32_t> worst)
{
    RouteReading reading;
    reading.characterLevel = level;
    reading.sampleSpacingYards = 30.f;
    reading.worstLevelAtSample = std::move(worst);
    return reading;
}

// THE GORGE, AS A LEVEL 13 READS IT. Dark Iron Geologists (44), Tempered War
// Golems (47), Magma Elementals (48) and Greater Lava Spiders (49) stand along
// the survey's road from the Loch Modan side to Blackrock Mountain; read from
// the world's spawns at 30 yards a sample, more than 400 yards of it is
// unbroken. Kharanos to the gates of Ironforge reads nothing.
void TheGorgeRoadIsNotWalked()
{
    RouteLimits const limits = FarWalkRoadLimits();
    CheckNumber("the `??` rule: ten levels over", limits.unknownLevelDiff, 10);
    CheckNear("...for two hundred yards unbroken", limits.lethalRunYards, 200.f);

    std::vector<uint32_t> gorge(15, 47);
    RouteVerdict v = JudgeRoute(Reading(13, gorge), limits);
    Check("450 yards of level 47 ground is not walked at 13", v.survivable, false);
    CheckNumber("...and the worst of it is named", v.worstLevel, 47);
    Check("...nor at 24", JudgeRoute(Reading(24, gorge), limits).survivable, false);
    Check("a level 40 walks it", JudgeRoute(Reading(40, gorge), limits).survivable, true);

    std::vector<uint32_t> kharanos(40, 0);
    Check("an empty road is walked", JudgeRoute(Reading(13, kharanos), limits).survivable, true);

    // A camp or two passed on the way is a walk through a contested zone, not
    // a road through somewhere it cannot live.
    std::vector<uint32_t> camps{0, 0, 30, 30, 0, 0, 0, 30, 30, 30, 0, 0};
    Check("two short camps are walked", JudgeRoute(Reading(13, camps), limits).survivable, true);
}

// THE WALK'S VERDICT. A road found lethal ends the walk, after anything that
// already ended it and after the flight leg the walk boarded; an arrival still
// counts as one.
void ALethalRoadEndsTheWalk()
{
    MailWalkFacts f;
    f.present = true;
    f.alive = true;
    f.sameMap = true;
    f.timeoutMs = 600000;
    f.roadLethal = true;
    Check("a lethal road ends the walk",
          JudgeMailWalk(f) == MailWalkState::LethalRoad, true);

    MailWalkFacts g = f;
    g.alive = false;
    Check("a death is still a death", JudgeMailWalk(g) == MailWalkState::Died, true);

    g = f;
    g.onFlightLeg = true;
    Check("a walker in the air flies on", JudgeMailWalk(g) == MailWalkState::Flying, true);

    g = f;
    g.mailboxInReach = true;
    Check("arrived is arrived", JudgeMailWalk(g) == MailWalkState::Arrived, true);

    g = f;
    g.roadLethal = false;
    Check("an ordinary road walks", JudgeMailWalk(g) == MailWalkState::Walking, true);
}

// The words, per goal, and that nobody asks again for the same road soon: a
// road through somewhere ten levels over does not move by the next pass.
void TheEndingIsSaid()
{
    CheckText("word", MailWalkStateWord(MailWalkState::LethalRoad), "lethal_road");
    CheckText("mailbox", MailWalkEndReason(MailWalkState::LethalRoad), M::LethalRoad);
    CheckText("spawn", WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::LethalRoad),
              S::SpawnLethalRoad);
    CheckText("trainer", WalkEndReasonFor(WalkGoal::Trainer, MailWalkState::LethalRoad),
              E::TrainerLethalRoad);
    CheckText("vendor", WalkEndReasonFor(WalkGoal::Vendor, MailWalkState::LethalRoad),
              E::VendorLethalRoad);
    Check("the mailbox ending is not retryable", MailWalkRefusalRetryable(M::LethalRoad), false);
    Check("the spawn ending is not retryable",
          ErrandWalkRefusalRetryable(S::SpawnLethalRoad), false);
    Check("the trainer ending is not retryable",
          ErrandWalkRefusalRetryable(E::TrainerLethalRoad), false);
    Check("the vendor ending is not retryable",
          ErrandWalkRefusalRetryable(E::VendorLethalRoad), false);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty(), true);
    Check("the far walk samples its own road",
          source.find("OverseerDecisions::RoadSamplesAlong(") != std::string::npos, true);
    Check("...judges it by the far walk's limits",
          source.find("OverseerDecisions::FarWalkRoadLimits()") != std::string::npos, true);
    Check("...and hands the verdict to the walk's poll",
          source.find("facts.roadLethal = ev.roadLethal") != std::string::npos, true);
    Check("the deploy proof is said",
          source.find("crosses ground far above its level") != std::string::npos, true);
}

}  // namespace

int main()
{
    TheRoadIsSampledAlongItsPoints();
    TheGorgeRoadIsNotWalked();
    ALethalRoadEndsTheWalk();
    TheEndingIsSaid();
    TheAdapterIsWired();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the far walk reads its road\n");
    return EXIT_SUCCESS;
}
