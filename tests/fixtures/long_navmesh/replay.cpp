#include "overseer_decisions.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using uint32 = uint32_t;
namespace G3D {
struct Vector3 {
  float x, y, z;
};
} // namespace G3D
struct WorldPosition {
  uint32 map = 129;
  float x = 0, y = 0, z = 0;
  WorldPosition() = default;
  WorldPosition(uint32 m, float a, float b, float c)
      : map(m), x(a), y(b), z(c) {}
  uint32 GetMapId() const { return map; }
  float GetPositionX() const { return x; }
  float GetPositionY() const { return y; }
  float GetPositionZ() const { return z; }
};
struct Player : WorldPosition {
  bool alive = true, combat = false, world = true;
  uint32 instance = 1;
  Player() : WorldPosition(129, 2405.64f, 957.621f, 54.6379f) {}
  float GetExactDist2d(float a, float b) const {
    return std::hypot(x - a, y - b);
  }
  float GetAngle(float a, float b) const { return std::atan2(b - y, a - x); }
  float GetDistance2d(float a, float b) const { return GetExactDist2d(a, b); }
  bool IsInWorld() const { return world; }
  bool IsAlive() const { return alive; }
  bool IsInCombat() const { return combat; }
  uint32 GetInstanceId() const { return instance; }
};
struct AreaTrigger {
  uint32 map = 129;
  float x = 2593.94f, y = 1124.64f, z = 56.179f, radius = 11, length = 0,
        width = 0, height = 0, orientation = 0;
} door;
std::map<std::string, Player *> players;
namespace ObjectAccessor {
Player *FindPlayerByName(std::string const &name) {
  auto found = players.find(name);
  return found == players.end() ? nullptr : found->second;
}
} // namespace ObjectAccessor
Player *SteerableAI(Player *p) { return p && p->world ? p : nullptr; }
bool StepThroughAreaTrigger(std::string const &, Player *, std::string const &);
#define LOG_WARN(...)
constexpr unsigned PATHFIND_NOPATH = 1, PATHFIND_FARFROMPOLY_END = 2,
                   PATHFIND_NOT_USING_PATH = 4, PATHFIND_SHORT = 8;
using PathType = unsigned;
bool disconnected = false, shortcut = false, farEnd = false,
     shortLegRefused = false;
bool remoteStart = false, steepFirstLeg = false;
int nativeLegChecks = 0;
// Navigation readings are the fixture boundary. The pinned core's real
// FindSmoothPath, queried on the captured route, exhausted all 148 points;
// findStraightPath returned 62 corners. Only the measured first-leg prefix is
// needed here. The final endpoint reading is independent of that prefix.
struct PathGenerator {
  Player *bot;
  bool straight = false, near = false;
  float x = 0, y = 0, z = 0;
  explicit PathGenerator(Player *p) : bot(p) {}
  void SetUseStraightPath(bool value) { straight = value; }
  bool CalculatePath(float a, float b, float c) {
    x = a;
    y = b;
    z = c;
    near = bot->GetExactDist2d(a, b) < 100;
    if (near && !straight)
      ++nativeLegChecks;
    return true;
  }
  PathType GetPathType() const {
    if (disconnected || (near && shortLegRefused))
      return PATHFIND_NOPATH;
    if (shortcut)
      return PATHFIND_NOT_USING_PATH;
    if (farEnd)
      return PATHFIND_FARFROMPOLY_END;
    return !near && !straight ? PATHFIND_NOPATH : 0;
  }
  G3D::Vector3 GetActualEndPosition() const {
    return near ? G3D::Vector3{x, y, z}
                : G3D::Vector3{2575.47f, 1085.6f, 51.9772f};
  }
  std::vector<G3D::Vector3> GetPath() const {
    if (!near && !straight)
      return {{bot->x, bot->y, bot->z}, {x, y, z}};
    if (near)
      return {{bot->x, bot->y, bot->z},
              {(bot->x + x) / 2, (bot->y + y) / 2, (bot->z + z) / 2},
              {x, y, z}};
    return {{2405.64f, 957.621f, remoteStart ? 100.f : 55.5077f},
            {2392.8f, 988.f, steepFirstLeg ? 0.f : 54.6439f},
            {2393.33f, 989.067f, 53.0439f},
            {2396.53f, 989.6f, 53.3106f},
            {2422.13f, 989.867f, 50.3772f},
            {2422.67f, 989.6f, 49.8439f}};
  }
};
constexpr float TRAVEL_STEP_YARDS = 60, TRAVEL_GROUND_SNAP_YARDS = 3,
                TRAVEL_STEP_VERTICAL_YARDS = 20,
                TRAVEL_STEP_SIDE_FRACTION = .5f,
                TRAVEL_ROUTE_ENDPOINT_YARDS = 5,
                TRAVEL_FIRST_LEG_STRIDE_YARDS = 5, TRAVEL_GROUND_DROP_YARDS = 5,
                TRAVEL_STEP_MIN_YARDS = 8;
