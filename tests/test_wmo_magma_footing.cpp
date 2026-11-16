/*
 * The footing check sees world-model (WMO) magma whose bed is out of reach
 * (#1573, #1557).
 *
 * THE FAULT. SurfaceAt asks the core for the liquid TYPE only when it has also
 * been handed the ground under the liquid. Map::GetWaterOrGroundLevel computes
 * that ground with Map::GetHeight, which looks DOWN a bounded distance
 * (maxSearchDist = 50 yards, Map.cpp:1140-1143 at the pinned revision). Over a
 * cavern lake whose bed lies further down than that, the ground comes back as
 * INVALID_HEIGHT while the liquid level still comes back (the WMO liquid is
 * found by its own query, Map.cpp:1336-1365), so the call returns the magma's
 * SURFACE as a floor and the ground as "nothing". The old test
 * `ground > INVALID_HEIGHT` then skipped the type lookup, `lethal` stayed
 * false, and the stride passed as a perfectly level floor.
 *
 * THE RULE. A liquid surface with no measurable bed under it is the deepest
 * liquid there is, not a dry one. The type is asked of the core just under the
 * surface, with MAP_ALL_LIQUIDS, and the verdict is the same pure rule.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The second
 * half reads src/mod_overseer.cpp as text, as the sibling tests do, because the
 * adapter cannot be compiled without a core.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::DEATH_LIQUID_MAGMA;
using OverseerDecisions::DEATH_LIQUID_WATER;
using OverseerDecisions::LethalLiquidProbe;
using OverseerDecisions::LiquidProbePlan;
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

// The core's own GetHeight reach (Map.cpp:1140, `GetHeight(..., 50.0f)`), and
// the offset it adds to the start (Z_OFFSET_FIND_HEIGHT).
constexpr float SEARCH_YARDS = 50.f;
constexpr float START_OFFSET = 0.5f;
constexpr float TRAVEL_GROUND_DROP_YARDS = 10.f;

// What Map::GetWaterOrGroundLevel hands SurfaceAt over a lake, modelled from
// its source: the ground is the bed when the bed is within SEARCH_YARDS of the
// start, otherwise nothing; the surface is the liquid level either way.
struct Reading
{
    float surface;
    float ground;
    bool groundKnown;
};

Reading ReadLake(float footing, float lavaLevel, float bed)
{
    float const from = footing + TRAVEL_GROUND_DROP_YARDS + START_OFFSET;
    bool const reach = bed <= from && from - bed <= SEARCH_YARDS;
    return {lavaLevel, reach ? bed : 0.f, reach};
}

// What SurfaceAt now does with that reading: plan the probe, and when the plan
// asks, take the core's answer (here, a WMO magma liquid standing at the level
// over the sample) through the same pure rule.
bool Lethal(Reading const& r)
{
    LiquidProbePlan const plan = LethalLiquidProbe(r.surface, r.ground, r.groundKnown);
    return plan.ask && StepLiquidIsLethal(plan.depthYards, DEATH_LIQUID_MAGMA);
}

}  // namespace

int main()
{
    // The roll 73 approach: the walkway stands near z 181.9, the cavern's magma
    // near z 169. Whatever the bed is, the stride is magma.
    float const walkway = 181.9f;
    float const magma = 169.0f;
    Check("magma over a bed within reach is lethal", Lethal(ReadLake(walkway, magma, 160.f)), true);
    Check("magma over a bed just inside reach is lethal",
          Lethal(ReadLake(walkway, magma, walkway + 10.5f - 49.f)), true);
    Check("magma over a bed OUT of reach is lethal (the roll 73 blind spot)",
          Lethal(ReadLake(walkway, magma, 90.f)), true);
    Check("the modelled deep bed really is out of the core's reach",
          ReadLake(walkway, magma, 90.f).groundKnown, false);

    // No liquid over the ground: a floor, as before.
    Check("a floor at the liquid's level is not asked about",
          LethalLiquidProbe(160.f, 160.f, true).ask, false);
    Check("a floor above the liquid is not asked about",
          LethalLiquidProbe(160.f, 165.f, true).ask, false);

    // Where it asks, it asks inside the liquid.
    LiquidProbePlan const shallow = LethalLiquidProbe(169.f, 165.f, true);
    Check("a known bed is asked at the midpoint", shallow.ask && shallow.z == 167.f, true);
    LiquidProbePlan const deep = LethalLiquidProbe(169.f, 0.f, false);
    Check("an unknown bed is asked just under the surface",
          deep.ask && deep.z < 169.f && deep.z > 168.f, true);
    Check("an unknown bed counts as deep liquid", deep.depthYards > 100.f, true);

    // Water is still not refused here, deep bed or not.
    Check("deep water over an unmeasurable bed is not refused",
          StepLiquidIsLethal(LethalLiquidProbe(169.f, 0.f, false).depthYards,
                             DEATH_LIQUID_WATER),
          false);

    // The adapter has to use the plan, and not gate the lookup on the ground.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::size_t const helper = text.find("static bool SurfaceAt(");
    std::size_t const helperEnd = text.find("static bool WaterDepthAt(", helper);
    if (helper == std::string::npos || helperEnd == std::string::npos)
    {
        std::printf("FAIL SurfaceAt is not where the test expects it\n");
        return EXIT_FAILURE;
    }
    std::string const body = text.substr(helper, helperEnd - helper);
    Check("SurfaceAt plans the probe through the pure rule",
          Has(body, "OverseerDecisions::LethalLiquidProbe("), true);
    Check("SurfaceAt asks the core for every liquid type, WMO included",
          Has(body, "MAP_ALL_LIQUIDS"), true);
    Check("SurfaceAt no longer skips the lookup when the bed is unmeasurable",
          Has(body, "lethal && ground > INVALID_HEIGHT"), false);
    Check("SurfaceAt passes the known-bed flag, not a guess from a height",
          Has(body, "groundKnown"), true);
    Check("SurfaceAt still reads the type from the core",
          Has(body, "GetLiquidData("), true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
