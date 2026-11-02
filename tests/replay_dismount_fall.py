"""Execute the patched CompleteDismount and the landing clear it depends on.

tests/fixtures/dismount_fall holds verbatim excerpts of the pinned source. The
real patch is applied to them with git apply. What runs is the source as it
stands: the SMSG_DISMOUNT case of HandleBotOutgoingPacket, the head of
PlayerbotAI::UpdateAI down to the fall baseline, CompleteDismount and
FinishDismountDescent. Only the core under them is a port, of
MotionMaster::MoveFall, Map::GetHeight and Unit::IsFalling at the core pin.
The same scenarios run on the unpatched excerpts, so a scenario that cannot
fail on the old code is a failure of this test. No realm or network is used.

Both deployments are scenarios here. A bot with no client gets its landing
only from UpdateAI, because EffectMovementGenerator::Finalize returns early for
a player. A bot sharing its Player with a client also gets its movement
packets, each of which overwrites the flags (MovementHandler.cpp:435, ported as
Case::ClientPacket), and a later fall of that client's own must not be touched.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0019-dismount-fall-keeps-its-flags-and-its-bound.patch"
FIXTURES = HERE / "fixtures/dismount_fall"
MOUNT = "src/Ai/Base/Actions/CheckMountStateAction.cpp"
AI = "src/Bot/PlayerbotAI.cpp"


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
using uint32 = uint32_t;
constexpr float INVALID_HEIGHT = -100000.0f;
constexpr float MAX_FALL_DISTANCE = 250000.0f;
constexpr float DEFAULT_HEIGHT_SEARCH = 50.0f;
constexpr uint32 MOVEMENTFLAG_FALLING = 0x1000, MOVEMENTFLAG_FALLING_FAR = 0x4000;
constexpr uint32 MOVEMENTFLAG_FLYING = 0x2000000, MOVEMENTFLAG_DISABLE_GRAVITY = 0x200;
constexpr uint32 SMSG_DISMOUNT = 0x3AC;
#define LOG_DEBUG(...) do { } while (0)

struct Spline {
    bool done = true, falling = false;
    bool Finalized() const { return done; }
    bool isFalling() const { return falling; }
};
struct World { float terrainZ = INVALID_HEIGHT, modelZ = INVALID_HEIGHT; };
struct ObjectGuid {
    int value = 0;
    bool operator!=(ObjectGuid const& other) const { return value != other.value; }
    ObjectGuid* ReadAsPacked() { return this; }
};
struct WorldPacket {
    ObjectGuid guid;
    uint32 GetOpcode() const { return SMSG_DISMOUNT; }
    void rpos(int) {}
    void operator>>(ObjectGuid* into) const { *into = guid; }
};
struct Session { bool IsLoggingOut() const { return false; } };
// Unit::IsFalling at the core pin: the flags, or a fall spline still running.
struct Unit {
    uint32 flags = 0;
    Spline spline, *movespline = &spline;
    bool IsFalling() const {
        return (flags & (MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)) ||
            (!movespline->Finalized() && movespline->isFalling());
    }
};
struct Player;
struct Motion { Player* owner; void MoveFall(); };
struct Name { char const* c_str() const { return "bot"; } };
struct Player : Unit {
    float z = 100.0f, lastFallZ = 0.0f;
    bool inWorld = true, disableMove = false, launchFails = false, rooted = false;
    bool teleporting = false;
    int flagUpdates = 0;
    World world;
    Session session;
    Motion motion{this};
    ObjectGuid GetGUID() const { return {1}; }
    Session* GetSession() { return &session; }
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsDuringRemoveFromWorld() const { return false; }
    bool IsRooted() const { return rooted; }
    float GetPositionX() const { return 0; }
    float GetPositionY() const { return 0; }
    float GetPositionZ() const { return z; }
    std::string GetName() const { return "bot"; }
    Motion* GetMotionMaster() { return &motion; }
    void SetFallInformation(long, float at) { lastFallZ = at; }
    bool HasUnitMovementFlag(uint32 f) const { return flags & f; }
    void AddUnitMovementFlag(uint32 f) { flags |= f; }
    void RemoveUnitMovementFlag(uint32 f) { flags &= ~f; }
    void SendMovementFlagUpdate() { ++flagUpdates; }
    // Map::GetHeight: the search bound reaches models only, and the raw
    // terrain below the feet answers however far down it is.
    float GetMapHeight(float, float, float at, bool, float bound) const {
        bool model = world.modelZ > INVALID_HEIGHT && at - world.modelZ <= bound;
        if (!model)
            return world.terrainZ;
        return std::max(world.modelZ, world.terrainZ);
    }
};
// MotionMaster::MoveFall at the core pin. The flag goes on before the spline
// launches, so a launch that fails leaves the flag with no spline under it.
void Motion::MoveFall() {
    if (owner->disableMove)
        return;
    float tz = owner->GetMapHeight(0, 0, owner->z, true, MAX_FALL_DISTANCE);
    if (tz <= INVALID_HEIGHT || std::fabs(owner->z - tz) < 0.1f)
        return;
    owner->AddUnitMovementFlag(MOVEMENTFLAG_FALLING);
    if (owner->launchFails)
        return;
    owner->spline.done = false;
    owner->spline.falling = true;
}
namespace GameTime { struct Now { long count() const { return 1; } }; inline Now GetGameTime() { return {}; } }

struct CheckMountStateAction { static RETURN CompleteDismount(Player* bot); };
struct PlayerbotAI {
    Player* bot;
    uint32 nextAICheckDelay = 0;
    bool dismountDescentRunning = false;
    void FinishDismountDescent();
    void UpdateAI(uint32 elapsed, bool minimal = false);
    void HandleBotOutgoingPacket(WorldPacket const& packet);
};
HELPERS

constexpr uint32 BOTH = MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR;
struct Case {
    Player bot;
    PlayerbotAI ai{&bot};
    void Dismount() { WorldPacket packet; packet.guid = bot.GetGUID(); ai.HandleBotOutgoingPacket(packet); }
    void Tick() { ai.UpdateAI(100); }
    // MovementHandler.cpp:435: a client's packet replaces the flags outright.
    void ClientPacket(uint32 flags) { bot.flags = flags; }
    void Land() { bot.spline.done = true; bot.z = bot.world.modelZ > INVALID_HEIGHT ? bot.world.modelZ : bot.world.terrainZ; }
    void Report(char const* name, bool ok) { std::cout << name << "=" << ok << "\n"; }
};

int main() {
    {   // A bot that was not falling: the flag stays for the descent, then goes.
        Case c; c.bot.world.terrainZ = 90;
        c.Dismount();
        bool during = c.bot.flags & MOVEMENTFLAG_FALLING;
        c.bot.z = 95; c.Tick();
        during = during && (c.bot.flags & MOVEMENTFLAG_FALLING) && c.bot.lastFallZ == 100;
        c.Land(); c.Tick();
        c.Report("committed_keeps_flag_then_landing_clears", during && !c.bot.flags && c.bot.flagUpdates == 1);
        // UpdateAI reads the flag right after the clear, so the landing tick
        // itself lets go of the baseline rather than the tick after it.
        c.Report("landing_tick_rebases_the_fall_baseline", c.bot.lastFallZ == 90);
    }
    {   // Standing on the ground with stale flags: MoveFall declines.
        Case c; c.bot.world.terrainZ = 100; c.bot.flags = BOTH;
        c.Dismount();
        c.Report("declined_clears_stale_flags", !c.bot.flags && c.bot.spline.Finalized());
    }
    {   // Falling before the dismount, MoveFall commits: a before and after
        // sample of the flag reads no change here, and the descent is real.
        Case c; c.bot.world.terrainZ = 90; c.bot.flags = BOTH;
        c.Dismount();
        bool during = (c.bot.flags & BOTH) == BOTH;
        c.Land(); c.Tick();
        c.Report("already_falling_committed_keeps_then_clears", during && !c.bot.flags);
    }
    {   // Falling before the dismount, MoveFall declines: a sample reads the
        // same no change, and this time the flags are stale.
        Case c; c.bot.world.terrainZ = 100; c.bot.flags = MOVEMENTFLAG_FALLING;
        c.Dismount();
        c.Report("already_falling_declined_clears", !c.bot.flags);
    }
    {   // An earlier descent still runs and this dismount cannot start one.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.bot.disableMove = true; c.Dismount();
        bool during = c.bot.flags & MOVEMENTFLAG_FALLING;
        c.Land(); c.Tick();
        c.Report("live_earlier_descent_keeps_then_clears", during && !c.bot.flags);
    }
    {   // MoveFall set the flag and the spline never launched.
        Case c; c.bot.world.terrainZ = 90; c.bot.launchFails = true;
        c.Dismount();
        c.Report("failed_launch_clears", !c.bot.flags);
    }
    {   // A descent stopped short still has to give the flag back.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.bot.spline.done = true; c.Tick();
        c.Report("interrupted_descent_clears", !c.bot.flags);
    }
    {   // Standing on a surface the model search cannot see, 80 yards up.
        Case c; c.bot.world.terrainZ = 20; c.bot.flags = MOVEMENTFLAG_FALLING;
        c.Dismount();
        c.Report("long_drop_refused", c.bot.spline.Finalized() && c.bot.z == 100 && !c.bot.flags);
    }
    {   // The same, with a model floor further down than any bound.
        Case c; c.bot.world.modelZ = 20;
        c.Dismount();
        c.Report("long_model_drop_refused", c.bot.spline.Finalized());
    }
    {   // Inside the bound the descent is the one MoveFall always made.
        Case c; c.bot.world.modelZ = 60; c.bot.world.terrainZ = 20;
        c.Dismount();
        c.Report("short_drop_settles", !c.bot.spline.Finalized());
    }
    {   // A flyer forced down over a long drop really is in the air, and a
        // bound here would leave it hovering.
        for (uint32 flag : {MOVEMENTFLAG_FLYING, MOVEMENTFLAG_DISABLE_GRAVITY}) {
            Case c; c.bot.world.terrainZ = 20; c.bot.flags = flag;
            c.Dismount();
            c.Report("flyer_falls_past_the_bound", !c.bot.spline.Finalized());
        }
    }
    {   // A bot in transfer is skipped by UpdateAI, and lands on its next tick.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.Land(); c.bot.teleporting = true; c.Tick();
        bool held = c.bot.flags & MOVEMENTFLAG_FALLING;
        c.bot.teleporting = false; c.Tick();
        c.Report("teleporting_bot_lands_on_its_next_tick", held && !c.bot.flags);
    }
    {   // No client: nothing but the tick can land it, and repeats are silent.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.Land(); c.Tick(); c.Tick();
        c.Report("no_client_landing_clears_once", !c.bot.flags && c.bot.flagUpdates == 1);
    }
    {   // Client attached: told it is falling for the descent, and its landing
        // packet ends it, so the tick has nothing left to send.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.bot.z = 95; c.Tick();
        bool during = c.bot.flags & MOVEMENTFLAG_FALLING;
        c.Land(); c.ClientPacket(0); c.Tick();
        c.Report("client_keeps_flag_then_lands", during && !c.bot.flags && c.bot.flagUpdates == 0);
    }
    {   // Client attached: it reports landing while the spline still runs.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.ClientPacket(0); c.Tick();
        c.Land(); c.Tick();
        c.Report("client_lands_before_the_spline_ends", !c.bot.flags && c.bot.flagUpdates == 0);
    }
    {   // Client attached: the spline ends and the tick clears before the
        // landing packet arrives. That packet then changes nothing.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.Land(); c.Tick(); c.ClientPacket(0); c.Tick();
        c.Report("client_landing_after_the_tick_clears_once", !c.bot.flags && c.bot.flagUpdates == 1);
    }
    {   // Client attached: its landing packet came first, then a fall of its
        // own while the spline is finished. That fall is not ours to clear.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.Land(); c.ClientPacket(0); c.Tick();
        c.ClientPacket(MOVEMENTFLAG_FALLING); c.Tick();
        c.Report("client_own_fall_is_left_alone", c.bot.flags == MOVEMENTFLAG_FALLING);
    }
    {   // Client attached: its own fall with no dismount behind it.
        Case c; c.ClientPacket(MOVEMENTFLAG_FALLING); c.Tick();
        c.Report("client_fall_without_dismount_is_left_alone", c.bot.flags == MOVEMENTFLAG_FALLING);
    }
    {   // A rooted bot sends no flag update, as ClearStaleFlightFlags does not.
        Case c; c.bot.world.terrainZ = 90; c.Dismount();
        c.Land(); c.bot.rooted = true; c.Tick();
        c.Report("rooted_landing_sends_no_update", !c.bot.flags && c.bot.flagUpdates == 0);
    }
    return 0;
}
"""

