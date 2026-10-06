/*
 * A creature spawn walk's drop guard reads the pathfinder's first waypoint
 * (2026-10-06, after the first-leg fix of #868 still refused the guild's class
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

float const LETHAL = D::LethalFallYards();

// Bigzug, map 1: start (-278.664, -3957.11, 101.338), Uzzek at z 27.3383.
void TheBarrensWarrior()
{
    float const startZ = 101.338f;
    float const spawnZ = 27.3383f;
    Check("the whole way down is a lethal drop (what a straight probe read)",
          startZ - spawnZ > LETHAL);
    std::vector<MailWalkPoint> const path = {
        {-278.664f, -3957.11f, startZ},
        {-250.f, -3940.f, 99.f},
        {-120.f, -3860.f, 90.f},
        {185.746f, -3597.21f, spawnZ},
    };
    D::FirstLegPick const pick = D::PickFirstLegWaypoint(path, startZ, 250.f, LETHAL);
    Check("the first leg is found", pick.found);
    Check("its drop is the first waypoint's, small", pick.firstDropYards < 5.f);
    Check("the leg ends at the furthest waypoint inside the reach", pick.point.z == 90.f);
    Check("the guard lets the walk start",
          *D::SpawnWalkFirstLegRefusal(true, false, pick.found) == '\0');
}

// Gronk and Hurk, map 0: start z 483.183, Ilsa Corbin at z 109.521.
void TheDwarfWarriors()
{
    float const startZ = 483.183f;
    Check("the spawn is far below the start", startZ - 109.521f > LETHAL);
    std::vector<MailWalkPoint> const path = {
        {-6049.f, -206.714f, startZ},
        {-6030.f, -190.f, 480.f},
        {-5900.f, -100.f, 470.f},
        {-8688.56f, 325.764f, 109.521f},
    };
    D::FirstLegPick const pick = D::PickFirstLegWaypoint(path, startZ, 250.f, LETHAL);
    Check("found", pick.found);
    Check("stops before the far waypoint", pick.point.z == 470.f);
}

void ALongFlatLegPasses()
{
    std::vector<MailWalkPoint> const path = {{0.f, 0.f, 50.f}, {400.f, 0.f, 50.f}};
    D::FirstLegPick const pick = D::PickFirstLegWaypoint(path, 50.f, 250.f, LETHAL);
    Check("a first waypoint beyond the reach is still the leg", pick.found);
    Check("and it is that waypoint", pick.point.x == 400.f);
}

void ARealCliffIsStillRefused()
{
    std::vector<MailWalkPoint> const path = {{0.f, 0.f, 100.f}, {5.f, 0.f, 20.f},
                                             {40.f, 0.f, 20.f}};
    D::FirstLegPick const pick = D::PickFirstLegWaypoint(path, 100.f, 250.f, LETHAL);
    Check("a first waypoint 80 yards down gives no leg", !pick.found);
    Check("and the drop is reported", pick.firstDropYards > LETHAL);
    Check("so the guard refuses with the bridge's words",
          std::strcmp(D::SpawnWalkFirstLegRefusal(true, false, pick.found),
                      D::SpawnWalkRefusal::SpawnFirstStepDrop) == 0);
}

void ADescentStopsBeforeTheLethalWaypoint()
{
    std::vector<MailWalkPoint> const path = {{0.f, 0.f, 100.f}, {10.f, 0.f, 90.f},
                                             {20.f, 0.f, 80.f}, {30.f, 0.f, 20.f}};
    D::FirstLegPick const pick = D::PickFirstLegWaypoint(path, 100.f, 250.f, LETHAL);
    Check("the leg ends at the last waypoint that survives", pick.found && pick.point.z == 80.f);
}

void TooShortAPathGivesNothing()
{
    Check("no waypoints", !D::PickFirstLegWaypoint({}, 0.f, 250.f, LETHAL).found);
    Check("one waypoint", !D::PickFirstLegWaypoint({{0.f, 0.f, 0.f}}, 0.f, 250.f, LETHAL).found);
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
    ARealCliffIsStillRefused();
    ADescentStopsBeforeTheLethalWaypoint();
    TooShortAPathGivesNothing();
    ADropRefusalIsRetryable();
    AGameobjectIsNeverRefused();
    TheAdapterLogsTheThreeHeights();
    if (failures)
        return 1;
    std::printf("ok test_spawn_walk_first_leg_waypoint\n");
    return 0;
}
