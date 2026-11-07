#ifndef OVERSEER_ZF_STEPS_H
#define OVERSEER_ZF_STEPS_H

// ZUL'FARRAK'S TWO LAST STEPS HAVE AN EXIT (#1389).
//
// After the End Door fixes (#1337, #1341), Zul'Farrak guild finder runs still
// timed out at two places. From the worldserver log archive 2026-11-03T14:58Z
// to 2026-11-05T15:00Z, 22 runs ended and 6 timed out:
//
// - 4 timed out with the leader's last target `Temple Summit (Executioner
//   event)`, the pyramid event in front of Shadowpriest Sezz'ziz, at 3 of 7
//   bosses. None reached the End Door. Read run by run, the party fights its
//   way up the stairs, wipes, walks back and rests, over and over. Every pull
//   and every rest resets the stall ladder's stillness clock (a hold is
//   stillness the overseer asked for, a fight holds the ladder), so the ladder
//   never fires and the run waits out the two hour ceiling.
// - 2 timed out after the module opened the End Door, still heading to Chief
//   Ukorz Sandscalp, the last path logged 10 to 30 minutes before the timeout.
//
// So each step gets a clock of its own, read off the instance and the boss
// credit rather than off movement or combat, which is what the ladder could
// not see past:
//
// THE PYRAMID STEP is the run's step while Theka, Antu'sul and Zum'rah are
// credited (or the event has started), Ukorz is not, the End Door is not done
// and the pyramid's trolls are not all dead (past that, ZulFarrakEndDoorNext
// owns the run). Its clock is the instance's DATA_PYRAMID phase: a phase that
// stands ZF_PYRAMID_STALL_SECONDS unchanged gives up. Measured over the cleared
// runs from 2026-11-05 to 2026-11-07, the brain's first path to the summit to
// its first path to Ukorz took 14 to 22 minutes for the whole event, every
// phase included, so 25 minutes on one phase is not a slow event.
//
// THE UKORZ WALK is the run's step once the End Door is done and Ukorz lives
// uncredited. Its clock is the tank's distance to Ukorz: a walk that comes no
// ZF_UKORZ_NEARER_YARDS nearer than its best for ZF_UKORZ_WALK_SECONDS gives
// up. Inside ZF_UKORZ_REACHED_YARDS the party is at Ukorz, his fight is the
// boss's own, and the walk is not judged. Cleared runs took 2 to 9 minutes from
// their first path to Ukorz to the end of the run.
//
// Both clocks run on through wipes and corpse runs: a run that dies on the way
// over and over is the failure measured, not a reason to wait. While nothing
// can be read (no tank alive inside), the clock is carried, not reset.
//
// GIVING UP ENDS THE RUN AS STALLED, with one log line naming the run, the step
// and the reason. Skipping is not an exit here: past the pyramid the brain's
// next boss is Ukorz behind a shut door, and the run would only stall there.
//
// No AzerothCore include: tests/test_zf_steps.cpp compiles this alone.

#include "overseer_decisions.h"

#include <cstdint>
#include <ctime>

