/*
 * A ghost whose corpse zone offers no graveyard it can stand at walks to one
 * beyond it (wow-overseer#1093).
 *
 * On the dev realm a level 20 Horde member died at Rut'theran Village,
 * Teldrassil, to level 75 Darnassus Sentinels 107 times from 2026-11-06 to
 * 2026-11-07. Every graveyard its corpse zone links was refused, so it waited
 * 1200 s as a ghost, took the refused healer beside the guards, and died again
 * about 11 s after it rose. The fixtures are the map-1 graveyards nearest that
 * corpse, with their distances from it as the realm's world database holds them.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::FarHealerConsidered;
using OverseerDecisions::OnOtherFactionsGround;
using OverseerDecisions::OtherFactionsHomeZone;

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

constexpr bool HORDE = false;
constexpr bool ALLIANCE = true;
constexpr float FAR = 3000.f;

// Zones: Teldrassil 141, Darkshore 148, Felwood 361, Durotar 14, the Barrens 17.
constexpr uint32_t TELDRASSIL = 141;
constexpr uint32_t DARKSHORE = 148;
constexpr uint32_t FELWOOD = 361;

}  // namespace

int main()
{
    // The Teldrassil graveyards a Horde ghost must not walk to: Dolanaar 995
    // yards, Aldrassil 1686, Darnassus 1773 (Darnassus city is zone 1657).
    Check(!FarHealerConsidered(HORDE, TELDRASSIL, 995.f, FAR),
          "Dolanaar, on Teldrassil, is the other faction's home ground for the Horde");
    Check(!FarHealerConsidered(HORDE, 1657, 1773.f, FAR),
          "Darnassus is the other faction's home ground for the Horde");
    // Auberdine, Darkshore, 2107 yards across the water: the way out.
    Check(FarHealerConsidered(HORDE, DARKSHORE, 2107.f, FAR),
          "Auberdine, Darkshore, is judged for a Horde ghost dead at Rut'theran");
    // Felwood, Irontree Woods, 3527 yards: past the walk this search allows.
    Check(!FarHealerConsidered(HORDE, FELWOOD, 3527.f, FAR),
          "a graveyard past the far walk is not judged");
    Check(FarHealerConsidered(HORDE, FELWOOD, 3000.f, FAR),
          "a graveyard at the far walk's edge is judged");
    Check(!FarHealerConsidered(HORDE, DARKSHORE, -1.f, FAR),
          "an unmeasured distance is not judged");

    // Every starting zone and capital of the other faction, both ways.
    for (uint32_t zone : {12u, 1519u, 1u, 1537u, 141u, 1657u, 3524u, 3557u})
    {
        Check(OtherFactionsHomeZone(HORDE, zone), "an Alliance home zone is hostile to the Horde");
        Check(!OtherFactionsHomeZone(ALLIANCE, zone), "an Alliance home zone is not hostile to the Alliance");
    }
    for (uint32_t zone : {14u, 1637u, 215u, 1638u, 85u, 1497u, 3430u, 3487u})
    {
        Check(OtherFactionsHomeZone(ALLIANCE, zone), "a Horde home zone is hostile to the Alliance");
        Check(!OtherFactionsHomeZone(HORDE, zone), "a Horde home zone is not hostile to the Horde");
    }
    Check(!OtherFactionsHomeZone(HORDE, DARKSHORE), "Darkshore is no faction's home ground");
    Check(!OtherFactionsHomeZone(ALLIANCE, 17), "the Barrens is no faction's home ground");
    Check(FarHealerConsidered(ALLIANCE, 17, 1200.f, FAR),
          "an Alliance ghost dead in Durotar may walk to a graveyard in the Barrens");
    Check(!FarHealerConsidered(ALLIANCE, 14, 600.f, FAR),
          "an Alliance ghost is not walked to Razor Hill");

    // The narrower list that decides the immediate healer and the bridge's
    // hearth is unchanged.
    Check(!OnOtherFactionsGround(HORDE, TELDRASSIL),
          "OnOtherFactionsGround still names only Elwynn and Stormwind for the Horde");
    Check(OnOtherFactionsGround(HORDE, 12) && OnOtherFactionsGround(ALLIANCE, 14),
          "OnOtherFactionsGround keeps Elwynn and Durotar");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
