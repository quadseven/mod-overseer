#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::TravelStuckAction;
using OverseerDecisions::TravelStuckDecision;

int main()
{
    if (TravelStuckDecision(4, 5) != TravelStuckAction::Continue)
        return std::printf("failed to keep a progressing walk\n"), 1;
    if (TravelStuckDecision(5, 5) != TravelStuckAction::Release)
        return std::printf("failed to release at the retry limit\n"), 1;
    if (TravelStuckDecision(9, 5) != TravelStuckAction::Release)
        return std::printf("failed to release past the retry limit\n"), 1;
    return 0;
}
