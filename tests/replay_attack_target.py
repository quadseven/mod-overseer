"""Execute the real AttackAction::Attack body, pinned and patched.

The fixture preserves src/Ai/Base/Actions/AttackAction.cpp exactly as the
realm pin ships it. The harness compiles the Attack() body extracted from
that fixture twice - once as pinned, once with this repository's patch
applied - against a lifetime model of the world the body runs in:

- a unit is a GUID, a map id, an in-world flag and an update-field array.
  The reaction read (IsFriendlyTo -> GetReactionTo -> the charmer/owner
  field, the faulting chain of the crash this is written for) reads through
  that array, and the model throws when the array is already gone, which
  is the harness stand-in for the SIGSEGV at address 0: the real Unit
  cannot exist inside a decisions test, so the crash is modelled as the
  exact read that faulted, not approximated away.
- ObjectAccessor resolution (PlayerbotAI::GetUnit) answers only for a
  unit registered in the bot's own map store, which is the real
  accessor's contract.

A departed target is modelled the way the crash presents it: no longer
registered in its map, its update-field array already torn down, while
the raw pointer the AI value cached still reads an in-world flag that
says nothing about either - which is why the pinned body's null and
IsInWorld() checks both pass and the reaction read still faults.

No realm, database or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
PREVIOUS_PATCH = HERE.parent / "patches/mod-playerbots/0020-attack-target-resolved-before-use.patch"
PATCH = HERE.parent / "patches/mod-playerbots/0021-attack-uses-stored-target-guid.patch"
CORE_PATCH = HERE.parent / "patches/azerothcore-wotlk/0002-duel-info-retains-opponent-guid.patch"
FIXTURE = HERE / "fixtures/attack_target/AttackAction.cpp"
FIXTURES = {
    Path("src/Ai/Base/Actions/AttackAction.cpp"): FIXTURE,
    Path("src/Ai/Base/Actions/AttackAction.h"): HERE / "fixtures/attack_target/AttackAction.h",
    Path("src/Ai/Base/Value/CurrentTargetValue.h"): HERE / "fixtures/attack_target/CurrentTargetValue.h",
    Path("src/Ai/Base/Value/DuelTargetValue.h"): HERE / "fixtures/attack_target/DuelTargetValue.h",
    Path("src/Ai/Base/Value/DuelTargetValue.cpp"): HERE / "fixtures/attack_target/DuelTargetValue.cpp",
    Path("src/Ai/Base/Value/RtiTargetValue.h"): HERE / "fixtures/attack_target/RtiTargetValue.h",
    Path("src/Ai/Base/Value/RtiTargetValue.cpp"): HERE / "fixtures/attack_target/RtiTargetValue.cpp",
    Path("src/Ai/Base/Actions/ChooseTargetActions.cpp"): HERE / "fixtures/attack_target/ChooseTargetActions.cpp",
    Path("src/Bot/Engine/Value/Value.cpp"): HERE / "fixtures/attack_target/Value.cpp",
    Path("src/Bot/Engine/Value/Value.h"): HERE / "fixtures/attack_target/Value.h",
}


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r"""
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <utility>

using uint32 = uint32_t;

// Thrown when the reaction read reaches a unit whose update-field array
// is already gone: the harness stand-in for the fault at address 0.
struct SegvModel
{
    char const* what;
};

struct ObjectGuid
{
    uint64_t raw = 0;
    ObjectGuid() = default;
    explicit ObjectGuid(uint64_t value) : raw(value) {}
    bool operator!() const { return raw == 0; }
    bool operator<(ObjectGuid other) const { return raw < other.raw; }
    bool operator==(ObjectGuid other) const { return raw == other.raw; }
    bool operator!=(ObjectGuid other) const { return raw != other.raw; }
};

struct Unit
{
    ObjectGuid guid;
    uint32 fieldStore[1] = {0};
    uint32* fields = fieldStore;  // the update-field array; null once torn down
    bool inWorld = true;
    bool player = false, pet = false, dead = false;
    int mapId = 1;
    int reaction = 1;             // < 5 hostile, >= 5 friendly (stand-in scale)
    std::string name = "target";

