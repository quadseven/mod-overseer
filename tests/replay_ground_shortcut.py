"""Run the patched MovementAction::MoveTo against paths the navmesh cannot carry.

tests/fixtures/ground_shortcut holds verbatim excerpts of the pinned source
(patches 0001 to 0028 applied): MoveTo with the function before it, and the
MovementAction class from the header. The real patch 0029 is applied to them
with git apply. What runs is MoveTo as it stands, against stand-ins for the
bot, PathGenerator and the motion master. The stand-in PathGenerator answers
the path type the core gives a player in each case (wow-overseer issue 2937):
NORMAL | SHORT for a route past the 74-point limit, NORMAL | NOT_USING_PATH for
an end with no polygon. Every MovePoint the move would launch is recorded.

The same scenarios run on the unpatched excerpt, and every scenario about a
straight line through the air must fail there: a scenario that cannot fail on
the old code is a failure of this test. No realm or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0029-a-ground-move-the-navmesh-cannot-carry-is-refused.patch"
FIXTURES = HERE / "fixtures/ground_shortcut"
CPP = "src/Ai/Base/Actions/MovementActions.cpp"
HEADER = "src/Ai/Base/Actions/MovementActions.h"


def function(source, signature):
    """A definition from its signature to the first closing brace in column 0.

    Brace counting is not used: MoveTo carries a long commented-out body
    whose braces do not balance.
    """
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    return source[start:end]


def patched_part(source):
    """The new helper and its constant, which sit just before MoveTo."""
    if "GroundMoveLeavesTheGround" not in source:
        return ""
    start = source.index("static constexpr float GROUND_SHORTCUT_STRIDE_YARDS")
    end = source.index("bool MovementAction::MoveTo(uint32 mapId")
    return source[start:end]


HARNESS = r"""
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using uint32 = uint32_t;
constexpr float INVALID_HEIGHT = -100000.0f;
constexpr uint32 IN_MILLISECONDS = 1000;
#define LOG_DEBUG(...) do { } while (0)

enum PathType : uint32 {
    PATHFIND_BLANK = 0x00, PATHFIND_NORMAL = 0x01, PATHFIND_SHORTCUT = 0x02, PATHFIND_INCOMPLETE = 0x04,
    PATHFIND_NOPATH = 0x08, PATHFIND_NOT_USING_PATH = 0x10, PATHFIND_SHORT = 0x20,
    PATHFIND_FARFROMPOLY_START = 0x40, PATHFIND_FARFROMPOLY_END = 0x80
};
enum UnitState : uint32 { UNIT_STATE_IGNORE_PATHFINDING = 0x1 };
enum UnitStandStateType { UNIT_STAND_STATE_STAND = 0 };
enum MovementPriority { MOVEMENT_NORMAL = 1 };

struct World {
    float bx = 0, by = 0, bz = 0;
    bool flying = false, swimming = false, transport = false, battleground = false, ignorePath = false;
    uint32 pathType = PATHFIND_NORMAL;
    int launched = 0;
    bool launchGeneratePath = false;
    int pathCalls = 0;
} W;

enum UnitMoveType { MOVE_RUN = 1, MOVE_RUN_BACK = 2 };
struct Transport {};
struct Unit {
    bool CanFly() const { return false; }
    float GetExactDist(float, float, float) const { return 0; }
    float GetSpeed(UnitMoveType) const { return 7; }
};
struct VehicleSeatEntry { bool CanControl() const { return true; } };
struct Vehicle {
    VehicleSeatEntry const* GetSeatForPassenger(void const*) const { return nullptr; }
    Unit* GetBase() const { return nullptr; }
};
namespace Movement { using PointsArray = std::vector<int>; }

struct Player : Unit {
    float GetExactDist(float x, float y, float z) const
    {
        return std::sqrt((x - W.bx) * (x - W.bx) + (y - W.by) * (y - W.by) + (z - W.bz) * (z - W.bz));
    }
    bool IsFlying() const { return W.flying; }
    bool isSwimming() const { return W.swimming; }
    Vehicle* GetVehicle() const { return nullptr; }
    Transport* GetTransport() const { return W.transport ? &t : nullptr; }
    bool InBattleground() const { return W.battleground; }
    bool HasUnitState(uint32) const { return W.ignorePath; }
    bool IsSitState() const { return false; }
    void SetStandState(int) {}
    float GetOrientation() const { return 0; }
    std::string GetName() const { return "Uzza"; }
    mutable Transport t;
};