SCENARIOS = {
    "committed_keeps_flag_then_landing_clears",
    "landing_tick_rebases_the_fall_baseline",
    "declined_clears_stale_flags",
    "already_falling_committed_keeps_then_clears",
    "already_falling_declined_clears",
    "live_earlier_descent_keeps_then_clears",
    "failed_launch_clears",
    "interrupted_descent_clears",
    "long_drop_refused",
    "long_model_drop_refused",
    "short_drop_settles",
    "flyer_falls_past_the_bound",
    "teleporting_bot_lands_on_its_next_tick",
    "no_client_landing_clears_once",
    "client_keeps_flag_then_lands",
    "client_lands_before_the_spline_ends",
    "client_landing_after_the_tick_clears_once",
    "client_own_fall_is_left_alone",
    "client_fall_without_dismount_is_left_alone",
    "rooted_landing_sends_no_update",
}

# What the unpatched code gets right, so the rest are proved to fail on it. The
# two client and rooted guards at the end pin the landing clear, which the old
# code does not have, against a clear that reaches too far.
OLD_PASSES = {
    "landing_tick_rebases_the_fall_baseline",
    "client_lands_before_the_spline_ends",
    "declined_clears_stale_flags",
    "already_falling_declined_clears",
    "failed_launch_clears",
    "interrupted_descent_clears",
    "short_drop_settles",
    "flyer_falls_past_the_bound",
    "client_fall_without_dismount_is_left_alone",
    "client_own_fall_is_left_alone",
    "rooted_landing_sends_no_update",
}


