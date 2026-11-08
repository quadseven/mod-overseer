#ifndef OVERSEER_BRD_LEGACY_GHOST_H
#define OVERSEER_BRD_LEGACY_GHOST_H

// A GHOST AT BLACKROCK MOUNTAIN WALKS TO FRANCLORN FORGEWRIGHT FIRST
// (wow-overseer#2021).
//
// Blackrock Depths' Upper City sits behind lock 680, which opens for the
// Shadowforge Key (11000) or lockpicking 250 (overseer_brd_wing.h). The key's
// one source is Dark Iron Legacy: Franclorn Forgewright (8888) gives 3801 and
// 3802, and he is seen only by a ghost (flags_extra 1026). The bridge writes
// the take and turn-in rows for a ghost of level 48 or more that stands within
// 20 yards of him (wow-overseer#811); the Shrine hand-in inside the dungeon
// (overseer_brd_shrine.h) then earns the key.
//
// Nothing walked a ghost there. Franclorn stands at (-7596, -1115, 252.8) on
// map 0, about 400 yards from the Blackrock Mountain graveyard where a ghost
// that died inside map 230 lands, and 120 yards above it. A corpse run from
// that graveyard to a body inside the instance goes to the door and never
// passes him. So a family with no rogue never took 3801, never had Ironfel
// drop for it (quest loot on Fineous Darkvire), and never earned the key; the
// dev realm held no copy of 11000 and no character with 3801 or 3802.
//
// So a member of a family with a Blackrock Depths order that dies there, and
// whose family cannot open the doors, takes a detour as a ghost: it walks to
// Franclorn, stands there while the bridge's rows take the chain, and then
// goes back to its ordinary recovery. Once per member: a detour that ends, by
// the quest or by running out of time, is not walked again.
//
// No AzerothCore include: tests/test_brd_legacy_ghost.cpp compiles this alone.

#include <cstdint>

