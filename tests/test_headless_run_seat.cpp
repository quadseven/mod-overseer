/*
 * A headless roster leader holds a dungeon run's leading seat.
 *
 * MEASURED ON THE DEV REALM, 2026-09-29. Both families are on
 * Overseer.HeadlessRoster and nobody was watching, so neither head had a
 * client. Grug's family went into The Stockade through the dungeon finder and
 * the worldserver said, in this order:
 *
 *     'Grug' is inside map 34 but no groupmate may issue a dungeon-clear
 *         command - ... no client-attached member of the party is a selfbot
 *         or a person, so the dungeon brain stays OFF
 *     dungeon run for 'Grug' is held because the leader's client
 *         disconnected; members inside stay put for up to 300 seconds
 *     dungeon run 207050 ended 'client_lost' - the leader client remained
 *         disconnected while members were inside
 *
 * and then reset, queued again, and did it again. A family that plays headless
 * on purpose (#549) could never clear anything: the run demanded a socket the
 * character was configured never to have. What #735 protects is a run whose
 * leader's client DROPS around the door; a listed headless bot in the world
 * does not drop, and is steerable by the same rule every other drive uses.
 *
 * Compiles against the pure decision file alone.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

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
    std::vector<std::string> const listed = HeadlessRosterNames("Grug,Zug");

    // The seat is held by a client socket, as it always was.
    Check("an open client socket holds the seat", DungeonLeaderHoldsTheSeat(true, false), 1);
    // A listed headless bot in the world holds it too: this is the defect.
    Check("a listed headless bot in the world holds the seat",
          DungeonLeaderHoldsTheSeat(
              false, RosterCharacterIsSteerable(false, true, true, "Grug", true, listed)),
          1);
    // Nothing else does: a character out of the world, an unlisted bot (the
    // sweep is about to evict it), a real session that just lost its client.
    Check("a listed bot that is not in the world does not",
          DungeonLeaderHoldsTheSeat(
              false, RosterCharacterIsSteerable(false, false, true, "Grug", true, listed)),
          0);
    Check("an unlisted bot does not",
          DungeonLeaderHoldsTheSeat(
              false, RosterCharacterIsSteerable(false, true, true, "Bork", true, listed)),
          0);
    Check("a real session that lost its client does not",
          DungeonLeaderHoldsTheSeat(
              false, RosterCharacterIsSteerable(false, true, false, "Grug", true, listed)),
          0);
    Check("no socket and nothing headless does not", DungeonLeaderHoldsTheSeat(false, false), 0);

    // And the gate that reads it: a seated headless leader settles and opens
    // the door like a client, and one that is not seated still blocks.
    Check("a seated headless leader opens after the settle window",
          static_cast<int>(DungeonLeaderClientGate(DungeonLeaderHoldsTheSeat(false, true), 800, 1000)),
          static_cast<int>(LeaderClientGate::Open));
    Check("an unseated leader still blocks the door",
          static_cast<int>(DungeonLeaderClientGate(DungeonLeaderHoldsTheSeat(false, false), 800, 1000)),
          static_cast<int>(LeaderClientGate::NoClient));

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