def bot_paths(ai):
    """The real UpdateAI down to the fall baseline, and the real packet case."""
    update = ai[ai.index("void PlayerbotAI::UpdateAI"):ai.index("    // Handle cheat options")]
    case = ai[ai.index("case SMSG_DISMOUNT:"):ai.index("default:")]
    packet = "void PlayerbotAI::HandleBotOutgoingPacket(WorldPacket const& packet)\n{\n"
    packet += "switch (packet.GetOpcode()) {\n" + case + "}\n}\n"
    return update + "}\n\n" + packet


def run(tree, workdir, patched):
    mount = (tree / MOUNT).read_text()
    ai = (tree / AI).read_text()
    if patched:
        helpers = [
            function(mount, "static bool DismountMaySettle"),
            function(mount, "bool CheckMountStateAction::CompleteDismount"),
            function(ai, "void PlayerbotAI::FinishDismountDescent"),
        ]
    else:
        helpers = [function(mount, "void CheckMountStateAction::CompleteDismount")]
    source = HARNESS.replace("RETURN", "bool" if patched else "void")
    source = source.replace("HELPERS", "\n".join(helpers + [bot_paths(ai)]))
    cpp = workdir / ("new.cpp" if patched else "old.cpp")
    binary = cpp.with_suffix(".bin")
    cpp.write_text(source)
    subprocess.run(["g++", "-std=c++17", "-w", str(cpp), "-o", str(binary)], check=True)
    output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    results = {}
    for line in output.splitlines():
        name, ok = line.split("=")
        results[name] = results.get(name, True) and ok == "1"
    return results


class DismountFallPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workdir = Path(tempfile.mkdtemp())
        cls.addClassCleanup(shutil.rmtree, cls.workdir)
        cls.old = cls.workdir / "old"
        cls.new = cls.workdir / "new"
        shutil.copytree(FIXTURES, cls.old)
        shutil.copytree(FIXTURES, cls.new)
        subprocess.run(
            ["git", "apply", "--whitespace=error", str(PATCH)],
            cwd=cls.new, check=True, env={**os.environ, "GIT_CEILING_DIRECTORIES": str(cls.workdir)},
        )
        cls.old_results = run(cls.old, cls.workdir, False)
        cls.new_results = run(cls.new, cls.workdir, True)

    def test_every_scenario_ran(self):
        self.assertEqual(SCENARIOS, set(self.new_results))
        self.assertEqual(SCENARIOS, set(self.old_results))

    def test_the_patched_code_passes_every_scenario(self):
        self.assertEqual({}, {k: v for k, v in self.new_results.items() if not v})

    def test_the_unpatched_code_fails_the_scenarios_it_should(self):
        self.assertEqual(OLD_PASSES, {k for k, v in self.old_results.items() if v})


if __name__ == "__main__":
    unittest.main()