    ObjectGuid GetGUID() const
    {
        if (!fields)
            throw SegvModel{"GUID read through a null update-field array"};
        return guid;
    }
    bool IsInWorld() const { return inWorld; }
    int GetMap() const { return mapId; }
    bool IsPlayer() const { return player; }
    bool IsPet() const { return pet; }
    bool isDead() const { return dead; }
    std::string const& GetName() const { return name; }
    uint32 GetZoneId() const { return 0; }
    uint32 GetAreaId() const { return 0; }
    ObjectGuid GetCharmerOrOwnerGuid() const
    {
        if (!fields)
            throw SegvModel{"reaction read through a null update-field array"};
        return ObjectGuid(fields[0]);
    }
    int GetReactionTo(Unit const*) const
    {
        (void)GetCharmerOrOwnerGuid();
        return reaction;
    }
    bool IsFriendlyTo(Unit const* unit) const { return unit->GetReactionTo(this) >= 5; }
};

enum MovementGeneratorType
{
    IDLE_MOTION_TYPE = 0,
    FLIGHT_MOTION_TYPE = 6,
    NULL_MOTION_TYPE = 99
};
constexpr int MOTION_SLOT_CONTROLLED = 1;
constexpr uint32 UNIT_STATE_IN_FLIGHT = 0x0001;
constexpr uint32 UNIT_STATE_MELEE_ATTACKING = 0x0002;
constexpr int BOT_STATE_COMBAT = 1;
constexpr float CAST_ANGLE_IN_FRONT = 1.0472f;

struct MotionMaster
{
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return IDLE_MOTION_TYPE; }
    MovementGeneratorType GetMotionSlotType(int) const { return NULL_MOTION_TYPE; }
    void Clear(bool) {}
};

struct DuelInfo
{
    Unit* Opponent = nullptr;
    ObjectGuid OpponentGuid;
};

struct Player : Unit
{
    DuelInfo* duel = nullptr;
    MotionMaster motionMaster;
    Unit* victim = nullptr;
    uint32 unitStates = 0;
    Unit* attacked = nullptr;

    MotionMaster* GetMotionMaster() { return &motionMaster; }
    bool HasUnitState(uint32 state) const { return (unitStates & state) != 0; }
    Unit* GetVictim() const { return victim; }
    bool Attack(Unit* target, bool) { attacked = target; return true; }
    void SetSelection(ObjectGuid) {}
    bool isMoving() const { return false; }
    void StopMoving() {}
    bool IsWithinMeleeRange(Unit const*) const { return true; }
    bool IsWithinLOSInMap(Unit const*) const { return true; }
    bool IsValidAttackTarget(Unit const*) const { return true; }
    bool HasInArc(float, Unit const*) const { return true; }
};

// The map store ObjectAccessor answers from: one registry per map, and a
// unit is findable only on the map it is registered on.
std::map<std::pair<int, uint64_t>, Unit*> mapStore;

struct PlayerbotAI
{
    Player* bot = nullptr;
    int state = 0;

    Unit* GetUnit(ObjectGuid guid)
    {
        if (!guid)
            return nullptr;
        auto found = mapStore.find({bot->mapId, guid.raw});
        return found == mapStore.end() ? nullptr : found->second;
    }
    void TellError(std::string const&) {}
    bool IsInVehicle() const { return false; }
    bool IsInVehicle(bool, bool, bool) const { return false; }
    int GetState() const { return state; }
    void ChangeEngine(int) {}
    bool CanMove() const { return true; }
    static bool IsMelee(Player const*) { return false; }
};

struct PlayerbotTextMgr
{
    static PlayerbotTextMgr& instance()
    {
        static PlayerbotTextMgr mgr;
        return mgr;
    }
    std::string GetBotTextOrDefault(std::string const&, std::string const& fallback,
                                    std::map<std::string, std::string> const&)
    {
        return fallback;
    }
};

