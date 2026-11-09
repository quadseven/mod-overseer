#ifndef OVERSEER_BRD_CORE_FRAGMENT_H
#define OVERSEER_BRD_CORE_FRAGMENT_H

// BLACKROCK DEPTHS: EACH ATTUNEMENT HOLDER LOOTS THE CORE FRAGMENT CHEST
// (mod-overseer#1456).
//
// Attunement to the Core (7848 Alliance, 7487 Horde) ends in one Core Fragment
// (18412) from the Core Fragment chest (gameobject 179553) beside the Molten
// Bridge on map 230. The chest opens only for a character that holds the
// quest. Nothing in the module walked to it or opened it, so no member of a
// Blackrock Depths run could finish the attunement.
//
// So a Blackrock Depths family run with a member alive inside whose log holds
// 7848 or 7487 and who carries no Core Fragment walks the party to the chest,
// and each such member in turn opens it the way a client does. A member
// without the quest, or already holding the fragment, is skipped. A run with
// no such member adds no detour: this module answers Nothing.
//
// EACH MEMBER'S TURN ENDS. Done when the member holds the fragment; given up
// when the walk comes no BRD_CORE_FRAGMENT_NEARER_YARDS nearer for
// BRD_CORE_FRAGMENT_WALK_SECONDS, or after BRD_CORE_FRAGMENT_LOOT_TRIES
// openings with the fragment still missing. A given-up member is not asked
// again in the run, and the next member's turn begins. When no member is left
// to ask, the run goes back to clearing. Every member gets one logged line
// (BrdCoreFragmentLine).
//
// The module grants nothing and turns nothing in: the item is the chest's own
// loot, and the hand-in is the attunement step's (#678).
//
// No AzerothCore include: tests/test_brd_core_fragment.cpp compiles this alone.