namespace OverseerDecisions
{

// DungeonEncounter.dbc bits for map 209 (mod-dungeon-clear's ZF roster).
constexpr std::uint32_t ZF_BIT_ANTUSUL = 1u << 2;
constexpr std::uint32_t ZF_BIT_THEKA = 1u << 3;
constexpr std::uint32_t ZF_BIT_ZUMRAH = 1u << 4;
constexpr std::uint32_t ZF_BIT_UKORZ = 1u << 7;
constexpr std::uint32_t ZF_SUMMIT_BEFORE = ZF_BIT_ANTUSUL | ZF_BIT_THEKA | ZF_BIT_ZUMRAH;
constexpr std::uint32_t ZF_UKORZ_ENTRY = 7267;  // NPC_UKORZ_SANDSCALP
constexpr std::uint32_t ZF_PYRAMID_NOT_STARTED = 0;

constexpr std::time_t ZF_PYRAMID_STALL_SECONDS = 25 * 60;
constexpr std::time_t ZF_UKORZ_WALK_SECONDS = 15 * 60;
constexpr float ZF_UKORZ_NEARER_YARDS = 10.f;
constexpr float ZF_UKORZ_REACHED_YARDS = 40.f;

constexpr char const* ZF_SUMMIT_STEP = "Temple Summit (Executioner event)";
constexpr char const* ZF_UKORZ_STEP = "the walk to Chief Ukorz Sandscalp";

enum class ZulFarrakStepVerdict : std::uint8_t
{
    Idle,      // not this step: the clock is cleared
    Carry,     // nothing readable this poll: the clock is kept as it is
    Watching,  // this step, inside its bound
    GiveUp,    // this step, past its bound: end the run with the reason
};

struct ZulFarrakSummitFacts
{
    // The instance script was read (the tank is alive inside).
    bool read{false};
    std::uint32_t creditedMask{0};
    std::uint32_t pyramidPhase{ZF_PYRAMID_NOT_STARTED};
    std::uint32_t endDoorState{ZF_END_DOOR_NOT_STARTED};
    std::time_t now{0};
};

struct ZulFarrakSummitClock
{
    bool active{false};
    std::uint32_t phase{0};
    std::time_t since{0};
};

struct ZulFarrakSummitStep
{
    ZulFarrakStepVerdict verdict{ZulFarrakStepVerdict::Idle};
    ZulFarrakSummitClock clock;
    // How long the phase stood, on GiveUp.
    std::time_t seconds{0};
};

inline bool ZulFarrakSummitIsTheStep(ZulFarrakSummitFacts const& f)
{
    if (f.creditedMask & ZF_BIT_UKORZ)
        return false;
    if (f.endDoorState == ZF_END_DOOR_DONE)
        return false;
    if (f.pyramidPhase >= ZF_PYRAMID_KILLED_ALL_TROLLS)
        return false;
    return f.pyramidPhase > ZF_PYRAMID_NOT_STARTED ||
           (f.creditedMask & ZF_SUMMIT_BEFORE) == ZF_SUMMIT_BEFORE;
}

inline ZulFarrakSummitStep ZulFarrakSummitNext(ZulFarrakSummitFacts const& f,
                                              ZulFarrakSummitClock const& clock)
{
    ZulFarrakSummitStep step;
    if (!f.read)
    {
        step.verdict = clock.active ? ZulFarrakStepVerdict::Carry : ZulFarrakStepVerdict::Idle;
        step.clock = clock;
        return step;
    }
    if (!ZulFarrakSummitIsTheStep(f))
        return step;
    if (!clock.active || clock.phase != f.pyramidPhase || clock.since > f.now)
    {
        step.verdict = ZulFarrakStepVerdict::Watching;
        step.clock = ZulFarrakSummitClock{true, f.pyramidPhase, f.now};
        return step;
    }
    step.clock = clock;
    if (f.now - clock.since >= ZF_PYRAMID_STALL_SECONDS)
    {
        step.verdict = ZulFarrakStepVerdict::GiveUp;
        step.seconds = f.now - clock.since;
        return step;
    }
    step.verdict = ZulFarrakStepVerdict::Watching;
    return step;
}

struct ZulFarrakUkorzFacts
{
    // The instance script was read (the tank is alive inside).
    bool read{false};
    std::uint32_t creditedMask{0};
    std::uint32_t endDoorState{ZF_END_DOOR_NOT_STARTED};
    // A living Ukorz was found in the instance, and the tank's distance to him.
    bool ukorzFound{false};
    float yards{0.f};
    std::time_t now{0};
};

struct ZulFarrakUkorzClock
{
    bool active{false};
    // The tank's distance when the clock last restarted, and when: at the
    // step's start, and on every ZF_UKORZ_NEARER_YARDS gain since.
    float best{0.f};
    std::time_t since{0};
};

struct ZulFarrakUkorzStep
{
    ZulFarrakStepVerdict verdict{ZulFarrakStepVerdict::Idle};
    ZulFarrakUkorzClock clock;
    // How long the walk came no nearer, on GiveUp.
    std::time_t seconds{0};
};

inline ZulFarrakUkorzStep ZulFarrakUkorzNext(ZulFarrakUkorzFacts const& f,
                                            ZulFarrakUkorzClock const& clock)
{
    ZulFarrakUkorzStep step;
    if (!f.read)
    {
        step.verdict = clock.active ? ZulFarrakStepVerdict::Carry : ZulFarrakStepVerdict::Idle;
        step.clock = clock;
        return step;
    }
    if ((f.creditedMask & ZF_BIT_UKORZ) || f.endDoorState != ZF_END_DOOR_DONE || !f.ukorzFound)
        return step;
    step.verdict = ZulFarrakStepVerdict::Watching;
    // At Ukorz: his fight is the boss's own, and the walk is over.
    if (f.yards <= ZF_UKORZ_REACHED_YARDS || !clock.active || clock.since > f.now ||
        f.yards <= clock.best - ZF_UKORZ_NEARER_YARDS)
    {
        step.clock = ZulFarrakUkorzClock{true, f.yards, f.now};
        return step;
    }
    // The best only moves on a ZF_UKORZ_NEARER_YARDS gain, so a slow walk
    // that keeps closing in still resets the clock every ten yards.
    step.clock = clock;
    if (f.now - clock.since >= ZF_UKORZ_WALK_SECONDS)
    {
        step.verdict = ZulFarrakStepVerdict::GiveUp;
        step.seconds = f.now - clock.since;
    }
    return step;
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_ZF_STEPS_H