struct PathGenerator {
    explicit PathGenerator(Player*) {}
    bool CalculatePath(float, float, float, bool) { ++W.pathCalls; return true; }
    uint32 GetPathType() const { return W.pathType; }
};

struct LastMovement {
    void Set(uint32, float, float, float, float, float, MovementPriority) {}
};
struct Config { int DisableMoveSplinePath = 0; float MaxWaitForMove = 5000; int MaxMovementSearchTime = 3; } sPlayerbotAIConfig;
struct BotAI { float GetReactDelay() const { return 100; } };
LastMovement lastMovementValue;
#define AI_VALUE(type, name) lastMovementValue

struct MovementAction {
    Player* bot = new Player();
    BotAI* botAI = new BotAI();
    void UpdateMovementState() {}
    bool IsMovingAllowed() { return true; }
    bool IsDuplicateMove(float, float, float) { return false; }
    bool IsWaitingForLastMove(MovementPriority) { return false; }
    float MoveDelay(float distance, bool) { return distance / 7.0f; }
    void DoMovePoint(Unit*, float, float, float z, bool generatePath, bool)
    {
        ++W.launched;
        W.launchGeneratePath = generatePath;
    }
    Movement::PointsArray SearchForBestPath(float, float, float z, float& modifiedZ, int, bool)
    {
        modifiedZ = z;
        return {};
    }
    bool RefusesGroundShortcut(float x, float y, float z, bool generatePath);
    bool MoveTo(uint32 mapId, float x, float y, float z, bool idle = false, bool react = false,
                bool normal_only = false, bool exact_waypoint = false,
                MovementPriority priority = MOVEMENT_NORMAL, bool lessDelay = false, bool backwards = false);
};

// PATCHED_PART

// MOVETO_BODY

// REFUSES_STUB

struct Case {
    const char* name;
    float dist;
    uint32 type;
    bool exact;
    bool flying, swimming, transport, battleground;
    bool expectLaunch;
};

