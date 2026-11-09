#ifndef OVERSEER_BRD_SHRINE_H
#define OVERSEER_BRD_SHRINE_H

// BLACKROCK DEPTHS: THE IRONFEL CARRIER IS WALKED TO THE SHRINE OF THAURISSAN
// (mod-overseer#1391).
//
// Blackrock Depths' lock-680 doors open only for a party member carrying the
// Shadowforge Key (11000), and the key's only source is Dark Iron Legacy
// (3802): Ironfel (10999, dropped by Fineous Darkvire, 9056) laid at the Shrine
// of Thaurissan (gameobject 164689) inside the dungeon. The bridge takes
// 3801/3802 for a ghost at Franclorn Forgewright and writes the 3802 turn-in
// once the carrier stands within 20 yards of the Shrine (the turn-in's own
// reach, SPAWN_WALK_ARRIVE_YARDS). Nothing walked the carrier there: the
// dungeon brain clears toward bosses and the Shrine is none, so the hand-in
// never fired and no member could earn the key (the dev realm held no 11000
// on 2026-11-07, and no member had 3801 or 3802 in its log).
//
// So a Blackrock Depths family run, while it clears, walks its party to the
// Shrine once a member holds 3802 with Ironfel in its bags and Fineous is dead,
// stands the carrier there for the bridge's turn-in, and then resumes the
// clear. The module turns nothing in and grants nothing.
//
// IT IS A WALK THAT ENDS. Done when the carrier no longer holds 3802 with
// Ironfel (the bridge's turn-in took it, and the core gave the key); given up
// when the walk comes no BRD_SHRINE_NEARER_YARDS nearer for
// BRD_SHRINE_WALK_SECONDS, or the carrier stands at the Shrine for
// BRD_SHRINE_STAND_SECONDS with the quest still in its log. Either way the
// run goes back to clearing and the Shrine is not asked again in that run.
//
// No AzerothCore include: tests/test_brd_shrine.cpp compiles this alone.