namespace OverseerDecisions
{

// Franclorn Forgewright's one spawn (acore_world.creature guid 5363).
constexpr std::uint32_t LEGACY_FRANCLORN_ENTRY = 8888;
constexpr std::uint32_t LEGACY_FRANCLORN_MAP = 0;
constexpr float LEGACY_FRANCLORN_X = -7596.0f;
constexpr float LEGACY_FRANCLORN_Y = -1115.0f;
constexpr float LEGACY_FRANCLORN_Z = 252.791f;
constexpr std::uint32_t LEGACY_BRD_MAP = 230;
constexpr std::uint32_t LEGACY_TALE_QUEST = 3801;
constexpr std::uint32_t LEGACY_KEY_QUEST = 3802;
constexpr std::uint32_t LEGACY_SHADOWFORGE_KEY = 11000;
// The bridge takes the chain for a ghost of this level or more.
constexpr std::uint32_t LEGACY_MIN_LEVEL = 48;
// Lock 680's lockpicking slot (BRD_UPPER_CITY_LOCKPICKING).
constexpr std::uint32_t LEGACY_LOCKPICKING = 250;
// The bridge writes its rows for a ghost within 20 yards (LEGACY_REACH_YARDS
// there, SPAWN_WALK_ARRIVE_YARDS here); the walk ends a margin inside that.
constexpr float LEGACY_ARRIVE_YARDS = 12.0f;
// "At Blackrock Mountain": within this many yards of Franclorn, flat. The
// graveyard a Blackrock Depths ghost lands at is about 400 yards off, the
// instance door about 470.
constexpr float LEGACY_MOUNTAIN_YARDS = 700.0f;
// The walk is given this long from its start, a climb of about 400 yards at a
// ghost's pace with room for the ramps; then the detour is over.
constexpr std::int64_t LEGACY_WALK_SECONDS = 480;
// Standing at Franclorn, the ghost waits this long for the bridge's rows: a
// guild pass writes them, the quest drive runs them, both on a cycle of a
// minute or so.
constexpr std::int64_t LEGACY_WAIT_SECONDS = 300;

enum class LegacyGhostStep : std::uint8_t
{
    None,  // no detour: the ordinary recovery goes on
    Walk,  // walk the ghost toward Franclorn
    Wait,  // stand at Franclorn while the chain is taken
    Done,  // the detour is over (taken, or out of time): hand the ghost back
};

struct LegacyGhostFacts
{
    bool ghost{false};  // released: PLAYER_FLAGS_GHOST
    std::uint32_t level{0};
    std::uint32_t ghostMap{0};
    // Flat yards from the ghost to Franclorn, and the 3D yards that count for
    // reach (he stands on a ledge above the graveyard).
    float ghostFlatYards{0.f};
    float ghostYards{0.f};
    // Where the corpse lies: its map, and flat yards to Franclorn when it is
    // on his map.
    std::uint32_t corpseMap{0};
    float corpseFlatYards{0.f};
    // The family has a Blackrock Depths order: a queued or active order on
    // the keyword, or an active run on map 230.
    bool familyBrdOrder{false};
    // Somebody in the family can already open the Upper City or is on the way
    // to: carries the key, holds 3802 or was rewarded it, or picks locks at
    // LEGACY_LOCKPICKING.
    bool familyHasKey{false};
    bool familyHasLockpicker{false};
    // This member already took a detour that ended.
    bool spent{false};
    // This detour has begun, how long ago, and how long the ghost has stood
    // in reach of Franclorn (0 until it arrives).
    bool begun{false};
    std::int64_t walkSeconds{0};
    std::int64_t atFranclornSeconds{0};
};

// At Blackrock Mountain: a ghost on Franclorn's map within
// LEGACY_MOUNTAIN_YARDS of him, whose corpse lies inside Blackrock Depths or
// on the mountain too. The adapter asks this first, so nothing else is read
// for a ghost anywhere else.
inline bool LegacyGhostAtMountain(LegacyGhostFacts const& f)
{
    if (!f.ghost || f.ghostMap != LEGACY_FRANCLORN_MAP || f.ghostFlatYards > LEGACY_MOUNTAIN_YARDS)
        return false;
    if (f.corpseMap == LEGACY_BRD_MAP)
        return true;
    return f.corpseMap == LEGACY_FRANCLORN_MAP && f.corpseFlatYards <= LEGACY_MOUNTAIN_YARDS;
}

// The family needs this member at Franclorn: it is a ghost at the mountain of
// a level the chain takes, the family has a Blackrock Depths order, and no
// member can open the Upper City or has started the chain.
inline bool LegacyGhostNeeded(LegacyGhostFacts const& f)
{
    return LegacyGhostAtMountain(f) && f.level >= LEGACY_MIN_LEVEL && f.familyBrdOrder &&
           !f.familyHasKey && !f.familyHasLockpicker;
}

// SIX RULES, IN THIS ORDER:
//   1. A detour that has begun and is no longer needed (the chain is taken,
//      the member left the mountain or came back to life) is Done.
//   2. Not needed, or this member already took its detour: None.
//   3. In reach of Franclorn and waited LEGACY_WAIT_SECONDS: Done.
//   4. In reach: Wait.
//   5. Walked LEGACY_WALK_SECONDS without reaching him: Done.
//   6. Otherwise: Walk.
inline LegacyGhostStep LegacyGhostNext(LegacyGhostFacts const& f)
{
    bool const needed = LegacyGhostNeeded(f);
    if (f.begun && !needed)
        return LegacyGhostStep::Done;
    if (!needed || (f.spent && !f.begun))
        return LegacyGhostStep::None;
    if (f.ghostYards <= LEGACY_ARRIVE_YARDS)
        return f.atFranclornSeconds >= LEGACY_WAIT_SECONDS ? LegacyGhostStep::Done
                                                           : LegacyGhostStep::Wait;
    if (f.walkSeconds >= LEGACY_WALK_SECONDS)
        return LegacyGhostStep::Done;
    return LegacyGhostStep::Walk;
}

inline char const* LegacyGhostStepWord(LegacyGhostStep step)
{
    switch (step)
    {
        case LegacyGhostStep::None: return "none";
        case LegacyGhostStep::Walk: return "walk to Franclorn";
        case LegacyGhostStep::Wait: return "wait at Franclorn";
        case LegacyGhostStep::Done: return "detour over";
    }
    return "?";
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_LEGACY_GHOST_H
