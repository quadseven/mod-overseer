// Harness for tests/replay_zone_fallback.py. The two functions under test are
// pasted in verbatim at RUNTIME_BODIES; everything above is a stand-in for the
// types they touch at the pin. The zone of a point is the one its x falls in:
// x < 0 is the sea (zone 1), 0 to 1000 is zone 2, 1000 and up is zone 3.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
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
struct AreaTableEntry { uint32 flags = 0; };
struct AreaStore {
    AreaTableEntry capital{AREA_FLAG_CAPITAL}, plain{0};
    uint32 capitalZone = 99;
    AreaTableEntry const* LookupEntry(uint32 zone) const { return zone == capitalZone ? &capital : &plain; }
} sAreaTableStore;
static uint32 ZoneOf(float x) { return x < 0 ? 1 : (x < 1000 ? 2 : 3); }
struct Map {
    uint32 GetZoneId(uint32, float x, float, float) const { return ZoneOf(x); }
};
struct Player {
    uint32 level = 1, map = 530, zone = 1;
    float x = 0, y = 0, z = 0;
    Map m;
    std::string name = "bot";
    uint32 GetLevel() const { return level; }
    uint32 GetMapId() const { return map; }
    uint32 GetZoneId() const { return zone; }
    uint32 GetPhaseMask() const { return 1; }
    Map* GetMap() { return &m; }
    std::string const& GetName() const { return name; }
    float GetExactDist(WorldLocation const& l) const {
        return std::sqrt((x - l.x) * (x - l.x) + (y - l.y) * (y - l.y) + (z - l.z) * (z - l.z));
    }
};
struct TravelMgr {
    std::vector<WorldLocation> spots, hubs;
    std::vector<WorldLocation> const& GetLocsPerLevelCache(uint32) const { return spots; }
    std::vector<WorldLocation> const GetTravelHubs(Player*) const { return hubs; }
} sTravelMgr;
struct NewRpgBaseAction {
    WorldPosition SelectRandomGrindPos(Player* bot);
    WorldPosition SelectRandomCampPos(Player* bot);
};

// RUNTIME_BODIES

static int failures = 0;
static void Report(char const* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    failures += !ok;
}
static bool Picked(WorldPosition const& p) { return p.map != 4294967295u; }

int main() {
    NewRpgBaseAction ai;
    // Spots: two on land in zone 2 (x 100 and 400), one in zone 3 (x 1200).
    auto world = []() {
        sTravelMgr.spots = {{530, 100, 0, 5}, {530, 400, 0, 5}, {530, 1200, 0, 5}};
        sTravelMgr.hubs = {{530, 300, 0, 5}};
    };
    {   // The sea bot of the realm: its zone caches no spot at all.
        Player bot; bot.level = 1; bot.x = -100; bot.zone = 1; world();
        WorldPosition grind = ai.SelectRandomGrindPos(&bot);
        Report("sea_bot_gets_a_grind_spot", Picked(grind));
        Report("sea_bot_grind_spot_is_in_reach", Picked(grind) && bot.GetExactDist(grind) < 2500.0f / 3);
        Report("sea_bot_gets_a_camp", Picked(ai.SelectRandomCampPos(&bot)));
    }
    {   // A bot with a spot in its own zone never leaves it for another zone.
        Player bot; bot.level = 20; bot.x = 200; bot.zone = 2; world();
        bool allOwnZone = true;
        for (g_rng = 0; g_rng < 3; ++g_rng) {
            WorldPosition p = ai.SelectRandomGrindPos(&bot);
            allOwnZone = allOwnZone && Picked(p) && ZoneOf(p.x) == 2;
            WorldPosition c = ai.SelectRandomCampPos(&bot);
            allOwnZone = allOwnZone && Picked(c) && ZoneOf(c.x) == 2;
        }
        g_rng = 0;
        Report("own_zone_spot_still_wins", allOwnZone);
    }
    {   // No spot in the zone, one in the next zone: take the next zone's.
        Player bot; bot.level = 15; bot.x = 900; bot.zone = 2; world();
        sTravelMgr.spots = {{530, 1200, 0, 5}};
        WorldPosition grind = ai.SelectRandomGrindPos(&bot);
        Report("zone_without_spot_borrows_the_neighbour", Picked(grind) && ZoneOf(grind.x) == 3);
    }
    {   // The distance limit is not widened: a spot out of reach stays out.
        Player bot; bot.level = 1; bot.x = -100; bot.zone = 1; world();
        sTravelMgr.spots = {{530, 5000, 0, 5}};
        sTravelMgr.hubs = {{530, 5000, 0, 5}};
        Report("far_spot_stays_out_of_reach",
               !Picked(ai.SelectRandomGrindPos(&bot)) && !Picked(ai.SelectRandomCampPos(&bot)));
    }
    {   // Another map is never chosen.
        Player bot; bot.level = 1; bot.x = -100; bot.zone = 1; world();
        sTravelMgr.spots = {{0, 100, 0, 5}};
        sTravelMgr.hubs = {{0, 100, 0, 5}};
        Report("other_map_stays_out",
               !Picked(ai.SelectRandomGrindPos(&bot)) && !Picked(ai.SelectRandomCampPos(&bot)));
    }
    {   // A capital keeps its old rule: every zone of the map is in play.
        Player bot; bot.level = 30; bot.x = 200; bot.zone = 99; world();
        Report("capital_unchanged", Picked(ai.SelectRandomGrindPos(&bot)));
    }
    return failures ? 1 : 0;
}
