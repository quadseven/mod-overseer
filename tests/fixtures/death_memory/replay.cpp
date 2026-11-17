// Harness for tests/replay_death_memory.py. The header excerpt is pasted in at
// the first marker below and the grind picker, verbatim from the pin with patch
// 0027 applied, at the second; everything else is a stand-in for the types they
// touch. The zone of a point is the one its x falls in: x < 0 is zone 1, 0 to
// 1000 is zone 2, 1000 and up is zone 3. Spots and marks sit on y = 0.
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include <set>
using uint32 = uint32_t;
using uint8 = uint8_t;
#define LOG_DEBUG(...) do { } while (0)
constexpr uint32 AREA_FLAG_CAPITAL = 0x8;
uint32 g_rng = 0;
uint32 urand(uint32 lo, uint32 hi) { return lo + (g_rng < hi - lo ? g_rng : hi - lo); }

struct WorldLocation {
    uint32 map = 4294967295u;
    float x = 0, y = 0, z = 0;
    WorldLocation() = default;
    WorldLocation(uint32 m, float x_, float y_, float z_) : map(m), x(x_), y(y_), z(z_) {}
    uint32 GetMapId() const { return map; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
};
struct WorldPosition : WorldLocation {
    WorldPosition() = default;
    WorldPosition(WorldLocation const& l) : WorldLocation(l) {}
    bool operator!=(WorldPosition const& o) const {
        return map != o.map || x != o.x || y != o.y || z != o.z;
    }
};
struct ObjectGuid { using LowType = uint32; };

// RUNTIME_HEADER
#endif

struct PlayerbotAI { NewRpgInfo rpgInfo; };
struct AreaTableEntry { uint32 flags = 0; };
struct AreaStore {
    AreaTableEntry plain{0};
    AreaTableEntry const* LookupEntry(uint32) const { return &plain; }
} sAreaTableStore;
static uint32 ZoneOf(float x) { return x < 0 ? 1 : (x < 1000 ? 2 : 3); }
struct Map {
    uint32 GetZoneId(uint32, float x, float, float) const { return ZoneOf(x); }
};
struct Player {
    uint32 level = 20, map = 1, zone = 2;
    float x = 0, y = 0, z = 5;
    Map m;
    PlayerbotAI* ai = nullptr;
    std::string name = "bot";
    uint32 GetLevel() const { return level; }
    uint32 GetMapId() const { return map; }
    uint32 GetZoneId() const { return zone; }
    uint32 GetPhaseMask() const { return 1; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    Map* GetMap() { return &m; }
    std::string const& GetName() const { return name; }
    float GetExactDist(WorldLocation const& l) const {
        return std::sqrt((x - l.x) * (x - l.x) + (y - l.y) * (y - l.y) + (z - l.z) * (z - l.z));
    }
};
#define GET_PLAYERBOT_AI(p) ((p)->ai)
struct TravelMgr {
    std::vector<WorldLocation> spots;
    std::vector<WorldLocation> const& GetLocsPerLevelCache(uint32) const { return spots; }
} sTravelMgr;
struct NewRpgBaseAction {
    WorldPosition SelectRandomGrindPos(Player* bot);
};

// RUNTIME_BODIES

static int failures = 0;
static void Report(char const* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    failures += !ok;
}
static bool Picked(WorldPosition const& p) { return p.map != 4294967295u; }

// Every x the picker can return over all the random draws.
static std::set<int> Reach(NewRpgBaseAction& ai, Player& bot) {
    std::set<int> xs;
    for (g_rng = 0; g_rng < 6; ++g_rng) {
        WorldPosition p = ai.SelectRandomGrindPos(&bot);
        if (Picked(p)) xs.insert(int(p.x));
    }
    g_rng = 0;
    return xs;
}
static void Die(PlayerbotAI& a, float x, long agoSeconds = 60) {
    a.rpgInfo.deaths.marks.push_back({1, x, 0, time(nullptr) - agoSeconds});
}

int main() {
    NewRpgBaseAction ai;
    auto world = [](std::vector<float> xs) {
        sTravelMgr.spots.clear();
        for (float x : xs) sTravelMgr.spots.push_back({1, x, 0, 5});
    };
    {   // Record() keeps the newest marks, drops the expired, and caps the list.
        NewRpgDeathMemory m;
        time_t now = time(nullptr);
        m.Record(1, 0, 0, now - 7 * 3600);
        m.Record(1, 5, 0, now);
        bool expiredGone = m.marks.size() == 1;
        for (int i = 0; i < 40; ++i) m.Record(1, float(i), 0, now);
        Report("record_drops_expired_and_caps", expiredGone && m.marks.size() == NewRpgDeathMemory::kCap);
    }
    {   // One death on a spot is not a pattern.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({100, 400});
        Die(brain, 100);
        Report("one_death_changes_nothing", Reach(ai, bot) == std::set<int>({100, 400}));
    }
    {   // Two deaths on a spot and it is not picked while another exists.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({100, 400});
        Die(brain, 100); Die(brain, 110);
        Report("two_deaths_retire_the_spot", Reach(ai, bot) == std::set<int>({400}));
    }
    {   // Deaths older than the window are forgotten.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({100, 400});
        Die(brain, 100, 7 * 3600); Die(brain, 110, 7 * 3600);
        Report("old_deaths_are_forgotten", Reach(ai, bot) == std::set<int>({100, 400}));
    }
    {   // A spot is skipped when the walk to it crosses hot ground.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 50; world({450});
        sTravelMgr.spots.push_back({1, 60, 600, 5});
        Die(brain, 250); Die(brain, 255);
        Report("hot_ground_on_the_way_is_avoided", Reach(ai, bot) == std::set<int>({60}));
    }
    {   // A bot standing on hot ground may leave it; spots on it stay retired.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({350, 450});
        Die(brain, 250); Die(brain, 255);
        Report("a_bot_on_hot_ground_can_leave", Reach(ai, bot) == std::set<int>({450}));
    }
    {   // The memory never leaves a bot with nothing to pick.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({100});
        Die(brain, 100); Die(brain, 110);
        Report("never_strands_a_bot", Reach(ai, bot) == std::set<int>({100}));
    }
    {   // Deaths on another map do not count here.
        PlayerbotAI brain; Player bot; bot.ai = &brain; bot.x = 250; world({100, 400});
        brain.rpgInfo.deaths.marks.push_back({0, 100, 0, time(nullptr) - 60});
        brain.rpgInfo.deaths.marks.push_back({0, 110, 0, time(nullptr) - 60});
        Report("other_map_deaths_are_ignored", Reach(ai, bot) == std::set<int>({100, 400}));
    }
    {   // A bot with no brain (not a playerbot) is picked for as before.
        Player bot; bot.x = 250; world({100, 400});
        Report("no_brain_no_memory", Reach(ai, bot) == std::set<int>({100, 400}));
    }
    return failures ? 1 : 0;
}
