/*
 * A lone crafter is not left idle in a zone that keeps killing it (#1208).
 *
 * On the dev realm, in one day, one family member on job 'craft' died 13 times
 * alone in one zone, in combat with nothing moving it, and was left standing
 * there after every revival. The rule pinned here: two deaths alone on job
 * 'craft' in one zone within an hour and the crafter hearths to its bind, or,
 * while it cannot, its craft step is deferred. One death does not trip it, and
 * neither do deaths while grouped, which are the family's to answer.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::CRAFT_LONE_DEATH_LIMITS;
using OverseerDecisions::CraftLoneDeath;
using OverseerDecisions::CraftLoneDeathFacts;
using OverseerDecisions::CraftLoneDeathsInZone;
using OverseerDecisions::CraftLoneDeathStep;
using OverseerDecisions::DecideCraftLoneDeaths;
using OverseerDecisions::PruneCraftLoneDeaths;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr int64_t NOW = 1800000000;
constexpr uint32_t MAP = 0;
constexpr uint32_t ZONE = 33;

CraftLoneDeath Death(int64_t ago, bool grouped = false, uint32_t zone = ZONE, bool craft = true)
{
    CraftLoneDeath d;
    d.at = NOW - ago;
    d.mapId = MAP;
    d.zoneId = zone;
    d.craftJob = craft;
    d.grouped = grouped;
    return d;
}

// The step for a crafter standing in ZONE with these deaths on record.
CraftLoneDeathStep StepFor(std::vector<CraftLoneDeath> const& deaths, bool hearthReady = true,
                           bool grouped = false, bool resting = false, bool alive = true,
                           std::string const& job = "craft")
{
    CraftLoneDeathFacts facts;
    facts.job = job;
    facts.grouped = grouped;
    facts.alive = alive;
    facts.resting = resting;
    facts.hearthReady = hearthReady;
    facts.deathsHere = CraftLoneDeathsInZone(deaths, MAP, ZONE, NOW);
    return DecideCraftLoneDeaths(facts);
}

} // namespace

int main()
{
    std::vector<CraftLoneDeath> const two = {Death(50 * 60), Death(5 * 60)};

    Check("the limits are two deaths in an hour",
          CRAFT_LONE_DEATH_LIMITS.deaths == 2 && CRAFT_LONE_DEATH_LIMITS.windowSeconds == 3600);

    // Two solo deaths in one zone on the craft job: moved, or deferred.
    Check("two lone craft deaths in one zone count two", CraftLoneDeathsInZone(two, MAP, ZONE, NOW) == 2);
    Check("two lone craft deaths with a stone ready hearth", StepFor(two) == CraftLoneDeathStep::Hearth);
    Check("two lone craft deaths with no stone ready defer the craft",
          StepFor(two, false) == CraftLoneDeathStep::Defer);
    Check("two lone craft deaths while dead defer the craft",
          StepFor(two, true, false, false, false) == CraftLoneDeathStep::Defer);

    // One death does not.
    Check("one lone craft death stays", StepFor({Death(5 * 60)}) == CraftLoneDeathStep::Stay);

    // Deaths while grouped do not.
    std::vector<CraftLoneDeath> const groupedDeaths = {Death(50 * 60, true), Death(5 * 60, true)};
    Check("grouped deaths are not counted", CraftLoneDeathsInZone(groupedDeaths, MAP, ZONE, NOW) == 0);
    Check("two grouped craft deaths stay", StepFor(groupedDeaths) == CraftLoneDeathStep::Stay);
    Check("one grouped and one lone death stay",
          StepFor({Death(50 * 60, true), Death(5 * 60)}) == CraftLoneDeathStep::Stay);
    Check("a crafter grouped now is the family's, not this rule's",
          StepFor(two, true, true) == CraftLoneDeathStep::Stay);

    // One zone, one hour, one job.
    Check("deaths in two zones do not add up",
          StepFor({Death(50 * 60, false, 17), Death(5 * 60)}) == CraftLoneDeathStep::Stay);
    Check("a death older than the hour does not count",
          StepFor({Death(61 * 60), Death(5 * 60)}) == CraftLoneDeathStep::Stay);
    Check("deaths on another job do not count",
          StepFor({Death(50 * 60, false, ZONE, false), Death(5 * 60)}) == CraftLoneDeathStep::Stay);
    Check("a character no longer on job craft stays",
          StepFor(two, true, false, false, true, "quest") == CraftLoneDeathStep::Stay);

    // Already somewhere safe: an inn or a city.
    Check("a resting crafter stays", StepFor(two, true, false, true) == CraftLoneDeathStep::Stay);

    // The record is pruned to the window.
    std::vector<CraftLoneDeath> record = {Death(2 * 3600), Death(61 * 60), Death(5 * 60)};
    PruneCraftLoneDeaths(record, NOW);
    Check("pruning keeps only the deaths inside the hour", record.size() == 1 && record[0].at == NOW - 5 * 60);

    Check("every step has a reason word",
          std::string(OverseerDecisions::CraftLoneDeathStepWord(CraftLoneDeathStep::Hearth)) !=
                  OverseerDecisions::CraftLoneDeathStepWord(CraftLoneDeathStep::Defer));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok - a lone crafter is not left idle in a zone that keeps killing it\n");
    return 0;
}