struct PlayerbotAIConfigStub
{
    bool IsPvpProhibited(uint32, uint32) const { return false; }
};
PlayerbotAIConfigStub sPlayerbotAIConfig;

struct ServerFacade
{
    static ServerFacade& instance()
    {
        static ServerFacade facade;
        return facade;
    }
    void SetFacingTo(Unit*, Unit*) {}
};

struct WaitForAttackStrategy
{
    static bool ShouldWait(PlayerbotAI*) { return false; }
};

enum class MovementPriority
{
    MOVEMENT_IDLE,
    MOVEMENT_NORMAL,
    MOVEMENT_COMBAT
};
struct LastMovement
{
    MovementPriority priority = MovementPriority::MOVEMENT_COMBAT;
    void clear() { priority = MovementPriority::MOVEMENT_IDLE; }
};
LastMovement& HarnessLastMovement()
{
    static LastMovement movement;
    return movement;
}
#define AI_VALUE(type, name) (HarnessLastMovement())

struct LootObjectStack
{
    void Add(ObjectGuid) {}
};

template <typename T>
struct ValueHolder
{
    T value{};
    T Get() { return value; }
    void Set(T next) { value = next; }
};

struct AiObjectContext
{
    template <typename T>
    ValueHolder<T>* GetValue(std::string const&)
    {
        static ValueHolder<T> holder;
        return &holder;
    }
};

struct AttackAction
{
    Player* bot = nullptr;
    PlayerbotAI* botAI = nullptr;
    AiObjectContext* context = nullptr;
    bool verbose = false;

    bool Attack(Unit* target, bool with_pet = true);
};

// REAL_BODY

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "missing scenario\n";
        return 2;
    }
    std::string const scenario = argv[1];

    Player bot;
    bot.guid = ObjectGuid(1);
    bot.mapId = 1;
    PlayerbotAI ai;
    ai.bot = &bot;
    AiObjectContext context;
    LootObjectStack loot;
    context.GetValue<LootObjectStack*>("available loot")->Set(&loot);

    Unit target;
    target.guid = ObjectGuid(100);
    target.mapId = 1;
    target.reaction = 1;

    AttackAction action;
    action.bot = &bot;
    action.botAI = &ai;
    action.context = &context;

    Unit* passed = &target;
    if (scenario == "null")
    {
        passed = nullptr;
    }
    else if (scenario == "departed")
    {
        // Gone from the map and its update fields torn down, while the
        // cached pointer still reads as in the world: the crashed tick.
        target.fields = nullptr;
    }
    else if (scenario == "other-map")
    {
        target.mapId = 2;
        mapStore[{2, target.guid.raw}] = &target;
    }
    else if (scenario == "out-of-world")
    {
        target.inWorld = false;
        mapStore[{1, target.guid.raw}] = &target;
    }
    else if (scenario == "wrong-map-resolver")
    {
        target.mapId = 2;
        mapStore[{1, target.guid.raw}] = &target;
    }
    else if (scenario == "friendly")
    {
        target.reaction = 9;
        mapStore[{1, target.guid.raw}] = &target;
    }
    else if (scenario == "live")
    {
        mapStore[{1, target.guid.raw}] = &target;
    }
    else
    {
        std::cerr << "unknown scenario\n";
        return 2;
    }

    try
    {
        bool const returned = action.Attack(passed);
        std::cout << "returned=" << (returned ? 1 : 0)
                  << " attacked=" << (bot.attacked ? 1 : 0) << "\n";
        return 0;
    }
    catch (SegvModel const& fault)
    {
        std::cout << "CRASH " << fault.what << "\n";
        return 3;
    }
}
"""


GUID_HARNESS = HARNESS.replace(
    "bool Attack(Unit* target, bool with_pet = true);",
    "bool Attack(ObjectGuid targetGuid, bool with_pet = true);",
).replace("Unit* passed = &target;", "ObjectGuid passed = target.guid;").replace(
    "passed = nullptr;", "passed = ObjectGuid();"
)


UNIT_VALUE_HARNESS = r"""
#include <cstdint>
#include <iostream>
#include <string>

