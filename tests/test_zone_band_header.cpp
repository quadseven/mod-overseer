/*
 * The band table the module compiles in (#1518).
 *
 * src/overseer_zone_band.h is projected from data/zone_band.json by
 * tools/zone-band/band.py, and tests/test_zone_band.py fails when the two
 * disagree. This file proves the other half: the header compiles on its own,
 * its lookup answers for a zone and a side, and its answer is what the band
 * test of the roam rule reads.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"
#include "overseer_zone_band.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}
}  // namespace

int main()
{
    using namespace OverseerDecisions;

    // --- the table's shape ------------------------------------------------------
    std::size_t const rows = sizeof(ZONE_BANDS) / sizeof(ZONE_BANDS[0]);
    Check(rows > 30, "the table holds the zones of the world");
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    bool everyRowHasABand = true;
    bool everyTopIsALevel = true;
    for (ZoneBandRow const& row : ZONE_BANDS)
    {
        Check(seen.insert({row.mapId, row.zoneId}).second, "a zone is listed once");
        everyRowHasABand = everyRowHasABand && (row.allianceTop != 0 || row.hordeTop != 0);
        everyTopIsALevel = everyTopIsALevel && row.allianceTop <= 83 && row.hordeTop <= 83;
    }
    Check(everyRowHasABand || rows == 0, "a listed zone has a band for at least one side");
    Check(everyTopIsALevel, "no band top is above the highest real level");

    // --- the lookup -------------------------------------------------------------
    // Elwynn Forest (map 0, zone 12) and Durotar (map 1, zone 14): each side's
    // own starting zone tops out low for it and high for the other side, whose
    // band there counts the guards.
    ZoneBandTop const elwynnAlliance = ZoneBandTopFor(0, 12, false);
    ZoneBandTop const elwynnHorde = ZoneBandTopFor(0, 12, true);
    Check(elwynnAlliance.hasBand && elwynnAlliance.top > 0, "Elwynn has an Alliance band");
    Check(elwynnHorde.hasBand && elwynnHorde.top > elwynnAlliance.top,
          "and its Horde band tops out higher: the guards are hostile to the Horde");
    ZoneBandTop const durotarHorde = ZoneBandTopFor(1, 14, true);
    ZoneBandTop const durotarAlliance = ZoneBandTopFor(1, 14, false);
    Check(durotarHorde.hasBand && durotarAlliance.hasBand &&
              durotarAlliance.top > durotarHorde.top,
          "Durotar is the same the other way round");

    ZoneBandTop const nowhere = ZoneBandTopFor(0, 999999, false);
    Check(!nowhere.hasBand && nowhere.top == 0, "a zone the table does not hold has no band");
    Check(!ZoneBandTopFor(999999, 12, false).hasBand, "the map is part of the key");

    // --- and it is what the band test reads --------------------------------------
    Check(ZoneBandAdmits(elwynnAlliance.hasBand, elwynnAlliance.top, elwynnAlliance.top),
          "a member at the band top is admitted");
    Check(ZoneBandAdmits(elwynnAlliance.hasBand, elwynnAlliance.top, elwynnAlliance.top - 1),
          "and one a level below it");
    Check(elwynnAlliance.top < 3 ||
              !ZoneBandAdmits(elwynnAlliance.hasBand, elwynnAlliance.top,
                              elwynnAlliance.top - 2),
          "but not one two levels below it");
    Check(!ZoneBandAdmits(elwynnHorde.hasBand, elwynnHorde.top, elwynnAlliance.top),
          "a Horde member of the level Elwynn admits for the Alliance is refused there");
    Check(!ZoneBandAdmits(nowhere.hasBand, nowhere.top, 80),
          "a zone with no band admits nobody, whatever the level");

    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_zone_band_header: ok\n";
    return EXIT_SUCCESS;
}