#include "overseer_brd_shrine.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace OverseerDecisions
{

constexpr std::uint32_t BRD_CORE_FRAGMENT_CHEST = 179553;
constexpr std::uint32_t BRD_ATTUNE_CORE_ALLIANCE = 7848;
constexpr std::uint32_t BRD_ATTUNE_CORE_HORDE = 7487;
constexpr std::uint32_t BRD_CORE_FRAGMENT = 18412;

// Within this many yards of the chest the member opens it, inside a chest's
// usual interaction distance.
constexpr float BRD_CORE_FRAGMENT_LOOT_YARDS = 4.0f;
// Within this many yards the member is walked the last stretch by itself, as an
// escort stops short of a point.
constexpr float BRD_CORE_FRAGMENT_CLOSE_YARDS = 25.0f;
constexpr float BRD_CORE_FRAGMENT_NEARER_YARDS = 10.f;
constexpr std::time_t BRD_CORE_FRAGMENT_WALK_SECONDS = 10 * 60;
constexpr unsigned BRD_CORE_FRAGMENT_LOOT_TRIES = 3;
// The brain is asked again to stay paused this often while the visit runs.
constexpr std::time_t BRD_CORE_FRAGMENT_PAUSE_RETRY_SECONDS = 30;

// One party member as the pickup reads it.
struct BrdCoreFragmentMember
{
    std::string name;
    bool aliveInside{false};
    // 7848 or 7487 is in its quest log (taken, not yet rewarded).
    bool holdsQuest{false};
    std::uint32_t fragments{0};
};

enum class BrdCoreFragmentSkip : std::uint8_t
{
    None,            // the member wants the fragment
    NotInside,       // dead, offline or outside the instance
    NoQuest,         // the log holds neither attunement quest
    HoldsFragment,   // already carries a Core Fragment
};

inline BrdCoreFragmentSkip BrdCoreFragmentSkipFor(BrdCoreFragmentMember const& m)
{
    if (!m.aliveInside)
        return BrdCoreFragmentSkip::NotInside;
    if (!m.holdsQuest)
        return BrdCoreFragmentSkip::NoQuest;
    if (m.fragments > 0)
        return BrdCoreFragmentSkip::HoldsFragment;
    return BrdCoreFragmentSkip::None;
}

inline char const* BrdCoreFragmentSkipWord(BrdCoreFragmentSkip skip)
{
    switch (skip)
    {
        case BrdCoreFragmentSkip::None:          return "wants it";
        case BrdCoreFragmentSkip::NotInside:     return "not alive inside";
        case BrdCoreFragmentSkip::NoQuest:       return "no attunement quest";
        case BrdCoreFragmentSkip::HoldsFragment: return "already holds a Core Fragment";
    }
    return "wants it";
}

// The members who want the fragment, in the party's order: only on Blackrock
// Depths, so no other dungeon ever gets a detour.
inline std::vector<std::string> BrdCoreFragmentWanters(
    std::uint32_t mapId, std::vector<BrdCoreFragmentMember> const& party)
{
    std::vector<std::string> wanters;
    if (mapId != BRD_MAP_ID)
        return wanters;
    for (BrdCoreFragmentMember const& member : party)
        if (BrdCoreFragmentSkipFor(member) == BrdCoreFragmentSkip::None)
            wanters.push_back(member.name);
    return wanters;
}

// The one logged line per member. `reason` is empty for a member who looted.
inline std::string BrdCoreFragmentLine(std::string const& name, std::string const& reason)
{
    if (reason.empty())
        return "core fragment: " + name + " looted";
    return "core fragment: " + name + " skipped (" + reason + ")";
}

inline bool BrdCoreFragmentAtChest(float yards)
{
    return yards >= 0.f && yards <= BRD_CORE_FRAGMENT_LOOT_YARDS;
}

inline bool BrdCoreFragmentClose(float yards)
{
    return yards >= 0.f && yards <= BRD_CORE_FRAGMENT_CLOSE_YARDS;
}

enum class BrdCoreFragmentStep : std::uint8_t
{
    Nothing,  // no wanter, chest not in the world, or over for this run
    Walk,     // walk the party to the chest (the run's poll is this step's)
    Loot,     // the current member is at the chest: open it
    Skip,     // the current member is given up (see the verdict's reason)
    Done,     // nobody is left to ask: resume the clear
    GiveUp,   // the chest is gone from the world mid-visit: resume the clear
};

struct BrdCoreFragmentClock
{
    bool started{false};
    bool over{false};
    std::string current;               // the member whose turn it is
    std::vector<std::string> settled;  // given up, not asked again
    std::time_t walkSince{0};
    float best{-1.f};
    std::time_t bestAt{0};
    unsigned lootTries{0};
};

struct BrdCoreFragmentFacts
{
    std::vector<std::string> wanters;  // BrdCoreFragmentWanters
    bool chestFound{false};            // the world holds a chest spawn on the map
    bool inCombat{false};              // the member whose turn it is is fighting
    float yards{-1.f};                 // that member to the chest; negative unread
    std::time_t now{0};
};

struct BrdCoreFragmentVerdict
{
    BrdCoreFragmentStep step{BrdCoreFragmentStep::Nothing};
    BrdCoreFragmentClock clock;
    // Set on the poll that learns a member's turn ended: who (the member now
    // holds the fragment, or died or left), or who was given up and why.
    std::string ended;
    std::string gaveUp;
    std::string reason;
};

namespace brd_core_fragment_detail
{
inline bool Has(std::vector<std::string> const& names, std::string const& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}
}  // namespace brd_core_fragment_detail

// SIX RULES, IN THIS ORDER:
//   1. Over for this run: Nothing.
//   2. The member whose turn it was no longer wants the fragment: ended.
//   3. Pick the first wanter not given up. None: Done if begun, else Nothing.
//   4. No chest spawn: GiveUp if begun, else Nothing. Not begun and the member
//      fights, or the distance is unread: Nothing (Walk if begun).
//   5. At the chest: Loot, and the opening after BRD_CORE_FRAGMENT_LOOT_TRIES
//      gives that member up (Skip).
//   6. Otherwise Walk, and a walk no nearer for the bound gives that member up.
inline BrdCoreFragmentVerdict BrdCoreFragmentNext(BrdCoreFragmentFacts const& f,
                                                  BrdCoreFragmentClock const& clock)
{
    using brd_core_fragment_detail::Has;
    BrdCoreFragmentVerdict v;
    v.clock = clock;
    if (clock.over)
        return v;

    if (!clock.current.empty() && !Has(f.wanters, clock.current))
    {
        v.ended = clock.current;
        v.clock.current.clear();
        v.clock.lootTries = 0;
        v.clock.best = -1.f;
    }

    std::string next;
    for (std::string const& name : f.wanters)
        if (!Has(v.clock.settled, name))
        {
            next = name;
            break;
        }
    if (next.empty())
    {
        if (clock.started)
        {
            v.step = BrdCoreFragmentStep::Done;
            v.clock.over = true;
        }
        return v;
    }
    if (!f.chestFound)
    {
        if (clock.started)
        {
            v.step = BrdCoreFragmentStep::GiveUp;
            v.clock.over = true;
        }
        return v;
    }
    if (!clock.started && f.inCombat)
        return v;
    if (f.yards < 0.f)
    {
        v.step = clock.started ? BrdCoreFragmentStep::Walk : BrdCoreFragmentStep::Nothing;
        return v;
    }
    if (v.clock.current != next)
    {
        v.clock.started = true;
        v.clock.current = next;
        v.clock.walkSince = f.now;
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
        v.clock.lootTries = 0;
    }
    if (BrdCoreFragmentAtChest(f.yards))
    {
        if (v.clock.lootTries >= BRD_CORE_FRAGMENT_LOOT_TRIES)
        {
            v.gaveUp = next;
            v.reason = "the chest did not give the fragment";
            v.clock.settled.push_back(next);
            v.clock.current.clear();
            v.step = BrdCoreFragmentStep::Skip;
            return v;
        }
        ++v.clock.lootTries;
        v.step = BrdCoreFragmentStep::Loot;
        return v;
    }
    if (f.yards <= v.clock.best - BRD_CORE_FRAGMENT_NEARER_YARDS)
    {
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
    }
    if (f.now - v.clock.bestAt >= BRD_CORE_FRAGMENT_WALK_SECONDS)
    {
        v.gaveUp = next;
        v.reason = "the walk to the chest came no nearer";
        v.clock.settled.push_back(next);
        v.clock.current.clear();
        v.step = BrdCoreFragmentStep::Skip;
        return v;
    }
    v.step = BrdCoreFragmentStep::Walk;
    return v;
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_CORE_FRAGMENT_H
