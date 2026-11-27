// The quest picker and travel column both consume the same cooldown (#1711).
#include "overseer_decisions.h"
#include "overseer_movement_ownership.h"

#include <cstdio>
#include <cstdlib>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
}

int main()
{
    constexpr uint32_t leaderMap = 0;
    constexpr uint32_t spawnMap = 1;
    constexpr uint32_t remoteQuest = 41;
    constexpr uint32_t localQuest = 42;
    constexpr int64_t firstPoll = 1000;
    constexpr int64_t handbackGrace = 15;
    int64_t const cooldown = ErrandDeathLimits{}.cooloffSeconds;
    QuestTravelCooldown refusals;
    OverseerMovement::InMemoryAimColumn column;
    OverseerMovement::MovementOwnership movement(column, handbackGrace);
    movement.SetLeaders({"Leader"});
    column.columns["Leader"] = "12345";

    Check("no refusal before trying the first errand",
          !refusals.BlocksAim(column.Read("Leader"), leaderMap, firstPoll));
    movement.AskForLeader("Leader", LeaderIntentKind::TownErrand, "travel column",
                          "12345", "walk to the quest target", firstPoll);
    Check("the first errand takes the leader",
          movement.CurrentLeaderIntent("Leader", firstPoll) == LeaderIntentKind::TownErrand);
    refusals.Record("12345", leaderMap, {remoteQuest}, firstPoll, cooldown);
    auto const released = movement.ReleaseIfCurrent("Leader", "12345", firstPoll);
    Check("release clears the failed errand", released.endedAim == "12345" &&
          column.Read("Leader").empty());
    Check("release ends the failed travel intent", movement.EndColumnLeaderIntent(
          "Leader", "spawn on another map", firstPoll).ended);
    column.columns["Leader"] = "23456";
    Check("a concurrent rewrite survives release of the old aim",
          movement.ReleaseIfCurrent("Leader", "12345", firstPoll).leftNewerAim &&
          column.Read("Leader") == "23456");
    Check("the refusal names both maps",
          UnresolvedSpawnRefusal(leaderMap, spawnMap) ==
          "the spawn is on map 1 and there is no route from map 0");

    int remoteDispatches = 1;  // The first failed resolution.
    int localPicks = 0;
    for (int64_t now = firstPoll + 5; now < firstPoll + cooldown; now += 5)
    {
        // Both the same column being written and the same quest being offered
        // happen independently on every poll.
        column.columns["Leader"] = "12345";
        if (!refusals.BlocksAim(column.Read("Leader"), leaderMap, now))
            ++remoteDispatches;
        if (!movement.WithinHandbackGrace("Leader", now) &&
            refusals.BlocksQuest(remoteQuest, leaderMap, now) &&
            !refusals.BlocksQuest(localQuest, leaderMap, now) &&
            !refusals.BlocksAim("23456", leaderMap, now))
        {
            movement.AskForLeader("Leader", LeaderIntentKind::QuestDrive, "quest drive",
                                  "", "work the local quest", now);
            Check("the quest drive takes the leader after hand-back",
                  movement.CurrentLeaderIntent("Leader", now) == LeaderIntentKind::QuestDrive);
            ++localPicks;
        }
    }
    Check("the errand is dispatched once per bound", remoteDispatches == 1);
    Check("every retry after hand-back can choose the local quest",
          localPicks > 0 && localPicks == (cooldown - handbackGrace + 4) / 5);

    refusals.Record("34567", leaderMap, {43}, firstPoll + 20, cooldown);
    Check("a different failed errand keeps both cooldowns",
          refusals.BlocksQuest(remoteQuest, leaderMap, firstPoll + 20) &&
          refusals.BlocksQuest(43, leaderMap, firstPoll + 20));
    refusals.Record("12345", leaderMap, {remoteQuest}, firstPoll + 30, cooldown);
    Check("a rewrite does not extend the retry bound",
          !refusals.BlocksAim("12345", leaderMap, firstPoll + cooldown) &&
          !refusals.BlocksQuest(remoteQuest, leaderMap, firstPoll + cooldown));
    Check("an independent map change makes the remote quest and aim eligible",
          !refusals.BlocksAim("12345", spawnMap, firstPoll + 5) &&
          !refusals.BlocksQuest(remoteQuest, spawnMap, firstPoll + 5));

    // A target the same-map resolver offers remains walkable through the
    // existing travel-candidate selection.
    TravelTargetCandidate localSpawn;
    localSpawn.entry = 23456;
    localSpawn.distance = 50.f;
    localSpawn.mayInteract = true;
    Check("the same-map spawn still wins the normal selection",
          ChooseTravelTarget({localSpawn}).verdict == TravelTargetVerdict::Chosen);
    Check("non-numeric and overflowed targets cannot name a spawn",
          NumericTravelTarget("auctioneer") == 0 && NumericTravelTarget("at:0:1,2,3") == 0 &&
          NumericTravelTarget("4294967296") == 0 && NumericTravelTarget("12345") == 12345);

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
