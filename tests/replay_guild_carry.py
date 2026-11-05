"""Exercise the production carry lease against controlled player/AI boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/mod_overseer.cpp").read_text()
lease = source[source.index("    struct GuildCarryLease"):source.index("    // Is `name` one of a guild run")]
harness = r'''
#include "overseer_decisions.h"
#include <cassert>
#include <map>
#include <set>
#include <string>
using uint32 = unsigned;
enum BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT };
enum ReactStates { REACT_PASSIVE, REACT_DEFENSIVE, REACT_AGGRESSIVE };
struct Pet {
    ReactStates react = REACT_DEFENSIVE;
};
struct PlayerbotAI {
    std::set<BotState> passive;
    bool refuse = false;
    bool HasStrategy(std::string const& name, BotState state) { assert(name == "passive"); return passive.count(state); }
    void ChangeStrategy(std::string const& change, BotState state) {
        if (change == "-passive") passive.erase(state);
        else { assert(change == "+passive"); if (!refuse) passive.insert(state); }
    }
};
struct Player {
    PlayerbotAI ai;
    Pet pet;
    bool online = true, hasPet = true, attacking = true, casting = true, canResummon = true;
    uint32 pendingPet = 0;
    bool IsInWorld() { return online; }
    uint32 GetMapId() { return 0; } // queued outside Deadmines
    Pet* GetPet() { return hasPet ? &pet : nullptr; }
    void AttackStop() { attacking = false; }
    void InterruptNonMeleeSpells(bool) { casting = false; }
    void UnsummonPetTemporaryIfAny() { hasPet = false; pendingPet = 42; }
    uint32 GetTemporaryUnsummonedPetNumber() { return pendingPet; }
    void ResummonPetTemporaryUnSummonedIfAny() { if (canResummon) { hasPet = true; pendingPet = 0; } }
};
Player passenger;
namespace ObjectAccessor { Player* FindPlayerByName(std::string const& name) { assert(name == "First"); return &passenger; } }
#define GET_PLAYERBOT_AI(p) (&(p)->ai)
struct GuildRun { uint32 id = 1, mapId = 36; std::string carried = "First"; };
struct Driver {
LEASE
};
int main() {
    Driver driver;
    GuildRun run;
    passenger.ai.passive.insert(BOT_STATE_COMBAT); // operator-owned
    assert(driver.ApplyGuildCarryTactics(run));
    assert(passenger.ai.passive.size() == 2);
    assert(!passenger.attacking && !passenger.casting && !passenger.hasPet);
    assert(passenger.pet.react == REACT_DEFENSIVE);
    assert(driver.ApplyGuildCarryTactics(run)); // repeated enforcement retains original strategy ownership
    auto& lease = driver._guildCarryLeases.at("First");
    assert(lease.added.size() == 1 && lease.added.count(BOT_STATE_NON_COMBAT));
    passenger.ai.passive.erase(BOT_STATE_COMBAT); // engine reset during dc ensure
    assert(driver.ApplyGuildCarryTactics(run));
    assert(!lease.added.count(BOT_STATE_COMBAT));
    lease.returning = true;
    passenger.online = false;
    driver.ReturnGuildCarryLeases();
    assert(driver._guildCarryLeases.size() == 1);
    passenger.online = true;
    passenger.canResummon = false; // dead or mounted: core retains the pet number
    driver.ReturnGuildCarryLeases();
    assert(driver._guildCarryLeases.size() == 1 && passenger.pendingPet == 42);
    // A new run can borrow the same member while a mounted pet is pending.
    // Ownership transfers, but it must not inherit operator-owned passive.
    GuildRun next; next.id = 2;
    assert(driver.ApplyGuildCarryTactics(next));
    assert(lease.runId == 2 && !lease.returning);
    assert(lease.added.count(BOT_STATE_NON_COMBAT) && !lease.added.count(BOT_STATE_COMBAT));
    lease.returning = true;
    passenger.canResummon = true;
    driver.ReturnGuildCarryLeases();
    assert(passenger.ai.passive.size() == 1 && passenger.ai.passive.count(BOT_STATE_COMBAT));
    assert(passenger.pet.react == REACT_DEFENSIVE && passenger.hasPet);
    assert(driver._guildCarryLeases.empty());
    passenger.ai.passive.clear(); passenger.ai.refuse = true;
    assert(!driver.ApplyGuildCarryTactics(run));
    GuildRun ordinary; ordinary.carried.clear();
    assert(driver.ApplyGuildCarryTactics(ordinary));
}
'''.replace("LEASE", lease)
with tempfile.TemporaryDirectory(prefix="guild-carry-") as directory:
    folder = Path(directory)
    cpp = folder / "replay.cpp"
    cpp.write_text(harness)
    binary = folder / "replay"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(ROOT / "src"), str(cpp), str(ROOT / "src/overseer_decisions.cpp"), "-o", str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=10)
print("guild carry runtime replay passed")