int main()
{
    // The leader's line: a creature 3000 yards off, the route past the point limit.
    Case cases[] = {
        {"far_creature_route_past_the_point_limit_is_not_flown", 3000, PATHFIND_NORMAL | PATHFIND_SHORT, true, false, false, false, false, false},
        {"far_point_with_no_polygon_is_not_flown", 400, PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH, true, false, false, false, false, false},
        {"search_branch_does_not_fly_a_short_path", 600, PATHFIND_NORMAL | PATHFIND_SHORT, false, false, false, false, false, false},
        {"search_branch_does_not_fly_to_an_off_mesh_end", 120, PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY_END, false, false, false, false, false, false},
        {"a_nopath_fallback_line_is_not_flown", 90, PATHFIND_NOPATH, true, false, false, false, false, false},
        {"a_routed_move_is_still_walked", 250, PATHFIND_NORMAL, true, false, false, false, false, true},
        {"a_partial_route_is_still_walked", 200, PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY_END, true, false, false, false, false, true},
        {"a_stride_back_onto_the_mesh_is_still_taken", 15, PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH, true, false, false, false, false, true},
        {"a_flying_bot_still_flies_straight", 400, PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH, true, true, false, false, false, true},
        {"a_swimming_bot_still_swims_straight", 400, PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH, true, false, true, false, false, true},
        {"a_bot_on_a_transport_is_untouched", 400, PATHFIND_NORMAL | PATHFIND_SHORT, true, false, false, true, false, true},
        {"a_battleground_is_untouched", 400, PATHFIND_NORMAL | PATHFIND_SHORT, true, false, false, false, true, true},
    };
    for (Case const& c : cases)
    {
        W = World();
        W.pathType = c.type;
        W.flying = c.flying;
        W.swimming = c.swimming;
        W.transport = c.transport;
        W.battleground = c.battleground;
        MovementAction a;
        bool const moved = a.MoveTo(0, c.dist, 0, 0, false, false, false, c.exact);
        bool const launched = W.launched > 0;
        // A refused move answers false, so the caller's own fallback runs;
        // a launched move answers true.
        bool ok = launched == c.expectLaunch && moved == c.expectLaunch;
        std::cout << (ok ? "PASS " : "FAIL ") << c.name << "\n";
    }
    return 0;
}
"""

# The old code has no RefusesGroundShortcut; the harness declares it, so the
# unpatched run defines it as "never refuse", which is what the old code does.
OLD_STUB = "bool MovementAction::RefusesGroundShortcut(float, float, float, bool) { return false; }"

SCENARIOS = {
    "far_creature_route_past_the_point_limit_is_not_flown",
    "far_point_with_no_polygon_is_not_flown",
    "search_branch_does_not_fly_a_short_path",
    "search_branch_does_not_fly_to_an_off_mesh_end",
    "a_nopath_fallback_line_is_not_flown",
    "a_routed_move_is_still_walked",
    "a_partial_route_is_still_walked",
    "a_stride_back_onto_the_mesh_is_still_taken",
    "a_flying_bot_still_flies_straight",
    "a_swimming_bot_still_swims_straight",
    "a_bot_on_a_transport_is_untouched",
    "a_battleground_is_untouched",
}

# What the old MoveTo already gets right: it launches everything, so only the
# scenarios where launching is the right answer pass on it.
OLD_PASSES = {
    "a_routed_move_is_still_walked",
    "a_partial_route_is_still_walked",
    "a_stride_back_onto_the_mesh_is_still_taken",
    "a_flying_bot_still_flies_straight",
    "a_swimming_bot_still_swims_straight",
    "a_bot_on_a_transport_is_untouched",
    "a_battleground_is_untouched",
}


def run_scenarios(source, folder, label):
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise unittest.SkipTest("C++17 compiler required")
    body = function(source, "bool MovementAction::MoveTo(uint32 mapId")
    patched = patched_part(source)
    text = (
        HARNESS.replace("// PATCHED_PART", patched)
        .replace("// MOVETO_BODY", body)
        .replace("// REFUSES_STUB", "" if patched else OLD_STUB)
    )
    cpp = folder / f"replay_{label}.cpp"
    cpp.write_text(text)
    binary = folder / f"replay_{label}"
    built = subprocess.run(  # noqa: S603 - fixed repository sources
        [compiler, "-std=c++17", "-w", str(cpp), "-o", str(binary)],
        capture_output=True,
        text=True,
        timeout=90,
    )
    if built.returncode:
        raise AssertionError(built.stdout + built.stderr)
    ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)  # noqa: S603
    results = {}
    for line in ran.stdout.splitlines():
        verdict, name = line.split(" ", 1)
        results[name] = verdict == "PASS"
    return results


class GroundShortcutReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        tree = folder / "tree"
        for rel in (CPP, HEADER):
            (tree / rel).parent.mkdir(parents=True, exist_ok=True)
            (tree / rel).write_bytes((FIXTURES / rel).read_bytes())
        subprocess.run(["git", "init", "-q"], cwd=tree, check=True)  # noqa: S607
        subprocess.run(  # noqa: S603, S607
            ["git", "apply", "--whitespace=error", str(PATCH)], cwd=tree, check=True
        )
        cls.patched = (tree / CPP).read_text(encoding="utf-8")
        cls.header = (tree / HEADER).read_text(encoding="utf-8")
        cls.new = run_scenarios(cls.patched, folder, "new")
        cls.old = run_scenarios((FIXTURES / CPP).read_text(encoding="utf-8"), folder, "old")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_every_scenario_ran(self):
        self.assertEqual(SCENARIOS, set(self.new))
        self.assertEqual(SCENARIOS, set(self.old))

    def test_the_patched_code_passes_every_scenario(self):
        self.assertEqual([], sorted(n for n, ok in self.new.items() if not ok))

    def test_the_old_code_fails_the_rest(self):
        self.assertEqual(OLD_PASSES, {n for n, ok in self.old.items() if ok})

    def test_both_ground_branches_ask_before_they_launch(self):
        """The refusal sits ahead of the DoMovePoint in both non-vehicle branches."""
        body = function(self.patched, "bool MovementAction::MoveTo(uint32 mapId")
        self.assertEqual(2, body.count("RefusesGroundShortcut("))
        exact = body.index("RefusesGroundShortcut(x, y, z, generatePath)")
        self.assertLess(exact, body.index("DoMovePoint(bot, x, y, z, generatePath, backwards)"))
        search = body.index("RefusesGroundShortcut(x, y, modifiedZ, generatePath)")
        self.assertLess(search, body.index("DoMovePoint(bot, x, y, modifiedZ, generatePath, backwards)"))
        self.assertIn("bool RefusesGroundShortcut(float x, float y, float z, bool generatePath);", self.header)


if __name__ == "__main__":
    unittest.main()
