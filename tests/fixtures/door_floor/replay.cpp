#include "overseer_decisions.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using uint32 = uint32_t;
using dtPolyRef = uint64_t;
struct AreaTrigger {
  float x = 2593.94f, y = 1124.64f, z = 56.179f, radius = 11, length = 0,
        width = 0, height = 0, orientation = 0;
};
namespace G3D {
struct Vector3 {
  float x = 2575.47f, y = 1085.6f, z = 51.9772f;
};
} // namespace G3D
// Fixed navigation readings are the seam; the injected adapters and their
// trigger/endpoint decisions are compiled from the production sources.
bool loaded = true, meshPresent = true, noPath = false, queryFails = false,
     farEnd = false, shortcut = false;
unsigned pathPoints = 3;
float surface = 59.0f, nearestZ = 50.3626f;
int queries = 0;
struct dtQueryFilter {};
bool dtStatusFailed(unsigned status) { return status != 0; }
struct Query {
  unsigned findNearestPoly(float const *, float const *, dtQueryFilter const *,
                           dtPolyRef *ref, float *point) const {
    ++queries;
    *ref = queryFails ? 0 : 1;
    point[0] = 1125.33f;
    point[1] = nearestZ;
    point[2] = 2593.94f;
    return queryFails ? 1 : 0;
  }
} query;
struct Map {
  bool IsGridLoaded(float, float) const { return loaded; }
  Map &GetMapCollisionData() { return *this; }
  Map &GetMMapData() { return *this; }
  Query const *GetNavMeshQuery() const {
    return meshPresent ? &query : nullptr;
  }
} map;
struct Player {
  Map *GetMap() { return &map; }
} player;
namespace ObjectAccessor {
Player *FindPlayerByName(std::string const &) { return &player; }
} // namespace ObjectAccessor
bool SurfaceAt(Player *, float, float, float, float &height) {
  height = surface;
  return true;
}
constexpr float TRAVEL_ARRIVED_POSITION_YARDS = 5,
                DUNGEON_STAGING_Z_PROBE_LIFT_YARDS = 10,
                TRAVEL_ROUTE_ENDPOINT_YARDS = 5;
constexpr unsigned PATHFIND_NOPATH = 1, PATHFIND_FARFROMPOLY_END = 2,
                   PATHFIND_NOT_USING_PATH = 4, PATHFIND_SHORT = 8;
using PathType = unsigned;
struct PathGenerator {
  explicit PathGenerator(Player *) {}
  void SetUseStraightPath(bool) {}
  bool CalculatePath(float, float, float) { return true; }
  PathType GetPathType() const {
    return (noPath ? PATHFIND_NOPATH : 0) |
           (farEnd ? PATHFIND_FARFROMPOLY_END : 0) |
           (shortcut ? PATHFIND_NOT_USING_PATH : 0);
  }
  std::vector<G3D::Vector3> GetPath() const {
    return std::vector<G3D::Vector3>(pathPoints);
  }
  G3D::Vector3 GetActualEndPosition() const { return {}; }
};
// RUNTIME_BODIES
int main() {
  int failed = 0;
  auto check = [&](char const *name, bool condition) {
    if (!condition) {
      std::cerr << "FAIL " << name << "\n";
      ++failed;
    }
  };
  AreaTrigger door;
  std::vector<OverseerDecisions::DungeonRunEntryState> states(1);
  states[0].name = "Fixture";
  states[0].distanceFromDoor = 247;
  auto aim = DoorAimHeightFor(&door, states);
  check("surface mismatch selects the measured mesh floor",
        std::fabs(aim.z - nearestZ) < 0.001f);
  check("selected height passes the actual native route predicate",
        NavmeshRoutes(&player, door.x, door.y, aim.z));
  check("arrival still fits the actual trigger",
        aim.grounded &&
            OverseerDecisions::InsideAreaTrigger(
                TriggerShapeOf(door), door.x + TRAVEL_ARRIVED_POSITION_YARDS,
                door.y, aim.z, 0.f));
  surface = 50.64f;
  queries = 0;
  aim = DoorAimHeightFor(&door, states);
  check("working surface preserved without another mesh query",
        std::fabs(aim.z - surface) < 0.001f && queries == 0);
  surface = 59;
  meshPresent = false;
  aim = DoorAimHeightFor(&door, states);
  check("missing mesh preserves the existing surface", aim.z == surface);
  meshPresent = true;
  queryFails = true;
  aim = DoorAimHeightFor(&door, states);
  check("failed query preserves surface", aim.z == surface);
  queryFails = false;
  nearestZ = 100;
  aim = DoorAimHeightFor(&door, states);
  check("outside trigger candidate refused", aim.z == surface);
  nearestZ = 50.3626f;
  noPath = true;
  aim = DoorAimHeightFor(&door, states);
  check("disconnected candidate refused", aim.z == surface);
  noPath = false;
  farEnd = true;
  aim = DoorAimHeightFor(&door, states);
  check("off-mesh endpoint still refused", aim.z == surface);
  farEnd = false;
  pathPoints = 2;
  shortcut = true;
  aim = DoorAimHeightFor(&door, states);
  check("shortcut still refused", aim.z == surface);
  pathPoints = 3;
  shortcut = false;
  loaded = false;
  aim = DoorAimHeightFor(&door, states);
  check("unloaded grid preserves trigger height", aim.z == door.z);
  loaded = true;
  door.radius = 0;
  door.length = 4;
  door.width = 8;
  door.height = 2;
  door.orientation = 0.7f;
  aim = DoorAimHeightFor(&door, states);
  check("rotated box refuses a floor outside its height",
        aim.z == door.z && !aim.grounded);
  door.length = 14;
  door.width = 18;
  door.height = 16;
  aim = DoorAimHeightFor(&door, states);
  check("rotated box accepts a contained routed floor",
        aim.grounded && std::fabs(aim.z - nearestZ) < 0.001f);
  if (!failed)
    std::cout << "door-floor adapter replay passed\n";
  return failed ? 1 : 0;
}
