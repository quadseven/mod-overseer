#ifndef OVERSEER_BRD_IRONFEL_H
#define OVERSEER_BRD_IRONFEL_H

// BLACKROCK DEPTHS: THE DARK IRON LEGACY CARRIER LOOTS IRONFEL FROM FINEOUS
// DARKVIRE'S CORPSE (mod-overseer#1454).
//
// The Shadowforge Key's one source is Dark Iron Legacy (3802): Ironfel (10999)
// laid at the Shrine of Thaurissan (overseer_brd_shrine.h). Ironfel is a 100
// percent drop from Fineous Darkvire (9056), flagged quest-required, so it is
// offered only to a member who holds 3802 and only when that member opens the
// corpse. Nothing made the member do that: on 2026-11-09 the first Blackrock
// Depths run to kill Fineous with a 3802 holder in the party (run 1682464)
// was pulled into the room's next packs within 30 seconds of the kill, the
// dungeon brain's loot yield timed out and moved on, and no Ironfel reached
// any bag on the realm. The Shrine step waits for a carrier that holds Ironfel,
// so it never started, and 3802 stayed in the log with status 3 (incomplete).
//
// So a Blackrock Depths family run, once Fineous is dead and a member alive
// inside holds 3802 without Ironfel while his corpse still offers it to that
// member, walks the party to the corpse and has that member loot it the way a
// client does (the hunt's corpse looter: open, take every slot, close). Then
// the Shrine step takes over, because the member now carries Ironfel.
//
// IT IS A WALK THAT ENDS. Done when the member no longer wants Ironfel (it
// holds it, or no longer holds 3802); given up when the walk comes no
// BRD_IRONFEL_NEARER_YARDS nearer for BRD_IRONFEL_WALK_SECONDS, when the corpse
// stops offering Ironfel to the member while it still wants it, or after
// BRD_IRONFEL_LOOT_TRIES loot attempts. Either way the run goes back to
// clearing and the corpse is not asked again in that run.
//
// The module grants nothing and rolls nothing: the item is the core's drop.
//
// No AzerothCore include: tests/test_brd_ironfel.cpp compiles this alone.