using uint32 = uint32_t;
using int32 = int32_t;

struct ObjectGuid
{
    uint64_t raw = 0;
    ObjectGuid() = default;
    explicit ObjectGuid(uint64_t value) : raw(value) {}
    explicit operator bool() const { return raw != 0; }
    static ObjectGuid const Empty;
};
ObjectGuid const ObjectGuid::Empty{};

struct Unit
{
    ObjectGuid guid;
    bool inWorld = true;
    bool fields = true;

    ObjectGuid GetGUID() const
    {
        if (!fields)
            throw 1;
        return guid;
    }
    bool IsInWorld() const
    {
        if (!fields)
            throw 2;
        return inWorld;
    }
};

struct PlayerbotAI
{
    Unit* live = nullptr;
    Unit* GetUnit(ObjectGuid guid) { return live && live->guid.raw == guid.raw ? live : nullptr; }
};

struct PerfMonitorOperation { void finish() {} };
struct PerfMonitorStub
{
    PerfMonitorOperation* start(int, std::string const&, void*) { return nullptr; }
};
PerfMonitorStub sPerfMonitor;
constexpr int PERF_MON_VALUE = 0;
uint32 getMSTime() { return 2; }

struct Context { int performanceStack = 0; };
struct UnitCalculatedValue
{
    PlayerbotAI* botAI = nullptr;
    Context* context = nullptr;
    int32 checkInterval = 100;
    uint32 lastCheckTime = 1;
    Unit* value = nullptr;
    ObjectGuid guid;
    Unit* calculated = nullptr;

    std::string const& getName() const
    {
        static std::string const name = "target";
        return name;
    }
    Unit* Calculate() { return calculated; }
    Unit* Get();
};

// REAL_BODY

