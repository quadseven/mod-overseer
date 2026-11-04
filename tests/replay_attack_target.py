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
PATCH = HERE.parent / "patches/mod-playerbots/0020-attack-target-resolved-before-use.patch"
FIXTURE = HERE / "fixtures/attack_target/AttackAction.cpp"
TARGET = Path("src/Ai/Base/Actions/AttackAction.cpp")


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

    ObjectGuid GetGUID() const { return guid; }  // a direct member read, as in the core
    bool IsInWorld() const { return inWorld; }
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


def build(root, source_text):
    body = function(source_text, "bool AttackAction::Attack(Unit* target")
    (root / "replay.cpp").write_text(HARNESS.replace("// REAL_BODY", body))
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

        pinned_root = root / "pinned"
        pinned_file = pinned_root / TARGET
        pinned_file.parent.mkdir(parents=True)
        shutil.copyfile(FIXTURE, pinned_file)
        cls.pinned = build(pinned_root, pinned_file.read_text())

        # The replay proves the PATCHED body. Without the patch it would
        # compile the pinned fixture as the realm ships it and could pass
        # for the wrong reason, so a missing patch is a failure, not a
        # skip. When upstream takes the fix and the patch is deleted,
        # retire this replay or re-pin its fixture in the same change.
        if not PATCH.exists():
            raise RuntimeError(
                "%s is missing: this replay applies it to the fixture and "
                "proves nothing without it" % PATCH.relative_to(HERE.parent)
            )
        git = shutil.which("git")
        if git is None:
            raise RuntimeError("git required to apply the repository patch")
        patched_root = root / "patched"
        patched_file = patched_root / TARGET
        patched_file.parent.mkdir(parents=True)
        shutil.copyfile(FIXTURE, patched_file)
        applied = subprocess.run(  # noqa: S603 - repository patch applied to temporary fixtures
            [git, "apply", str(PATCH)],
            cwd=patched_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
        if applied.returncode != 0:
            raise RuntimeError("patch does not apply to the pinned fixture:\n" + applied.stderr)
        cls.patched = build(patched_root, patched_file.read_text())

    def test_pinned_body_faults_on_departed_target(self):
        code, out = run(self.pinned, "departed")
        self.assertEqual(code, 3, out)
        self.assertIn("CRASH", out)

    def test_patched_body_refuses_departed_target(self):
        code, out = run(self.patched, "departed")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_pinned_body_attacks_other_map_target(self):
        code, out = run(self.pinned, "other-map")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=1 attacked=1", out)

    def test_patched_body_refuses_other_map_target(self):
        code, out = run(self.patched, "other-map")
        self.assertEqual(code, 0, out)
        self.assertIn("returned=0 attacked=0", out)

    def test_live_target_attacked_with_and_without_patch(self):
        for binary in (self.pinned, self.patched):
            code, out = run(binary, "live")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=1 attacked=1", out)

    def test_friendly_target_refused_with_and_without_patch(self):
        for binary in (self.pinned, self.patched):
            code, out = run(binary, "friendly")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=0 attacked=0", out)

    def test_null_target_refused_with_and_without_patch(self):
        for binary in (self.pinned, self.patched):
            code, out = run(binary, "null")
            self.assertEqual(code, 0, out)
            self.assertIn("returned=0 attacked=0", out)


if __name__ == "__main__":
    unittest.main()