bool SurfaceAt(Player *, float, float, float, float &) { return false; }
bool GroundHolds(Player *, float, float, float &, float *) { return false; }
bool NothingInTheWay(Player *, float, float, float) { return false; }
// RUNTIME_BODIES
// This is the session/transport boundary. It models the core refusing any
// crossing outside the unchanged trigger; the production knock is injected.
bool StepThroughAreaTrigger(std::string const &, Player *p,
                            std::string const &aim) {
  if (aim != "trigger:444" || p->map != door.map || !p->alive || p->combat ||
      !OverseerDecisions::InsideAreaTrigger(TriggerShapeOf(door), p->x, p->y,
                                            p->z, 0.f))
    return false;
  p->map = 1;
  return true;
}
int main() {
  int failures = 0;
  auto check = [&](char const *what, bool value) {
    if (!value) {
      std::cerr << "FAIL " << what << "\n";
      ++failures;
    }
  };
  Player bot;
  WorldPosition goal(129, 2593.94f, 1124.64f, 50.3626f), step;
  WorldPosition nearby(129, 2412, 970, 54.7f);
  check("working native route stays direct", GroundedStep(&bot, nearby, step) &&
                                                 step.x == nearby.x &&
                                                 step.y == nearby.y);
  nativeLegChecks = 0;
  check("long smoothed path still refused",
        !NavmeshRoutes(&bot, goal.x, goal.y, goal.z));
  check("long routed walk produces a bounded native leg",
        GroundedStep(&bot, goal, step));
  check("leg remains under the ordinary stride bound",
        bot.GetExactDist2d(step.x, step.y) <= 60.01f);
  check("the actual short mover path was checked", nativeLegChecks > 0);
  shortLegRefused = true;
  check("refused mover leg is never issued", !GroundedStep(&bot, goal, step));
  shortLegRefused = false;
  disconnected = true;
  check("disconnected route stays refused", !GroundedStep(&bot, goal, step));
  disconnected = false;
  shortcut = true;
  check("missing mesh shortcut stays refused", !GroundedStep(&bot, goal, step));
  shortcut = false;
  farEnd = true;
  check("off-mesh destination stays refused", !GroundedStep(&bot, goal, step));
  farEnd = false;
  remoteStart = true;
  check("another floor is never used as the start",
        !GroundedStep(&bot, goal, step));
  remoteStart = false;
  steepFirstLeg = true;
  check("first-stride cliff stays refused", !GroundedStep(&bot, goal, step));
  steepFirstLeg = false;
  auto shape = TriggerShapeOf(door);
  bool const inside = OverseerDecisions::InsideAreaTrigger(
      shape, 2593.94f, 1119.2f, 50.6439f, 0.f);
  check("measured reachable endpoint is physically in the trigger", inside);
  std::vector<Player> roster(5);
  std::vector<std::string> names = {"Leader", "Healer", "Caster", "Melee",
                                    "Ranged"};
  for (unsigned i = 0; i < roster.size(); ++i) {
    roster[i].x = 2593.94f;
    roster[i].y = 1119.2f;
    roster[i].z = 50.6439f;
    players[names[i]] = &roster[i];
  }
  uint32 through = 0;
  auto census = [&] {
    return DungeonRunCensus(names, &door, 1, through, "Leader", true);
  };
  auto members = census();
  check("whole party physically in trigger can cross",
        OverseerDecisions::DungeonRunEntryReady(members, 5));
  if (OverseerDecisions::DungeonRunEntryReady(members, 5))
    check("all five cross through the injected native knock",
          DungeonRunKnock(members, "Leader", 444) == 5);
  for (auto &member : roster)
    member.map = 129;
  for (auto &member : roster) {
    member.x = door.x;
    member.y = door.y;
    member.z = 50.6439f;
  }
  roster[0].x = door.x;
  roster[0].y = door.y;
  roster[0].z = 0;
  check("center XY on another floor does not open the gate",
        !OverseerDecisions::DungeonRunEntryReady(census(), 5));
  for (auto &member : roster) {
    member.x = 2593.94f;
    member.y = 1119.2f;
    member.z = 50.6439f;
  }
  roster[0].world = false;
  check("missing member still holds the whole party",
        !OverseerDecisions::DungeonRunEntryReady(census(), 5));
  roster[0].world = true;
  roster[0].alive = false;
  check("dead member still holds the whole party",
        !OverseerDecisions::DungeonRunEntryReady(census(), 5));
  roster[0].alive = true;
  members = census();
  members[0].inCombat = true;
  check("combat still holds the whole party",
        !OverseerDecisions::DungeonRunEntryReady(members, 5));
  members[0].inCombat = false;
  members[0].otherCopy = true;
  check("another instance never counts as ready",
        !OverseerDecisions::DungeonRunEntryReady(members, 5));
  door.map = 1;
  for (auto &member : roster)
    member.instance = 2;
  roster[0].instance = 1;
  members = DungeonRunCensus(names, &door, 129, through, "Leader", false);
  check("native census rejects another instance copy",
        !OverseerDecisions::DungeonRunEntryReady(members, 5));
  door.map = 129;
  for (auto &member : roster)
    member.instance = 1;
  door.radius = 0;
  door.length = 14;
  door.width = 18;
  door.height = 16;
  door.orientation = .7f;
  for (auto &member : roster) {
    member.x = door.x - std::sin(door.orientation) * 6;
    member.y = door.y + std::cos(door.orientation) * 6;
    member.z = door.z;
  }
  check("rotated box containment is read by the actual census",
        OverseerDecisions::DungeonRunEntryReady(census(), 5));
  for (auto &member : roster) {
    member.x = door.x;
    member.y = door.y;
  }
  roster[0].z = door.z + 20;
  check("outside rotated box stays refused",
        !OverseerDecisions::DungeonRunEntryReady(census(), 5));
  OverseerDecisions::DoorRejoinFacts rejoin;
  rejoin.steerable = true;
  rejoin.alive = true;
  rejoin.leaderInsideKnownDoor = true;
  rejoin.onDoorMap = true;
  rejoin.partyBelongsInside = true;
  rejoin.yardsFromDoor = 5.44f;
  rejoin.insideDoor = true;
  check("rejoin recognizes the same physical doorway",
        OverseerDecisions::DecideDoorRejoin(rejoin, 5) ==
            OverseerDecisions::DoorRejoinStep::Knock);
  rejoin.insideDoor = false;
  rejoin.yardsFromDoor = 0;
  check("rejoin refuses another floor at center XY",
        OverseerDecisions::DecideDoorRejoin(rejoin, 5) ==
            OverseerDecisions::DoorRejoinStep::Walk);
  return failures ? 1 : 0;
}
