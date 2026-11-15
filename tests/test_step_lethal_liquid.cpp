/*
 * The straight-line step never lands on lava or slime (#1569).
 *
 * THE FAULT. The module's footing check (GroundHolds) measures every stride of
 * a step with Map::GetWaterOrGroundLevel, which answers with the liquid's
 * SURFACE wherever there is liquid, so that a river reads as a floor to swim
 * on rather than as a fifteen yard drop to its bed. For water that is the
 * right answer. For magma and slime it is a lethal one: a lava pool read as a
 * perfectly level floor, every stride "held", and the step walked straight in.
 *
 * THE EVIDENCE (the Molten Core rehearsal on the disposable copy realm). The
 * route planner ended at the door node and the head died anyway, twice, both
 * `killer: self`, `liquid magma`, `fell 0.0 yards`, with the last movement
 * step `generator=point` (the module's own straight step, taken whenever the
 * navmesh and the survey have no route from where the head stands):
 *
 *   map 0, zone 25, (-7537.4, -1094.2, 163.8), 57 yards from the door aim
 *   map 0, zone 46, (-7581.7,  -860.1, 180.7)
 *
 * and 37 deaths among 16 seats in zones 25 and 46 in 30 minutes, all of them
 * the same way. The surveyed legs are not at fault: the playerbots survey's
 * walk legs are navmesh-walked, and the navmesh does not route over lava. The
 * stepper that takes over where the survey stops had no liquid test at all.
 *
 * THE RULE. A stride whose surface stands over the ground in magma or slime
 * is not ground. Water is untouched: swimming a river is still a step.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The second
 * half reads src/mod_overseer.cpp as text, because the adapter cannot be
 * compiled without a core: it pins that the one footing check every straight
 * step goes through asks this rule, and asks the core for the liquid TYPE at
 * the sampled point rather than guessing it from a height.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::DEATH_LIQUID_DARK_WATER;
using OverseerDecisions::DEATH_LIQUID_MAGMA;
using OverseerDecisions::DEATH_LIQUID_OCEAN;
using OverseerDecisions::DEATH_LIQUID_SLIME;
using OverseerDecisions::DEATH_LIQUID_WATER;
using OverseerDecisions::StepLiquidIsLethal;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

bool Has(std::string const& text, char const* needle, std::size_t from = 0)
{
    return text.find(needle, from) != std::string::npos;
}

}  // namespace

int main()
{
    // The two live deaths: magma standing over the ground at the sample.
    Check("lava over the ground is lethal", StepLiquidIsLethal(4.0f, DEATH_LIQUID_MAGMA), true);
    Check("a thin film of lava is lethal", StepLiquidIsLethal(0.3f, DEATH_LIQUID_MAGMA), true);
    Check("slime over the ground is lethal", StepLiquidIsLethal(2.0f, DEATH_LIQUID_SLIME), true);
    Check("a mixed mask with magma in it is lethal",
          StepLiquidIsLethal(2.0f, DEATH_LIQUID_WATER | DEATH_LIQUID_MAGMA), true);

    // Water is still a step, deep or shallow: the drowning rule (#503) owns it.
    Check("a river is not refused here", StepLiquidIsLethal(3.0f, DEATH_LIQUID_WATER), false);
    Check("the ocean is not refused here", StepLiquidIsLethal(40.0f, DEATH_LIQUID_OCEAN), false);
    Check("dark water is not refused here",
          StepLiquidIsLethal(3.0f, DEATH_LIQUID_DARK_WATER), false);

    // No liquid over the ground: a magma bit with nothing standing over the
    // floor (a liquid layer below the ground, as under a bridge) is a floor.
    Check("lava below the floor is a floor", StepLiquidIsLethal(0.0f, DEATH_LIQUID_MAGMA), false);
    Check("negative depth is a floor", StepLiquidIsLethal(-5.0f, DEATH_LIQUID_MAGMA), false);
    Check("no liquid bits is a floor", StepLiquidIsLethal(3.0f, 0), false);

    // The adapter has to ask the rule, from the one footing check.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const holds = text.find("static bool GroundHolds(Player* bot, float toX");
    std::size_t const holdsEnd = text.find("static bool NothingInTheWay(", holds);
    if (holds == std::string::npos || holdsEnd == std::string::npos)
    {
        std::printf("FAIL GroundHolds is not where the test expects it\n");
        return EXIT_FAILURE;
    }
    std::string const body = text.substr(holds, holdsEnd - holds);
    Check("GroundHolds refuses a stride that lands in lethal liquid",
          Has(body, "LethalLiquidAt(bot,"), true);

    std::size_t const helper = text.find("static bool LethalLiquidAt(");
    if (helper == std::string::npos)
    {
        std::printf("FAIL the adapter has no LethalLiquidAt\n");
        return EXIT_FAILURE;
    }
    std::string const helperBody = text.substr(helper, 2600);
    Check("the liquid is measured with the core's own depth query",
          Has(helperBody, "GetWaterOrGroundLevel("), true);
    Check("the liquid TYPE is read from the core, not guessed from a height",
          Has(helperBody, "GetLiquidData("), true);
    Check("the verdict is the pure rule's",
          Has(helperBody, "OverseerDecisions::StepLiquidIsLethal("), true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
