#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::TravelStuckAction;
using OverseerDecisions::TravelStuckDecision;
using OverseerDecisions::TravelNoProgressBackoffAction;
using OverseerDecisions::TravelNoProgressBackoffState;
using OverseerDecisions::TravelNoProgressBackoffStep;

int main()
{
    if (TravelStuckDecision(4, 5, true) != TravelStuckAction::Continue)
        return std::printf("failed to keep a progressing walk\n"), 1;
    if (TravelStuckDecision(5, 5, true) != TravelStuckAction::Release)
        return std::printf("failed to release at the retry limit\n"), 1;
    if (TravelStuckDecision(9, 5, true) != TravelStuckAction::Release)
        return std::printf("failed to release past the retry limit\n"), 1;

    // A COUNTER WHOSE WRITER IS NOT RUNNING DECIDES NOTHING. MoveFarTo owns
    // the number and runs only under `new rpg`; without it the number is
    // whatever an earlier walk left behind, at any size.
    if (TravelStuckDecision(5, 5, false) != TravelStuckAction::Continue)
        return std::printf("released on a counter nothing is writing\n"), 1;
    if (TravelStuckDecision(404, 5, false) != TravelStuckAction::Continue)
        return std::printf("released on a large stale counter\n"), 1;
    if (TravelStuckDecision(0, 5, false) != TravelStuckAction::Continue)
        return std::printf("a zero with no writer is not a release\n"), 1;

    // THE LIVE READING IS UNCHANGED, which is the property that keeps the
    // anti-teleport guard doing its job: with the strategy on, this answers
    // exactly as it did before the third input existed.
    for (unsigned attempts = 0; attempts < 12; ++attempts)
    {
        TravelStuckAction const want =
            attempts >= 5 ? TravelStuckAction::Release : TravelStuckAction::Continue;
        if (TravelStuckDecision(attempts, 5, true) != want)
            return std::printf("live reading changed at %u attempts\n", attempts), 1;
    }

    // The exact measured livelock: the value 5, repeated, on a character whose
    // engine had had `new rpg` removed. Twenty-five consecutive releases came
    // out of that reading and every one of them must now be a Continue.
    for (unsigned poll = 0; poll < 25; ++poll)
        if (TravelStuckDecision(5, 5, false) != TravelStuckAction::Continue)
            return std::printf("the measured livelock is still reachable\n"), 1;

    TravelNoProgressBackoffState backoff;
    if (TravelNoProgressBackoffStep(backoff, 100, 3, 60) !=
        TravelNoProgressBackoffAction::Continue)
        return std::printf("backoff started too early\n"), 1;
    if (TravelNoProgressBackoffStep(backoff, 101, 3, 60) !=
        TravelNoProgressBackoffAction::Continue)
        return std::printf("backoff did not count releases\n"), 1;
    if (TravelNoProgressBackoffStep(backoff, 102, 3, 60) !=
        TravelNoProgressBackoffAction::Backoff)
        return std::printf("backoff did not bound repeated releases\n"), 1;
    if (TravelNoProgressBackoffStep(backoff, 103, 3, 60) !=
        TravelNoProgressBackoffAction::Backoff)
        return std::printf("backoff expired early\n"), 1;
    if (TravelNoProgressBackoffStep(backoff, 162, 3, 60) !=
        TravelNoProgressBackoffAction::Continue)
        return std::printf("backoff did not reset after cooldown\n"), 1;
    return 0;
}