#include "overseer_brd_shrine.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace OverseerDecisions
{

// Within this many yards of the corpse the member opens it (the core's
// interaction distance is 5.5; the hunt's looter asks the same).
constexpr float BRD_IRONFEL_LOOT_YARDS = 5.0f;
// Within this many yards the member is walked the last stretch to the corpse by
// itself, as the hunt does, because an escort stops short of a point.
constexpr float BRD_IRONFEL_CLOSE_YARDS = 25.0f;
constexpr float BRD_IRONFEL_NEARER_YARDS = 10.f;
constexpr std::time_t BRD_IRONFEL_WALK_SECONDS = 10 * 60;
// Three openings of the corpse, one poll apart or more, with Ironfel still
// wanted: the corpse does not give it, and the run moves on.
constexpr unsigned BRD_IRONFEL_LOOT_TRIES = 3;
// The brain is asked again to stay paused this often while the walk runs.
constexpr std::time_t BRD_IRONFEL_PAUSE_RETRY_SECONDS = 30;

// One party member as the pickup reads it.
struct BrdIronfelHolder
{
    std::string name;
    bool aliveInside{false};
    // 3802 is in its quest log (taken, not yet rewarded).
    bool holdsLegacy{false};
    std::uint32_t ironfel{0};
};

// The member who wants Ironfel, or empty: only on Blackrock Depths, only a
// member alive inside that holds 3802 and carries no Ironfel. The first such
// member in the party's order.
inline std::string BrdIronfelWanter(std::uint32_t mapId,
                                    std::vector<BrdIronfelHolder> const& party)
{
    if (mapId != BRD_MAP_ID)
        return std::string();
    for (BrdIronfelHolder const& member : party)
        if (member.aliveInside && member.holdsLegacy && member.ironfel == 0)
            return member.name;
    return std::string();
}

// The member is at the corpse: close enough to open it. A negative distance is
// an unread one and never counts.
inline bool BrdIronfelAtCorpse(float yards)
{
    return yards >= 0.f && yards <= BRD_IRONFEL_LOOT_YARDS;
}

// The member is near enough to be walked the last stretch by itself.
inline bool BrdIronfelClose(float yards)
{
    return yards >= 0.f && yards <= BRD_IRONFEL_CLOSE_YARDS;
}

enum class BrdIronfelStep : std::uint8_t
{
    Nothing,  // no wanter, Fineous alive, no corpse offers it, or over for this run
    Walk,     // walk the party to the corpse (the run's poll is this step's)
    Loot,     // the wanter is at the corpse: open it and take what it offers
    Done,     // the wanter no longer wants Ironfel: resume the clear
    GiveUp,   // past a bound: resume the clear, not asked again this run
};

struct BrdIronfelClock
{
    bool started{false};
    bool over{false};
    std::string wanter;
    std::time_t walkSince{0};
    float best{-1.f};
    std::time_t bestAt{0};
    unsigned lootTries{0};
};

struct BrdIronfelFacts
{
    std::string wanter;       // BrdIronfelWanter, empty for none
    bool fineousDead{false};  // a Fineous was found and none of them lives
    // A dead Fineous's corpse still offers Ironfel to the wanter (an unlooted
    // quest slot the core says the member may take).
    bool corpseOffers{false};
    // The wanter is fighting: a pickup is not begun mid-fight (the room's next
    // packs come at the party within seconds of the kill), and one under way
    // is carried on.
    bool wanterInCombat{false};
    float yards{-1.f};        // the wanter to the corpse; negative unread
    std::time_t now{0};
};

struct BrdIronfelVerdict
{
    BrdIronfelStep step{BrdIronfelStep::Nothing};
    BrdIronfelClock clock;
};

// FIVE RULES, IN THIS ORDER:
//   1. Over for this run: Nothing.
//   2. Nobody wants Ironfel: Done if a pickup was under way (the member holds
//      it now, or left), else Nothing.
//   3. Not started and (Fineous alive or no corpse offers it or the wanter is
//      fighting): Nothing. Started and the corpse no longer offers it: GiveUp.
//   4. The distance is unread: a pickup under way is carried as it is.
//   5. At the corpse: Loot, and the third Loot with Ironfel still wanted gives
//      up. Otherwise Walk, and a walk no nearer for the bound gives up.
inline BrdIronfelVerdict BrdIronfelNext(BrdIronfelFacts const& f, BrdIronfelClock const& clock)
{
    BrdIronfelVerdict v;
    v.clock = clock;
    if (clock.over)
        return v;
    if (f.wanter.empty())
    {
        if (clock.started)
        {
            v.step = BrdIronfelStep::Done;
            v.clock.over = true;
        }
        return v;
    }
    if (!f.fineousDead || !f.corpseOffers)
    {
        if (clock.started)
        {
            v.step = BrdIronfelStep::GiveUp;
            v.clock.over = true;
        }
        return v;
    }
    if (!clock.started && f.wanterInCombat)
        return v;
    if (f.yards < 0.f)
    {
        v.step = clock.started ? BrdIronfelStep::Walk : BrdIronfelStep::Nothing;
        return v;
    }
    if (!clock.started || clock.wanter != f.wanter)
    {
        v.clock.started = true;
        v.clock.wanter = f.wanter;
        v.clock.walkSince = f.now;
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
        v.clock.lootTries = 0;
    }
    if (BrdIronfelAtCorpse(f.yards))
    {
        if (v.clock.lootTries >= BRD_IRONFEL_LOOT_TRIES)
        {
            v.step = BrdIronfelStep::GiveUp;
            v.clock.over = true;
            return v;
        }
        ++v.clock.lootTries;
        v.step = BrdIronfelStep::Loot;
        return v;
    }
    if (f.yards <= v.clock.best - BRD_IRONFEL_NEARER_YARDS)
    {
        v.clock.best = f.yards;
        v.clock.bestAt = f.now;
    }
    if (f.now - v.clock.bestAt >= BRD_IRONFEL_WALK_SECONDS)
    {
        v.step = BrdIronfelStep::GiveUp;
        v.clock.over = true;
        return v;
    }
    v.step = BrdIronfelStep::Walk;
    return v;
}

inline char const* BrdIronfelStepWord(BrdIronfelStep step)
{
    switch (step)
    {
        case BrdIronfelStep::Nothing: return "nothing";
        case BrdIronfelStep::Walk:    return "walk";
        case BrdIronfelStep::Loot:    return "loot";
        case BrdIronfelStep::Done:    return "done";
        case BrdIronfelStep::GiveUp:  return "give up";
    }
    return "nothing";
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_IRONFEL_H
