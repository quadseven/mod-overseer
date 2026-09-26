#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* name, int got, int want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", name, got, want);
    ++failures;
}
}

int main()
{
    Check("no socket blocks", static_cast<int>(DungeonLeaderClientGate(false, 100, 1000)),
          static_cast<int>(LeaderClientGate::NoClient));
    Check("young socket settles",
          static_cast<int>(DungeonLeaderClientGate(true, 900, 1000)),
          static_cast<int>(LeaderClientGate::Settling));
    // The measured flap: a client that reconnects every 65s never opens the door.
    Check("a 65s flap never opens",
          static_cast<int>(DungeonLeaderClientGate(true, 1000 - 65, 1000)),
          static_cast<int>(LeaderClientGate::Settling));
    Check("settled socket opens",
          static_cast<int>(DungeonLeaderClientGate(true, 800, 1000)),
          static_cast<int>(LeaderClientGate::Open));
    Check("lost client holds inside bound",
          static_cast<int>(DungeonLeaderClientLoss(false, 1000, 1299)),
          static_cast<int>(LeaderClientLossAction::Hold));
    Check("lost client abandons after bound",
          static_cast<int>(DungeonLeaderClientLoss(false, 1000, 1301)),
          static_cast<int>(LeaderClientLossAction::Abandon));
    Check("connected client continues",
          static_cast<int>(DungeonLeaderClientLoss(true, 0, 1000)),
          static_cast<int>(LeaderClientLossAction::Continue));

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