#include "overseer_decisions.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace OverseerDecisions
{

constexpr std::uint32_t BRD_MAP_ID = 230;
constexpr std::uint32_t BRD_DARK_IRON_LEGACY = 3802;
constexpr std::uint32_t BRD_IRONFEL = 10999;
constexpr std::uint32_t BRD_SHRINE_ENTRY = 164689;   // Shrine of Thaurissan
constexpr std::uint32_t BRD_FINEOUS_ENTRY = 9056;    // Fineous Darkvire
// Fineous is looked for by entry across the instance, as Weegli is (#1341).
constexpr float BRD_FINEOUS_SEARCH_YARDS = 2000.f;
// The bridge writes the turn-in from inside this, and the module takes it there.
constexpr float BRD_SHRINE_REACH_YARDS = SPAWN_WALK_ARRIVE_YARDS;
constexpr float BRD_SHRINE_NEARER_YARDS = 10.f;
constexpr std::time_t BRD_SHRINE_WALK_SECONDS = 10 * 60;
// Long enough for several bridge passes to see the carrier there and write the
// turn-in; the hand-in itself is one poll of the module once the row exists.
constexpr std::time_t BRD_SHRINE_STAND_SECONDS = 4 * 60;
// The dungeon brain is asked again to stay paused this often while the walk runs.
constexpr std::time_t BRD_SHRINE_PAUSE_RETRY_SECONDS = 30;
// A walk is not begun while any member alive inside is below this health percent.
constexpr float BRD_SHRINE_MIN_HEALTH_PCT = 60.f;

// One party member as the Shrine step reads it.
struct BrdShrineHolder
{
    std::string name;
    bool aliveInside{false};
    // 3802 is in its quest log (taken, not yet rewarded).
    bool holdsLegacy{false};
    std::uint32_t ironfel{0};
};

// The member the run walks to the Shrine, or empty: only on Blackrock Depths,
// only a member alive inside that holds 3802 with Ironfel in its bags. The
// first such member in the party's order.
inline std::string BrdShrineCarrier(std::uint32_t mapId, std::vector<BrdShrineHolder> const& party)
{
    if (mapId != BRD_MAP_ID)
        return std::string();
    for (BrdShrineHolder const& member : party)
        if (member.aliveInside && member.holdsLegacy && member.ironfel >= 1)
            return member.name;
    return std::string();
}

// The carrier is at the Shrine: within the turn-in's reach. A negative
// distance is an unread one and never counts.
inline bool BrdShrineReached(float yards)
{
    return yards >= 0.f && yards <= BRD_SHRINE_REACH_YARDS;
}

enum class BrdShrineStep : std::uint8_t
{
    Nothing,  // no carrier, Fineous alive, or the step is over for this run
    Walk,     // walk the party to the Shrine (the run's poll is the Shrine's)
    Stand,    // the carrier is there: hold it for the bridge's turn-in
    Done,     // the carrier no longer holds 3802 with Ironfel: resume the clear
    GiveUp,   // past a bound: resume the clear, not asked again this run
};

struct BrdShrineClock
{
    bool started{false};
    bool over{false};
    std::string carrier;
    std::time_t walkSince{0};
    float best{-1.f};
    std::time_t bestAt{0};
    bool standing{false};
    std::time_t standSince{0};
};

struct BrdShrineFacts
{
    std::string carrier;     // BrdShrineCarrier, empty for none
    bool fineousDead{false};
    // A Fineous was actually found in the instance. None found is a boss the
    // run has not reached (its grid is not loaded), never a dead one.
    bool fineousSeen{false};
    bool partyInCombat{false};  // any member alive inside is fighting
    // The lowest health percent among members alive inside (100 when unread).
    float lowestHealthPct{100.f};
    float yards{-1.f};       // the carrier to the Shrine; negative unread
    std::time_t now{0};
};

struct BrdShrineVerdict
{
    BrdShrineStep step{BrdShrineStep::Nothing};
    BrdShrineClock clock;
};

inline BrdShrineVerdict BrdShrineNext(BrdShrineFacts const& f, BrdShrineClock const& clock)
{
    BrdShrineVerdict v;
    v.clock = clock;
    if (clock.over)
        return v;
    if (f.carrier.empty())
    {
        // A walk under way whose carrier no longer holds 3802 with Ironfel:
        // the turn-in took it. Before any walk, there is nothing to do.
        if (clock.started)
        {
            v.step = BrdShrineStep::Done;
            v.clock.over = true;
        }
        return v;
    }
    // Before a walk: Fineous found and dead in THIS run, the party out of
    // combat and healthy. Nothing found is a boss not yet reached (#1470).
    if (!clock.started && (!f.fineousSeen || !f.fineousDead || f.partyInCombat ||
                           f.lowestHealthPct < BRD_SHRINE_MIN_HEALTH_PCT))
        return v;
    if (f.yards < 0.f)
    {
        // Nothing readable this poll: a walk under way is carried as it is.
        v.step = clock.started ? BrdShrineStep::Walk : BrdShrineStep::Nothing;
        return v;
    }
    if (!clock.started || clock.carrier != f.carrier)
    {
        v.clock.started = true;
        v.clock.carrier = f.carrier;
        v.clock.walkSince = f.now;
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
        v.clock.standing = false;
        v.clock.standSince = 0;
    }
    if (BrdShrineReached(f.yards))
    {
        if (!v.clock.standing)
        {
            v.clock.standing = true;
            v.clock.standSince = f.now;
        }
        if (f.now - v.clock.standSince >= BRD_SHRINE_STAND_SECONDS)
        {
            v.step = BrdShrineStep::GiveUp;
            v.clock.over = true;
            return v;
        }
        v.step = BrdShrineStep::Stand;
        return v;
    }
    v.clock.standing = false;
    v.clock.standSince = 0;
    if (f.yards <= v.clock.best - BRD_SHRINE_NEARER_YARDS)
    {
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
    }
    if (f.now - v.clock.bestAt >= BRD_SHRINE_WALK_SECONDS)
    {
        v.step = BrdShrineStep::GiveUp;
        v.clock.over = true;
        return v;
    }
    v.step = BrdShrineStep::Walk;
    return v;
}

inline char const* BrdShrineStepWord(BrdShrineStep step)
{
    switch (step)
    {
        case BrdShrineStep::Nothing: return "nothing";
        case BrdShrineStep::Walk:    return "walk";
        case BrdShrineStep::Stand:   return "stand";
        case BrdShrineStep::Done:    return "done";
        case BrdShrineStep::GiveUp:  return "give up";
    }
    return "nothing";
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_SHRINE_H
