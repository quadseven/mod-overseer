/*
 * Zul'Farrak's pyramid event and the walk to Ukorz each give up inside a bound
 * shorter than the run's ceiling (#1389).
 *
 * The live failure, worldserver log archive 2026-11-03 to 2026-11-05: 4 guild
 * finder runs timed out at `Temple Summit (Executioner event)` at 3 of 7
 * bosses, fighting, wiping and resting on the stairs until the two hour
 * ceiling, and 2 timed out after the End Door opened, still heading to Chief
 * Ukorz Sandscalp. The rules in src/overseer_zf_steps.h end a run whose
 * pyramid phase stands 25 minutes unchanged, or whose walk to Ukorz comes no
 * ten yards nearer in 15 minutes.
 *
 * THE NEGATIVE TESTS MATTER MOST: an event that advances, a walk that closes
 * in slowly, a party at Ukorz, a poll with nothing readable, and every state
 * outside the two steps must never give up.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_zf_steps.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

char const* Name(ZulFarrakStepVerdict v)
{
    switch (v)
    {
        case ZulFarrakStepVerdict::Idle:     return "idle";
        case ZulFarrakStepVerdict::Carry:    return "carry";
        case ZulFarrakStepVerdict::Watching: return "watching";
        case ZulFarrakStepVerdict::GiveUp:   return "give up";
    }
    return "unknown";
}

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckVerdict(char const* what, ZulFarrakStepVerdict got, ZulFarrakStepVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

constexpr std::time_t T0 = 1794000000;
// The guild run poll's clock.
constexpr std::time_t POLL = 5;

ZulFarrakSummitFacts Summit(std::time_t now, std::uint32_t phase)
{
    ZulFarrakSummitFacts f;
    f.read = true;
    f.creditedMask = ZF_SUMMIT_BEFORE;
    f.pyramidPhase = phase;
    f.endDoorState = ZF_END_DOOR_NOT_STARTED;
    f.now = now;
    return f;
}

ZulFarrakUkorzFacts Walk(std::time_t now, float yards)
{
    ZulFarrakUkorzFacts f;
    f.read = true;
    f.creditedMask = ZF_SUMMIT_BEFORE;
    f.endDoorState = ZF_END_DOOR_DONE;
    f.ukorzFound = true;
    f.yards = yards;
    f.now = now;
    return f;
}

void TheBoundsAreTheLiveOnes()
{
    Check("pyramid bound is 25 minutes", ZF_PYRAMID_STALL_SECONDS == 25 * 60);
    Check("walk bound is 15 minutes", ZF_UKORZ_WALK_SECONDS == 15 * 60);
    Check("both are well inside the two hour ceiling",
          ZF_PYRAMID_STALL_SECONDS < GUILD_RUN_CEILING_SECONDS &&
              ZF_UKORZ_WALK_SECONDS < GUILD_RUN_CEILING_SECONDS);
    Check("ten yards count as nearer", ZF_UKORZ_NEARER_YARDS == 10.f);
    Check("forty yards is at Ukorz", ZF_UKORZ_REACHED_YARDS == 40.f);
    Check("Theka, Antu'sul and Zum'rah come before the summit",
          ZF_SUMMIT_BEFORE == ((1u << 2) | (1u << 3) | (1u << 4)));
    Check("Ukorz is bit 7", ZF_BIT_UKORZ == (1u << 7));
}

// The measured shape: three bosses down, the party wiping on the stairs with
// the pyramid never started. A fight, a wipe and a rest change nothing this
// rule reads, so it gives up on the first poll at 25 minutes.
void APyramidThatNeverStartsGivesUp()
{
    ZulFarrakSummitClock clock;
    ZulFarrakSummitStep step;
    std::time_t t = T0;
    for (; t < T0 + ZF_PYRAMID_STALL_SECONDS; t += POLL)
    {
        step = ZulFarrakSummitNext(Summit(t, ZF_PYRAMID_NOT_STARTED), clock);
        CheckVerdict("under the bound is watched", step.verdict, ZulFarrakStepVerdict::Watching);
        clock = step.clock;
    }
    Check("the clock started at the first poll", clock.since == T0);
    step = ZulFarrakSummitNext(Summit(t, ZF_PYRAMID_NOT_STARTED), clock);
    CheckVerdict("25 minutes on one phase gives up", step.verdict, ZulFarrakStepVerdict::GiveUp);
    Check("and says how long", step.seconds == ZF_PYRAMID_STALL_SECONDS);
}

// A recorded phase sequence from a cleared run's shape: cages, stair, three
// waves and the trolls dead, each inside the bound. Every change restarts the
// clock, so the whole event can run far longer than 25 minutes and never give
// up, and once the trolls are dead the step is the End Door's, not this one.
void AnAdvancingEventNeverGivesUp()
{
    struct Mark { std::time_t at; std::uint32_t phase; };
    std::vector<Mark> const sequence = {
        {0, 0}, {20 * 60, 1}, {21 * 60, 2}, {23 * 60, 3}, {40 * 60, 4},
        {41 * 60, 5}, {60 * 60, 6}, {61 * 60, 7}, {80 * 60, 8},
    };
    ZulFarrakSummitClock clock;
    std::size_t next = 0;
    std::uint32_t phase = 0;
    for (std::time_t t = T0; t <= T0 + 90 * 60; t += POLL)
    {
        while (next < sequence.size() && T0 + sequence[next].at <= t)
            phase = sequence[next++].phase;
        ZulFarrakSummitStep const step = ZulFarrakSummitNext(Summit(t, phase), clock);
        if (step.verdict == ZulFarrakStepVerdict::GiveUp)
        {
            std::printf("FAIL an advancing event gave up at +%lds, phase %u\n",
                        static_cast<long>(t - T0), phase);
            ++failures;
            return;
        }
        clock = step.clock;
        if (phase >= ZF_PYRAMID_KILLED_ALL_TROLLS)
            CheckVerdict("trolls dead: the End Door's step", step.verdict,
                         ZulFarrakStepVerdict::Idle);
    }
}

void APhaseChangeRestartsTheClock()
{
    ZulFarrakSummitClock clock{true, 3, T0};
    ZulFarrakSummitStep step =
        ZulFarrakSummitNext(Summit(T0 + ZF_PYRAMID_STALL_SECONDS - 1, 4), clock);
    CheckVerdict("a new phase is watched", step.verdict, ZulFarrakStepVerdict::Watching);
    Check("from now", step.clock.since == T0 + ZF_PYRAMID_STALL_SECONDS - 1 &&
                          step.clock.phase == 4);
}

void OutsideTheSummitStepIsIdle()
{
    ZulFarrakSummitClock const clock{true, 0, T0};
    std::time_t const late = T0 + 3 * ZF_PYRAMID_STALL_SECONDS;

    ZulFarrakSummitFacts f = Summit(late, 0);
    f.creditedMask = ZF_BIT_THEKA | ZF_BIT_ANTUSUL;
    CheckVerdict("Zum'rah still up and the event not started", ZulFarrakSummitNext(f, clock).verdict,
                 ZulFarrakStepVerdict::Idle);

    f = Summit(late, 0);
    f.creditedMask |= ZF_BIT_UKORZ;
    CheckVerdict("Ukorz credited", ZulFarrakSummitNext(f, clock).verdict,
                 ZulFarrakStepVerdict::Idle);

    f = Summit(late, 0);
    f.endDoorState = ZF_END_DOOR_DONE;
    CheckVerdict("End Door done", ZulFarrakSummitNext(f, clock).verdict,
                 ZulFarrakStepVerdict::Idle);

    f = Summit(late, ZF_PYRAMID_KILLED_ALL_TROLLS);
    CheckVerdict("trolls dead", ZulFarrakSummitNext(f, clock).verdict, ZulFarrakStepVerdict::Idle);

    // A started event is the step even when a boss before it was skipped.
    f = Summit(T0, 3);
    f.creditedMask = ZF_BIT_THEKA;
    CheckVerdict("an event under way is the step", ZulFarrakSummitNext(f, {}).verdict,
                 ZulFarrakStepVerdict::Watching);
}

// No tank alive inside: nothing to read. The clock is carried through the
// corpse run, so a wipe loop still reaches the bound on the first readable
// poll after it.
void AWipeCarriesTheClock()
{
    ZulFarrakSummitClock const clock{true, 0, T0};
    ZulFarrakSummitFacts dead;
    dead.now = T0 + 600;
    ZulFarrakSummitStep step = ZulFarrakSummitNext(dead, clock);
    CheckVerdict("unreadable carries", step.verdict, ZulFarrakStepVerdict::Carry);
    Check("unchanged", step.clock.active && step.clock.since == T0);
    step = ZulFarrakSummitNext(Summit(T0 + ZF_PYRAMID_STALL_SECONDS, 0), step.clock);
    CheckVerdict("back inside past the bound gives up", step.verdict,
                 ZulFarrakStepVerdict::GiveUp);
    CheckVerdict("unreadable with no clock is idle", ZulFarrakSummitNext(dead, {}).verdict,
                 ZulFarrakStepVerdict::Idle);
}

// Run 1442262's shape: the door opened, the party wiped on the way and the
// tank never got nearer than its first reading.
void AWalkThatGetsNoNearerGivesUp()
{
    ZulFarrakUkorzClock clock;
    ZulFarrakUkorzStep step;
    std::time_t t = T0;
    float yards = 290.f;
    for (; t < T0 + ZF_UKORZ_WALK_SECONDS; t += POLL)
    {
        // Pacing: back and forth without ever gaining ten yards on the start.
        yards = (t / POLL) % 2 ? 290.f : 282.f;
        step = ZulFarrakUkorzNext(Walk(t, yards), clock);
        CheckVerdict("under the bound is watched", step.verdict, ZulFarrakStepVerdict::Watching);
        clock = step.clock;
    }
    step = ZulFarrakUkorzNext(Walk(t, yards), clock);
    CheckVerdict("15 minutes no nearer gives up", step.verdict, ZulFarrakStepVerdict::GiveUp);
    Check("and says how long", step.seconds == ZF_UKORZ_WALK_SECONDS);
}

// A slow walk that keeps closing in, a yard a poll, never gives up, and it
// takes far longer than the bound to cover the distance.
void ASlowWalkThatClosesInNeverGivesUp()
{
    ZulFarrakUkorzClock clock;
    float yards = 300.f;
    for (std::time_t t = T0; yards > ZF_UKORZ_REACHED_YARDS; t += POLL, yards -= 0.5f)
    {
        ZulFarrakUkorzStep const step = ZulFarrakUkorzNext(Walk(t, yards), clock);
        if (step.verdict == ZulFarrakStepVerdict::GiveUp)
        {
            std::printf("FAIL a closing walk gave up at %.1f yards\n", yards);
            ++failures;
            return;
        }
        clock = step.clock;
    }
}

void AtUkorzTheWalkIsNotJudged()
{
    ZulFarrakUkorzClock const clock{true, 30.f, T0};
    ZulFarrakUkorzStep const step = ZulFarrakUkorzNext(Walk(T0 + 3600, 30.f), clock);
    CheckVerdict("a party fighting at Ukorz is watched", step.verdict,
                 ZulFarrakStepVerdict::Watching);
    Check("and its clock restarts", step.clock.since == T0 + 3600);
}

void OutsideTheWalkIsIdle()
{
    ZulFarrakUkorzClock const clock{true, 290.f, T0};
    std::time_t const late = T0 + 3 * ZF_UKORZ_WALK_SECONDS;

    ZulFarrakUkorzFacts f = Walk(late, 290.f);
    f.endDoorState = ZF_END_DOOR_NOT_STARTED;
    CheckVerdict("door not done", ZulFarrakUkorzNext(f, clock).verdict, ZulFarrakStepVerdict::Idle);

    f = Walk(late, 290.f);
    f.creditedMask |= ZF_BIT_UKORZ;
    CheckVerdict("Ukorz credited", ZulFarrakUkorzNext(f, clock).verdict, ZulFarrakStepVerdict::Idle);

    f = Walk(late, 290.f);
    f.ukorzFound = false;
    CheckVerdict("no living Ukorz found", ZulFarrakUkorzNext(f, clock).verdict,
                 ZulFarrakStepVerdict::Idle);

    ZulFarrakUkorzFacts dead;
    dead.now = late;
    ZulFarrakUkorzStep const step = ZulFarrakUkorzNext(dead, clock);
    CheckVerdict("tank not alive inside carries", step.verdict, ZulFarrakStepVerdict::Carry);
    Check("unchanged", step.clock.since == T0 && step.clock.best == 290.f);
}

}  // namespace

int main()
{
    TheBoundsAreTheLiveOnes();
    APyramidThatNeverStartsGivesUp();
    AnAdvancingEventNeverGivesUp();
    APhaseChangeRestartsTheClock();
    OutsideTheSummitStepIsIdle();
    AWipeCarriesTheClock();
    AWalkThatGetsNoNearerGivesUp();
    ASlowWalkThatClosesInNeverGivesUp();
    AtUkorzTheWalkIsNotJudged();
    OutsideTheWalkIsIdle();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("Zul'Farrak's pyramid and the walk to Ukorz give up inside their bounds\n");
    return EXIT_SUCCESS;
}