int main()
{
    Unit stale;
    stale.guid = ObjectGuid(42);
    stale.fields = false;
    Unit live;
    live.guid = ObjectGuid(42);
    PlayerbotAI ai;
    ai.live = &live;
    UnitCalculatedValue value;
    value.botAI = &ai;
    value.value = &stale;
    value.guid = ObjectGuid(42);
    try
    {
        Unit* result = value.Get();
        std::cout << "returned_live=" << (result == &live ? 1 : 0) << "\n";
        return 0;
    }
    catch (...)
    {
        std::cout << "stale_pointer_dereferenced\n";
        return 3;
    }
}
"""


def build(root, source_text, *, guid_argument):
    signature = (
        "bool AttackAction::Attack(ObjectGuid targetGuid"
        if guid_argument
        else "bool AttackAction::Attack(Unit* target"
    )
    harness = GUID_HARNESS if guid_argument else HARNESS
    body = function(source_text, signature)
    (root / "replay.cpp").write_text(harness.replace("// REAL_BODY", body))
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise RuntimeError("C++ compiler required for the attack target replay")
    binary = root / "replay"
    built = subprocess.run(  # noqa: S603 - fixed compiler invocation on harness text
        [compiler, "-std=c++17", "-o", str(binary), str(root / "replay.cpp")],
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if built.returncode != 0:
        raise RuntimeError("harness failed to compile:\n" + built.stderr)
    return binary


def build_unit_value(root, source_text):
    body = function(source_text, "Unit* UnitCalculatedValue::Get()")
    (root / "unit-value-replay.cpp").write_text(UNIT_VALUE_HARNESS.replace("// REAL_BODY", body))
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise RuntimeError("C++ compiler required for the unit value replay")
    binary = root / "unit-value-replay"
    built = subprocess.run(  # noqa: S603 - fixed compiler invocation on harness text
        [compiler, "-std=c++17", "-o", str(binary), str(root / "unit-value-replay.cpp")],
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if built.returncode != 0:
        raise RuntimeError("unit value harness failed to compile:\n" + built.stderr)
    return binary


def run(binary, scenario):
    done = subprocess.run(  # noqa: S603 - the replay binary built just above
        [str(binary), scenario],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    return done.returncode, done.stdout


class AttackTargetPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="attack-target-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)

        if not PREVIOUS_PATCH.exists() or not PATCH.exists():
            raise RuntimeError(
                "both attack target patches are required for this replay"
            )
        git = shutil.which("git")
        if git is None:
            raise RuntimeError("git required to apply the repository patch")

        def copy_fixtures(destination):
            for target, fixture in FIXTURES.items():
                target_file = destination / target
                target_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(fixture, target_file)

        def apply_patch(destination, patch):
            applied = subprocess.run(  # noqa: S603 - repository patch applied to temporary fixtures
                [git, "apply", str(patch)],
                cwd=destination,
                check=False,
                capture_output=True,
                text=True,
                timeout=15,
            )
            if applied.returncode != 0:
                raise RuntimeError("patch does not apply to the pinned fixture:\n" + applied.stderr)

        pinned_root = root / "pinned"
        copy_fixtures(pinned_root)
        cls.pinned = build(
            pinned_root,
            (pinned_root / "src/Ai/Base/Actions/AttackAction.cpp").read_text(),
            guid_argument=False,
        )
        cls.pinned_unit_value = build_unit_value(
            pinned_root,
            (pinned_root / "src/Bot/Engine/Value/Value.cpp").read_text(),
        )

        previous_root = root / "previous"
        copy_fixtures(previous_root)
        apply_patch(previous_root, PREVIOUS_PATCH)
        cls.previous = build(
            previous_root,
            (previous_root / "src/Ai/Base/Actions/AttackAction.cpp").read_text(),
            guid_argument=False,
        )

        patched_root = root / "patched"
        copy_fixtures(patched_root)
        apply_patch(patched_root, PREVIOUS_PATCH)
        apply_patch(patched_root, PATCH)
        cls.patched_source = (patched_root / "src/Ai/Base/Actions/AttackAction.cpp").read_text()
        cls.current_target_source = (
            patched_root / "src/Ai/Base/Value/CurrentTargetValue.h"
        ).read_text()
        cls.attack_header_source = (
            patched_root / "src/Ai/Base/Actions/AttackAction.h"
        ).read_text()
        cls.duel_target_source = (
            patched_root / "src/Ai/Base/Value/DuelTargetValue.cpp"
        ).read_text()
        cls.rti_target_header_source = (
            patched_root / "src/Ai/Base/Value/RtiTargetValue.h"
        ).read_text()
        cls.rti_target_source = (
            patched_root / "src/Ai/Base/Value/RtiTargetValue.cpp"
        ).read_text()
        cls.choose_target_source = (
            patched_root / "src/Ai/Base/Actions/ChooseTargetActions.cpp"
        ).read_text()
        cls.unit_value_source = (
            patched_root / "src/Bot/Engine/Value/Value.cpp"
        ).read_text()
        cls.unit_value_header = (
            patched_root / "src/Bot/Engine/Value/Value.h"
        ).read_text()
        cls.patched = build(
            patched_root,
            cls.patched_source,
            guid_argument=True,
        )
        cls.patched_unit_value = build_unit_value(
            patched_root,
            cls.unit_value_source,
        )

    def test_pinned_body_faults_on_departed_target(self):
        code, out = run(self.pinned, "departed")
        self.assertEqual(code, 3, out)
        self.assertIn("CRASH", out)

    def test_previous_patch_faults_while_reading_stale_target_guid(self):
        code, out = run(self.previous, "departed")
        self.assertEqual(code, 3, out)
        self.assertIn("GUID read through a null update-field array", out)

    def test_patched_body_refuses_departed_target(self):
        code, out = run(self.patched, "departed")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_pinned_body_attacks_other_map_target(self):
        code, out = run(self.pinned, "other-map")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=1 attacked=1", out)

    def test_previous_patch_refuses_other_map_target(self):
        code, out = run(self.previous, "other-map")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_patched_body_refuses_other_map_target(self):
        code, out = run(self.patched, "other-map")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_patched_body_refuses_out_of_world_resolved_target(self):
        code, out = run(self.patched, "out-of-world")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_patched_body_refuses_wrong_map_resolver_result(self):
        code, out = run(self.patched, "wrong-map-resolver")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_live_target_attacked_with_and_without_patch(self):
        for binary in (self.previous, self.patched):
            code, out = run(binary, "live")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=1 attacked=1", out)

    def test_friendly_target_refused_with_and_without_patch(self):
        for binary in (self.previous, self.patched):
            code, out = run(binary, "friendly")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=0 attacked=0", out)

    def test_null_target_refused_with_and_without_patch(self):
        for binary in (self.previous, self.patched):
            code, out = run(binary, "null")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=0 attacked=0", out)

    def test_core_patch_caches_the_duel_opponent_guid(self):
        patch = CORE_PATCH.read_text()
        self.assertIn("ObjectGuid const OpponentGuid;", patch)
        header, implementation = patch.split(
            "diff --git a/src/server/game/Entities/Player/Player.cpp ", 1
        )
        self.assertNotIn("opponent->GetGUID()", header)
        self.assertIn("DuelInfo::DuelInfo(Player* opponent", implementation)
        self.assertIn("OpponentGuid(opponent ? opponent->GetGUID() : ObjectGuid::Empty)", implementation)

    def test_every_attack_value_enters_through_a_stored_guid(self):
        self.assertIn("return value ? value->GetGuid() : ObjectGuid::Empty;", self.patched_source)
        self.assertIn("bool result = Attack(guid);", self.patched_source)
        self.assertIn("return guid ? Attack(guid) : false;", self.patched_source)
        self.assertNotIn("Attack(AI_VALUE(Unit*", self.patched_source)
        self.assertNotIn("Attack(botAI->GetUnit(guid))", self.patched_source)
        self.assertNotIn("botAI->GetUnit(target->GetGUID())", self.patched_source)
        self.assertNotIn("ObjectGuid guid = target->GetGUID();", self.patched_source)
        self.assertIn(
            "if (!target || !target->IsInWorld() || target->GetMap() != bot->GetMap())",
            self.patched_source,
        )
        self.assertIn("ObjectGuid GetGuid() const { return selection; }", self.current_target_source)
        self.assertIn("botAI->GetUnit(guid)", self.duel_target_source)
        self.assertIn('GetTargetName() override { return "duel target"; }', self.attack_header_source)

    def test_rti_attack_passes_its_stored_guid_not_a_resolved_pointer(self):
        self.assertIn("ObjectGuid GetGuid();", self.rti_target_header_source)
        self.assertIn("ObjectGuid RtiTargetValue::GetGuid()", self.rti_target_source)
        self.assertIn("botAI->GetUnit(guid)", self.rti_target_source)
        self.assertIn("bool result = Attack(guid);", self.choose_target_source)
        self.assertIn('Set({guid})', self.choose_target_source)
        self.assertNotIn("rtiTarget->GetGUID()", self.choose_target_source)
        self.assertNotIn("Attack(botAI->GetUnit(", self.choose_target_source)

    def test_legacy_unit_attack_callers_remain_compatible(self):
        self.assertIn("bool Attack(Unit* target, bool with_pet = true);", self.attack_header_source)

    def test_cached_unit_value_resolves_stale_pointer_by_saved_guid(self):
        code, out = run(self.pinned_unit_value, "")
        self.assertEqual(code, 3, out)
        self.assertIn("stale_pointer_dereferenced", out)

        code, out = run(self.patched_unit_value, "")
        self.assertEqual(code, 0, out)
        self.assertIn("returned_live=1", out)


if __name__ == "__main__":
    unittest.main()
