/*
 * A natural guild member walks to its class trainer and buys what it can
 * afford.
 *
 * Measured on wow-dev (2026-09-27): natural guild members at levels 10 to 16
 * knew 4 to 7 spells, where a class trainer sells 12 to 15 by level 12, and
 * 92 of them died 603 times in 45 minutes to hostiles 0 to 3 levels above
 * them. Nothing granted them spells (patch 0024) and nothing sent them to a
 * trainer.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::DecideGuildTraining;
using OverseerDecisions::GuildTrainerSpotUsable;
using OverseerDecisions::GuildTrainingStep;
using OverseerDecisions::NearestTrainerSpot;
using OverseerDecisions::TrainerSpot;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    // Westfall: Sentinel Hill has no mage trainer; Goldshire and Stormwind do.
    std::vector<TrainerSpot> const spots = {
        {0, -9471.f, 33.f},     // Goldshire
        {0, -9010.f, 870.f},    // Stormwind
        {1, 10000.f, 2300.f},   // Darnassus
    };
    Check(NearestTrainerSpot(spots, 0, -10500.f, 1100.f, 3000.f) == 0,
          "the nearest trainer on its map is chosen");
    Check(NearestTrainerSpot(spots, 1, 0.f, 0.f, 3000.f) == -1,
          "a trainer beyond the walk limit is not chosen");
    Check(NearestTrainerSpot(spots, 530, 0.f, 0.f, 3000.f) == -1,
          "no trainer on its map, none chosen");

    Check(DecideGuildTraining(0, true, 10.f, 5.f) == GuildTrainingStep::Nothing,
          "nothing it can afford: it goes nowhere");
    Check(DecideGuildTraining(3, false, 0.f, 5.f) == GuildTrainingStep::Nothing,
          "no trainer: it goes nowhere");
    Check(DecideGuildTraining(3, true, 800.f, 5.f) == GuildTrainingStep::Walk,
          "affordable spells and a trainer away: it walks");
    Check(DecideGuildTraining(3, true, 4.f, 5.f) == GuildTrainingStep::Learn,
          "at the trainer: it learns");

    // A friendly-faction trainer is not a usable trainer by that alone. An
    // event trainer (faction 35, friendly to both sides) stands beside the
    // other side's town guards, and a level 37 Horde warlock walked to one in
    // Dun Morogh and died to a city guard 14 times, 15 minutes apart.
    Check(GuildTrainerSpotUsable(true, 0), "an in-world spawn with no hostile guard near is usable");
    Check(!GuildTrainerSpotUsable(false, 0),
          "a spawn tied to a game event that is not running is nobody to walk to");
    Check(!GuildTrainerSpotUsable(true, 1),
          "a spawn the other side's guard stands beside is not usable, however friendly it is");
    Check(!GuildTrainerSpotUsable(true, 6), "several guards: not usable");
    Check(!GuildTrainerSpotUsable(false, 3), "out of season and guarded: not usable");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild training: all checks passed\n");
    return 0;
}
