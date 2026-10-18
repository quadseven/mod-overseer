/*
 * Who holds a dungeon run's leading seat (#792).
 *
 * #735 asked a run's leader for an open socket, and the dungeon-clear issuer
 * test asked for one too, so a family whose head plays headless entered its
 * dungeon and could never clear it: the brain stayed OFF, the run was held for
 * a client that never comes, and it ended `client_lost`, every run. The seat
 * is held by an open socket OR by a headless bot in the world that is named on
 * the headless roster - and by nothing else, so a client leader that drops
 * keeps its settle window, its loss hold and its `client_lost` outcome.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, int got, int want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}
}

int main()
{
    std::vector<std::string> const headless{"Leader", "Follower"};

    // The seat itself.
    Check("an open socket holds the seat", DungeonLeaderHoldsTheSeat(true, false), 1);
    Check("a listed headless bot holds the seat", DungeonLeaderHoldsTheSeat(false, true), 1);
    Check("neither holds nothing", DungeonLeaderHoldsTheSeat(false, false), 0);

    // Which headless character counts.
    Check("a listed bot in the world", HeadlessLeaderSteerable(true, true, "Leader", headless), 1);
    Check("a listed bot out of the world",
          HeadlessLeaderSteerable(false, true, "Leader", headless), 0);
    Check("an unlisted bot", HeadlessLeaderSteerable(true, true, "Stranger", headless), 0);
    Check("a near-miss spelling is unlisted",
          HeadlessLeaderSteerable(true, true, "leader", headless), 0);
    Check("a real session that lost its client, even when listed",
          HeadlessLeaderSteerable(true, false, "Leader", headless), 0);
    Check("an empty roster lists nobody", HeadlessLeaderSteerable(true, true, "Leader", {}), 0);

    // End to end through the #735 gate and loss hold, as DriveDungeonRunFor
    // feeds them: the seat decides `socketOpen`.
    bool const headlessSeat =
        DungeonLeaderHoldsTheSeat(false, HeadlessLeaderSteerable(true, true, "Leader", headless));
    Check("a headless leader settles first",
          static_cast<int>(DungeonLeaderClientGate(headlessSeat, 900, 1000)),
          static_cast<int>(LeaderClientGate::Settling));
    Check("a settled headless leader opens the door gate",
          static_cast<int>(DungeonLeaderClientGate(headlessSeat, 800, 1000)),
          static_cast<int>(LeaderClientGate::Open));
    Check("a headless leader with an issuer is not held",
          static_cast<int>(DungeonLeaderClientLoss(headlessSeat, true, 0, 1000)),
          static_cast<int>(LeaderClientLossAction::Continue));

    bool const droppedClient =
        DungeonLeaderHoldsTheSeat(false, HeadlessLeaderSteerable(true, false, "Leader", headless));
    Check("a dropped client still has no client",
          static_cast<int>(DungeonLeaderClientGate(droppedClient, 800, 1000)),
          static_cast<int>(LeaderClientGate::NoClient));
    Check("a dropped client is still held inside the bound",
          static_cast<int>(DungeonLeaderClientLoss(droppedClient, true, 1000, 1299)),
          static_cast<int>(LeaderClientLossAction::Hold));
    Check("a dropped client still ends client_lost after the bound",
          static_cast<int>(DungeonLeaderClientLoss(droppedClient, true, 1000, 1301)),
          static_cast<int>(LeaderClientLossAction::Abandon));

    bool const clientSeat = DungeonLeaderHoldsTheSeat(true, false);
    Check("a client leader opens exactly as before",
          static_cast<int>(DungeonLeaderClientGate(clientSeat, 800, 1000)),
          static_cast<int>(LeaderClientGate::Open));

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
