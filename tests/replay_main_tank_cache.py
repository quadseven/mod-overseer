"""Execute the patched main-tank cache and its real threat consumer.

Pinned source fixtures preserve the old cache, lookup and threat call bodies.
The fixture selector supplies a party member; object removal between reads is
the lifetime boundary under test. No realm, database or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0018-main-tank-cache-keeps-a-guid.patch"
FIXTURES = HERE / "fixtures/main_tank_cache"


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
#include <stdexcept>
#include <string>
using uint32 = uint32_t;
constexpr int IN_MILLISECONDS = 1000;
uint32 clockNow = 1000;
uint32 getMSTime() { return clockNow; }
struct ObjectGuid {
    int id = 0;
    static const ObjectGuid Empty;
    explicit operator bool() const { return id != 0; }
    bool operator<(ObjectGuid other) const { return id < other.id; }
};
const ObjectGuid ObjectGuid::Empty{};
struct Unit;
struct ThreatReference {
    float amount = 0;
    bool IsAvailable() const { return true; }
    float GetThreat() const { return amount; }
};
struct ThreatManager {
    std::map<ObjectGuid, ThreatReference*> _myThreatListEntries;
    float GetThreat(Unit const*, bool = false) const;
};
struct Unit {
    ObjectGuid guid;
    bool poisoned = false, world = true, player = false;
    int map = 1;
    ThreatManager* mgr = nullptr;
    ObjectGuid GetGUID() const {
        if (poisoned) throw std::runtime_error("read a departed member's GUID storage");
        return guid;
    }
    bool IsInWorld() const { return world; }
    bool IsPlayer() const { return player; }
    ThreatManager& GetThreatMgr() { return *mgr; }
};
std::map<ObjectGuid, Unit*> registry;
namespace ObjectAccessor {
Unit* GetUnit(Unit const& owner, ObjectGuid guid) {
    auto found = registry.find(guid);
    if (found == registry.end()) return nullptr;
    Unit* unit = found->second;
    return unit->world && unit->map == owner.map ? unit : nullptr;
}
}
struct PlayerbotAI {
    Unit* bot;
    Unit* candidate;
    Unit* GetUnit(ObjectGuid);
};
struct PerfMonitorOperation { void finish() {} };
struct Context { int performanceStack = 0; };
struct Monitor {
    template<class... Args> PerfMonitorOperation* start(Args...) { return nullptr; }
} sPerfMonitor;
constexpr int PERF_MON_VALUE = 0;
class UnitCalculatedValue {
public:
    UnitCalculatedValue(PlayerbotAI* ai, std::string label, int interval)
        : botAI(ai), checkInterval(interval), name(label) {}
    virtual Unit* Calculate() = 0;
    virtual Unit* Get();
    void Reset() { lastCheckTime = 0; value = nullptr; }
    std::string getName() const { return name; }
protected:
    PlayerbotAI* botAI;
    uint32 checkInterval, lastCheckTime = 0;
    Unit* value = nullptr;
    Context* context = nullptr;
    std::string name;
};
class PartyMemberValue : public UnitCalculatedValue {
public:
    PartyMemberValue(PlayerbotAI* ai, std::string label, int interval)
        : UnitCalculatedValue(ai, label, interval) {}
};
// MAIN_TANK_CLASS
int selections = 0;
Unit* PartyMemberMainTankValue::Calculate() { ++selections; return botAI->candidate; }
// REAL_BODIES
PartyMemberMainTankValue* mainTank = nullptr;
Unit* currentTarget = nullptr;
Unit* readValue(std::string name) {
    return name == "main tank" ? mainTank->Get() : currentTarget;
}
#define AI_VALUE(type, name) readValue(name)
struct LowTankThreatTrigger { Unit* bot; bool IsActive(); };
// REAL_TRIGGER
void require(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char** argv) {
    try {
        require(argc == 2, "missing scenario");
        Unit bot{{1}}, tank{{2}}, replacement{{3}}, enemy{{4}};
        ThreatReference ownThreat{20}, tankThreat{10};
        ThreatManager threats;
        threats._myThreatListEntries = {{{1}, &ownThreat}, {{2}, &tankThreat}, {{3}, &tankThreat}};
        enemy.mgr = &threats;
        registry = {{{1}, &bot}, {{2}, &tank}, {{3}, &replacement}, {{4}, &enemy}};
        PlayerbotAI ai{&bot, &tank};
        PartyMemberMainTankValue cached(&ai);
        mainTank = &cached;
        currentTarget = &enemy;
        LowTankThreatTrigger trigger{&bot};
        require(trigger.IsActive(), "normal threat comparison changed");
        require(selections == 1, "first selection missing");
        std::string scenario = argv[1];
        clockNow += 500;
        if (scenario == "departed") {
            registry.erase(ObjectGuid{2});
            tank.poisoned = true;
            ai.candidate = nullptr;
            require(!trigger.IsActive(), "departed tank still used for threat");
        } else if (scenario == "reconnected") {
            replacement.guid = ObjectGuid{2};
            registry[ObjectGuid{2}] = &replacement;
            tank.poisoned = true;
            require(cached.Get() == &replacement, "cached pointer survived reconnection");
            require(trigger.IsActive(), "reconnected tank not used");
        } else if (scenario == "other-map") {
            tank.map = 2;
            require(!trigger.IsActive(), "foreign-map tank used for threat");
        } else if (scenario == "expired") {
            clockNow += 2000;
            ai.candidate = &replacement;
            require(cached.Get() == &replacement && selections == 2, "selection cadence changed");
        } else if (scenario == "reset") {
            ai.candidate = &replacement;
            cached.Reset();
            require(cached.Get() == &replacement && selections == 2, "reset kept old tank");
        } else {
            require(scenario == "stable", "unknown scenario");
            require(trigger.IsActive() && selections == 1, "stable selection was recalculated");
        }
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
"""


class MainTankCachePatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="main-tank-cache-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        target = root / "src/Ai/Base/Value"
        target.mkdir(parents=True)
        for name in ("PartyMemberValue.cpp", "PartyMemberValue.h"):
            shutil.copyfile(FIXTURES / name, target / name)
        if PATCH.exists():
            git = shutil.which("git")
            if git is None:
                raise RuntimeError("git required to apply the repository patch")
            subprocess.run(  # noqa: S603 - repository patch applied to temporary fixtures
                [git, "apply", str(PATCH)],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
                timeout=15,
            )
        header = (target / "PartyMemberValue.h").read_text()
        start = header.index("class PartyMemberMainTankValue")
        declaration = header[start : header.index("};", start) + 2]
        source = (target / "PartyMemberValue.cpp").read_text()
        consumers = (FIXTURES / "PinnedConsumers.cpp").read_text()
        bodies = "\n\n".join(
            function(consumers, signature)
            for signature in (
                "Unit* UnitCalculatedValue::Get()",
                "Unit* PlayerbotAI::GetUnit(ObjectGuid guid)",
                "float ThreatManager::GetThreat(Unit const* who, bool includeOffline) const",
            )
        )
        signature = "Unit* PartyMemberMainTankValue::Get()"
        if signature in source:
            bodies += "\n\n" + function(source, signature)
        harness = HARNESS.replace("// MAIN_TANK_CLASS", declaration)
        harness = harness.replace("// REAL_BODIES", bodies)
        harness = harness.replace(
            "// REAL_TRIGGER",
            function(consumers, "bool LowTankThreatTrigger::IsActive()"),
        )
        (root / "replay.cpp").write_text(harness)
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if compiler is None:
            raise RuntimeError("C++ compiler required for the main-tank cache replay")
        cls.binary = root / "replay"
        built = subprocess.run(  # noqa: S603 - compile the fixed repository replay
            [
                compiler,
                "-std=c++17",
                "-O0",
                str(root / "replay.cpp"),
                "-o",
                str(cls.binary),
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if built.returncode:
            raise RuntimeError(built.stderr)

    def test_main_tank_identity_is_resolved_at_each_use(self):
        for scenario in (
            "departed",
            "reconnected",
            "other-map",
            "expired",
            "reset",
            "stable",
        ):
            with self.subTest(scenario=scenario):
                result = subprocess.run(  # noqa: S603 - execute the replay with fixed scenarios
                    [str(self.binary), scenario],
                    capture_output=True,
                    text=True,
                    timeout=5,
                )
                self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main()
