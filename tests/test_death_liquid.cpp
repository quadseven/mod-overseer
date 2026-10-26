#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main()
{
    using OverseerDecisions::ClassifyDeathLiquid;
    using OverseerDecisions::DeathLiquid;
    using OverseerDecisions::DeathLiquidName;
    using OverseerDecisions::FallAccount;
    using OverseerDecisions::ReadSelfDeath;
    using OverseerDecisions::SelfDeathReading;
    bool const magmaFixture = ClassifyDeathLiquid(4, 1, 4, 8) == DeathLiquid::Magma &&
                              std::strcmp(DeathLiquidName(DeathLiquid::Magma), "magma") == 0;
    bool const waterIsDistinct = ClassifyDeathLiquid(1, 1, 4, 8) == DeathLiquid::Water &&
                                 std::strcmp(DeathLiquidName(DeathLiquid::Water), "water") == 0;
    bool const slimeIsDistinct = ClassifyDeathLiquid(8, 1, 4, 8) == DeathLiquid::Slime &&
                                 std::strcmp(DeathLiquidName(DeathLiquid::Slime), "slime") == 0;
    bool const unknownIsNotWater = ClassifyDeathLiquid(0, 1, 4, 8) == DeathLiquid::Unknown &&
                                   std::strcmp(DeathLiquidName(DeathLiquid::Unknown), "unknown") == 0;
    // A zone 46 magma point uses the core's magma flag, not the water bit.
    bool const magmaIsNotDrowning =
        ReadSelfDeath({32, 0, true, FallAccount::NoDrop, DeathLiquid::Magma}, {2}) !=
        SelfDeathReading::Drowning;
    bool const waterStillReadsAsDrowning =
        ReadSelfDeath({32, 0, true, FallAccount::NoDrop, DeathLiquid::Water}, {2}) ==
        SelfDeathReading::Drowning;
    if (!magmaFixture || !waterIsDistinct || !slimeIsDistinct || !unknownIsNotWater ||
        !magmaIsNotDrowning || !waterStillReadsAsDrowning)
    {
        std::printf("FAIL death liquid classification\n");
        return EXIT_FAILURE;
    }
    std::printf("test_death_liquid: water, magma, slime and unknown remain distinct\n");
    return EXIT_SUCCESS;
}
