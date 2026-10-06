/*
 * A creature spawn walk's drop guard reads the pathfinder's first waypoint
 * (2026-10-06; #872 judged the net drop of the leg and still refused the class
 * quest walks).
 *
 * Live rows on the dev realm, every pass: a warrior in the Barrens at z 101.338
 * sent to a quest giver at z 27.338, and two warriors near Ironforge at z
 * 483.183 sent to a quest giver at z 109.521, were all refused "the first step
 * toward the creature goes over a drop", and the row said retryable:false. The
 * spawn stands 74 and 374 yards below the start; neither is a cliff at the
 * character's feet. The waypoint heights below are built to match a descent of
 * the shape the survey records (start and spawn heights are the live ones).
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace D = OverseerDecisions;
using D::MailWalkPoint;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr float STRIDE = 5.f;
constexpr float SAFE = 10.f;   // TRAVEL_GROUND_DROP_YARDS

D::FirstLegPick Pick(std::vector<MailWalkPoint> const& path)
{
    return D::PickFirstLegWaypoint(path, 250.f, STRIDE, SAFE);
}

// Bigzug, map 1, live on 2026-10-06 22:36: bot z 101.338, the path's first
// waypoint is the destination at z 27.2, 592 yards off. The net drop is 74
// yards (more than a fall survives) over a seven degree ramp.
void TheBarrensWarrior()
{
    std::vector<MailWalkPoint> const path = {
        {-278.664f, -3957.11f, 101.338f},
        {185.746f, -3597.21f, 27.2f},
    };
    float const net = 101.338f - 27.2f;
    Check("the net drop of the leg is over the lethal fall (what failed live)",
          net > D::LethalFallYards());
    D::FirstLegPick const pick = Pick(path);
    Check("the ramp passes the guard", pick.found);
    Check("its steepest stride drops under a yard", pick.firstStrideDrop < 1.f);
    Check("the step is the reach along the leg, not the far end",
          pick.point.x < path[1].x && pick.point.z < 101.338f && pick.point.z > 27.2f);
    Check("the guard lets the walk start",
          *D::SpawnWalkFirstLegRefusal(true, false, pick.found) == '\0');
}

// Gronk (z 483.183) and Hurk (z 423.1), map 0, live: the path's first waypoint
// is the destination at z 109.4, thousands of yards off.
void TheDwarfWarriors()
{
    for (float startZ : {483.183f, 423.1f})
    {
        std::vector<MailWalkPoint> const path = {
            {-6049.f, -206.714f, startZ},
            {-8688.56f, 325.764f, 109.4f},
        };
        D::FirstLegPick const pick = Pick(path);
        Check("a long ramp far below the start passes", pick.found);
    }
}

void ALongFlatLegPasses()
{
    D::FirstLegPick const pick = Pick({{0.f, 0.f, 50.f}, {400.f, 0.f, 50.f}});
    Check("found", pick.found);
    Check("the step is the reach along it", pick.point.x == 250.f);
}

void ATrueCliffInTheFirstTenYardsIsRefused()
{
    D::FirstLegPick const pick = Pick({{0.f, 0.f, 100.f}, {10.f, 0.f, 20.f}, {400.f, 0.f, 20.f}});
    Check("80 yards down in 10 yards gives no leg", !pick.found);
    Check("the stride drop is reported", pick.firstStrideDrop > SAFE);
    Check("so the guard refuses with the bridge's words",
          std::strcmp(D::SpawnWalkFirstLegRefusal(true, false, pick.found),
                      D::SpawnWalkRefusal::SpawnFirstStepDrop) == 0);
}

void ASteepStrideAfterAGoodOneEndsTheLegBeforeIt()
{
    D::FirstLegPick const pick =
        Pick({{0.f, 0.f, 100.f}, {50.f, 0.f, 95.f}, {55.f, 0.f, 20.f}});
    Check("the leg is the good part", pick.found && pick.point.z == 95.f);
}

void TooShortAPathGivesNothing()
{
    Check("no waypoints", !Pick({}).found);
    Check("one waypoint", !Pick({{0.f, 0.f, 0.f}}).found);
}

void ADropRefusalIsRetryable()
{
    Check("the character moves, so the leg changes",
          D::ErrandWalkRefusalRetryable(D::SpawnWalkRefusal::SpawnFirstStepDrop));
    Check("a malformed row is still not",
          !D::ErrandWalkRefusalRetryable(D::SpawnWalkRefusal::MalformedSpawn));
}

void AGameobjectIsNeverRefused()
{
    Check("not guarded", *D::SpawnWalkFirstLegRefusal(false, false, false) == '\0');
    Check("a probe step suffices", *D::SpawnWalkFirstLegRefusal(true, true, false) == '\0');
}

void TheAdapterLogsTheThreeHeights()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("a refusal logs bot, waypoint and spawn heights at INFO",
          source.find("refused first leg: bot z") != std::string::npos);
}

}  // namespace

int main()
{
    TheBarrensWarrior();
    TheDwarfWarriors();
    ALongFlatLegPasses();
    ATrueCliffInTheFirstTenYardsIsRefused();
    ASteepStrideAfterAGoodOneEndsTheLegBeforeIt();
    TooShortAPathGivesNothing();
    ADropRefusalIsRetryable();
    AGameobjectIsNeverRefused();
    TheAdapterLogsTheThreeHeights();
    if (failures)
        return 1;
    std::printf("ok test_spawn_walk_first_leg_waypoint\n");
    return 0;
}
