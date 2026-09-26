/*
 * The Ragefire Chasm door is reached by a measured corridor (mod-overseer#557,
 * #579).
 *
 * The door stands on the floor of the Cleft of Shadow at z -18, under
 * Orgrimmar's streets. With no corridor, 21 of the first 30 attempts on the dev
 * realm ended `staging_failed` with a member above the staging point and no
 * nearer. The corridor is the shortest walk over the shipped navmesh tiles from
 * the Orgrimmar inn to the derived staging point, and its row names the inn as
 * the campaign's home.
 *
 * THE FIXTURE IS THE PORTAL TABLE ITSELF, read out of src/mod_overseer.cpp, so
 * the row cannot drift from what these checks say about it. Run from the repo
 * root, as scripts/check-locally.sh does.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ApproachGap;
using OverseerDecisions::ApproachLeg;
using OverseerDecisions::ApproachLegStep;
using OverseerDecisions::ApproachLimits;
using OverseerDecisions::ApproachRoute;
using OverseerDecisions::ApproachRouteState;
using OverseerDecisions::ApproachShape;
using OverseerDecisions::ApproachShapeOf;
using OverseerDecisions::AreaTriggerShape;
using OverseerDecisions::DungeonStagingPoint;
using OverseerDecisions::DungeonStagingStandoffYards;
using OverseerDecisions::PlanStagingCorridor;
using OverseerDecisions::RoutePoint;
using OverseerDecisions::StagingCorridorLimits;
using OverseerDecisions::StagingCorridorPlan;
using OverseerDecisions::StagingCorridorVerdict;
using OverseerDecisions::StagingCorridorVerdictName;
using OverseerDecisions::StagingPoint;

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

// The adapter's numbers, named so a failure reads as the rule it broke.
constexpr float STANDOFF = 20.f;           // DUNGEON_STAGING_STANDOFF_YARDS
constexpr float BARRIER = 10.f;            // DUNGEON_BARRIER_RADIUS_YARDS
constexpr float STEP = 60.f;               // TRAVEL_STEP_YARDS
constexpr float STEP_VERTICAL = 20.f;      // TRAVEL_STEP_VERTICAL_YARDS
constexpr float JOIN_RADIUS = 15.f;        // close enough to step onto the corridor
constexpr float LOOKAHEAD = 250.f;         // TRAVEL_ROUTE_LOOKAHEAD_YARDS
constexpr float HOME_TOWN = 250.f;         // CAMPAIGN_HOME_TOWN_YARDS
// PathGenerator smooths at most MAX_POINT_PATH_LENGTH 74 points of
// SMOOTH_PATH_STEP_SIZE 4 under MOD_PLAYERBOTS. A corridor walked one point at
// a time needs every leg well inside that, and 90 yards of straight line is
// what the `wailing` descent's legs keep to.
constexpr float LEG_CLEAR = 90.f;

constexpr ApproachLimits LIMITS{BARRIER, STEP, STEP_VERTICAL};

// areatrigger 2230, the entry box, and 2226's landing on map 1, as the row
// quotes them.
AreaTriggerShape RagefireDoor()
{
    AreaTriggerShape shape;
    shape.x = 1818.4f;
    shape.y = -4427.26f;
    shape.z = -10.4478f;
    shape.length = 21.69f;
    shape.width = 11.83f;
    shape.height = 21.22f;
    shape.orientation = 0.576f;
    return shape;
}
constexpr float LAND_X = 1813.49f;
constexpr float LAND_Y = -4418.58f;
constexpr float LAND_Z = -18.57f;

// Innkeeper Gryshka, creature 6929, guid 4661, from the world database.
constexpr float INN_X = 1633.99f;
constexpr float INN_Y = -4439.37f;
constexpr float INN_Z = 15.7597f;

// Doras the flight master's tower, where a party flying in lands.
constexpr float TOWER_X = 1676.2f;
constexpr float TOWER_Y = -4313.4f;
constexpr float TOWER_Z = 61.4f;

// Where the leader stood on the cleft floor on 2026-09-24 at 07:35:34.
constexpr float FLOOR_X = 1794.1f;
constexpr float FLOOR_Y = -4370.2f;
constexpr float FLOOR_Z = -16.5f;

struct Row
{
    bool found{false};
    float approachX{0.f}, approachY{0.f}, approachZ{0.f};
    std::vector<RoutePoint> corridor;
    float homeX{0.f}, homeY{0.f}, homeZ{0.f};
};

// The row from `{"ragefire",` to the brace that closes it, with comments
// stripped, and every float literal in it in order: three for the approach
// start, three per corridor point, three for the home.
Row ReadRagefireRow()
{
    Row row;
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    std::string const all = text.str();
    std::size_t const start = all.find("{\"ragefire\", 1, 2230, 389, 2226,");
    if (start == std::string::npos)
        return row;
    row.found = true;

    std::string body;
    int depth = 0;
    for (std::size_t i = start; i < all.size(); ++i)
    {
        if (all.compare(i, 2, "//") == 0)
        {
            i = all.find('\n', i);
            if (i == std::string::npos)
                break;
            continue;
        }
        char const c = all[i];
        body.push_back(c);
        if (c == '{')
            ++depth;
        else if (c == '}' && --depth == 0)
            break;
    }

    std::regex const number(R"((-?\d+\.\d+)f)");
    std::vector<float> values;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), number);
         it != std::sregex_iterator(); ++it)
        values.push_back(std::strtof((*it)[1].str().c_str(), nullptr));

    if (values.size() < 6 || values.size() % 3 != 0)
        return row;
    row.approachX = values[0];
    row.approachY = values[1];
    row.approachZ = values[2];
    for (std::size_t i = 3; i + 3 < values.size(); i += 3)
        row.corridor.push_back({values[i], values[i + 1], values[i + 2]});
    row.homeX = values[values.size() - 3];
    row.homeY = values[values.size() - 2];
    row.homeZ = values[values.size() - 1];
    return row;
}

float Plane(float ax, float ay, float bx, float by)
{
    return std::hypot(ax - bx, ay - by);
}

ApproachGap Gap(float fromX, float fromY, float fromZ, float toX, float toY, float toZ)
{
    ApproachGap gap;
    gap.horizontalYards = Plane(fromX, fromY, toX, toY);
    gap.verticalYards = fromZ - toZ;
    gap.measured = true;
    return gap;
}

StagingPoint Staging()
{
    AreaTriggerShape const door = RagefireDoor();
    return DungeonStagingPoint(door.x, door.y, LAND_X, LAND_Y, LAND_Z,
                               DungeonStagingStandoffYards(door, STANDOFF, BARRIER));
}

StagingCorridorLimits AdapterLimits()
{
    StagingCorridorLimits limits;
    limits.joinYards = JOIN_RADIUS;
    limits.maxLegYards = LOOKAHEAD;
    limits.distantJoinAtEntry = true;
    return limits;
}

// A LEADER ON THE CITY'S UPPER WALKWAYS DOES NOT JOIN THE NEAREST LOWER
// CORRIDOR POINT THROUGH A WALL (#217, #396). The surveyed route must reach the
// corridor entry first, even when point 9 is only 303 yards away in a straight
// line.
void TheFarCityJoinStartsAtTheSurveyedEntry(Row const& row)
{
    if (row.corridor.size() < 2)
        return;
    RoutePoint const city{1769.f, -4082.f, 44.f};
    StagingCorridorPlan const plan = PlanStagingCorridor(
        row.corridor, row.approachX, row.approachY, city.x, city.y, AdapterLimits());
    Check("the far city position is routed to the corridor before joining",
          plan.verdict == StagingCorridorVerdict::TooFarToJoin);
    Check("the far join is the corridor entry rather than the nearest point",
          plan.joinIndex == 0);
}

void TheRowCarriesACorridorThatEndsOnTheStagingPoint(Row const& row)
{
    Check("the ragefire row is in the portal table", row.found);
    Check("the ragefire row carries a measured corridor", row.corridor.size() >= 2);
    if (row.corridor.size() < 2)
        return;

    StagingPoint const stage = Staging();
    RoutePoint const& last = row.corridor.back();
    Check("the corridor's last point is the derived staging point",
          Plane(last.x, last.y, stage.x, stage.y) < 0.5f);

    for (std::size_t i = 0; i + 1 < row.corridor.size(); ++i)
    {
        RoutePoint const& a = row.corridor[i];
        RoutePoint const& b = row.corridor[i + 1];
        if (Plane(a.x, a.y, b.x, b.y) > LEG_CLEAR)
        {
            std::printf("FAIL leg %zu -> %zu is %.1f yards of straight line\n", i, i + 1,
                        static_cast<double>(Plane(a.x, a.y, b.x, b.y)));
            ++failures;
        }
    }

    bool onIt = false;
    for (RoutePoint const& p : row.corridor)
        onIt = onIt || (Plane(p.x, p.y, row.approachX, row.approachY) < 0.5f &&
                        std::fabs(p.z - row.approachZ) < 0.5f);
    Check("the approach starts on a point of the corridor", onIt);
}

void TheHomeIsTheOrgrimmarInn(Row const& row)
{
    if (row.corridor.empty())
        return;
    Check("the home is Innkeeper Gryshka's spawn",
          Plane(row.homeX, row.homeY, INN_X, INN_Y) < 1.f &&
              std::fabs(row.homeZ - INN_Z) < 1.f);
    Check("and is well inside the campaign's own town radius of it",
          Plane(row.homeX, row.homeY, INN_X, INN_Y) < HOME_TOWN);
    RoutePoint const& first = row.corridor.front();
    Check("the corridor starts at the inn, so a walk to it has measured ground",
          Plane(first.x, first.y, INN_X, INN_Y) < 1.f);
}

void TheCorridorIsUsedForTheWalksThatNeedIt(Row const& row)
{
    if (row.corridor.size() < 2)
        return;
    StagingPoint const stage = Staging();

    // From the inn to the staging point, forward along every point.
    StagingCorridorPlan const down = PlanStagingCorridor(
        row.corridor, stage.x, stage.y, INN_X, INN_Y, AdapterLimits());
    if (down.verdict != StagingCorridorVerdict::Joined)
        std::printf("FAIL inn to door: %s\n", StagingCorridorVerdictName(down.verdict));
    Check("inn to door joins at the inn", down.joinIndex == 0);
    Check("inn to door walks the whole corridor forward",
          !down.reversed && down.route.size() == row.corridor.size());

    // From the cleft floor where the leader was walked away, it is the last
    // stretch only: the corridor never sends him back up.
    StagingCorridorPlan const floor = PlanStagingCorridor(
        row.corridor, stage.x, stage.y, FLOOR_X, FLOOR_Y, AdapterLimits());
    Check("the cleft floor joins the corridor",
          floor.verdict == StagingCorridorVerdict::Joined);
    Check("and walks at most its last two points",
          floor.route.size() <= 2 && !floor.reversed);

    // The way back out, from the door to the inn, walks it in reverse.
    StagingCorridorPlan const out = PlanStagingCorridor(
        row.corridor, INN_X, INN_Y, stage.x, stage.y, AdapterLimits());
    Check("the door to the inn is the corridor reversed",
          out.verdict == StagingCorridorVerdict::Joined && out.reversed &&
              out.route.size() == row.corridor.size());
}

void TheFirstLegIsWalkableFromWhereThePartyArrives(Row const& row)
{
    if (row.corridor.empty())
        return;
    StagingPoint const stage = Staging();

    // Why the approach is not the inn: from the tower the inn reads Overhead,
    // and IDLE would hold the run there.
    Check("from the tower, the inn reads Overhead",
          ApproachShapeOf(Gap(TOWER_X, TOWER_Y, TOWER_Z, INN_X, INN_Y, INN_Z), LIMITS) ==
              ApproachShape::Overhead);
    Check("from the tower, the approach start does not",
          ApproachShapeOf(Gap(TOWER_X, TOWER_Y, TOWER_Z, row.approachX, row.approachY,
                              row.approachZ),
                          LIMITS) != ApproachShape::Overhead);
    Check("from the inn, the approach start does not",
          ApproachShapeOf(Gap(INN_X, INN_Y, INN_Z, row.approachX, row.approachY,
                              row.approachZ),
                          LIMITS) != ApproachShape::Overhead);

    // And a leader already on the cleft floor is not sent back up to it.
    ApproachRoute route;
    route.hasWaypoint = true;
    route.leaderToWaypoint =
        Gap(FLOOR_X, FLOOR_Y, FLOOR_Z, row.approachX, row.approachY, row.approachZ);
    route.leaderToStagingPoint = Gap(FLOOR_X, FLOOR_Y, FLOOR_Z, stage.x, stage.y, LAND_Z);
    float const dx = row.approachX - stage.x;
    float const dy = row.approachY - stage.y;
    float const dz = row.approachZ - LAND_Z;
    route.waypointToStagingYards = std::sqrt(dx * dx + dy * dy + dz * dz);
    ApproachRouteState state;
    Check("a leader on the cleft floor walks straight at the door",
          ApproachLegStep(state, route, LIMITS) == ApproachLeg::Direct);
}

}  // namespace

int main()
{
    Row const row = ReadRagefireRow();
    TheRowCarriesACorridorThatEndsOnTheStagingPoint(row);
    TheHomeIsTheOrgrimmarInn(row);
    TheCorridorIsUsedForTheWalksThatNeedIt(row);
    TheFarCityJoinStartsAtTheSurveyedEntry(row);
    TheFirstLegIsWalkableFromWhereThePartyArrives(row);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
