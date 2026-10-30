/*
 * The Teldrassil shore check reads a zone only near the island (#1224).
 *
 * The #1218 filter asked the map for the zone of every mailbox, vendor or
 * spawn candidate on map 1, whichever shore the walker stood on. Reading a
 * zone loads that point's grid (terrain, vmap and mmap tiles) on the world
 * thread, so one Kalimdor walk could load grids all over the continent. The
 * check now asks for the zone only when the destination lies in a box around
 * the island; a destination outside it is off the island without asking.
 *
 * Compiled against src/overseer_decisions.cpp. The last check reads
 * src/mod_overseer.cpp as text, from the repository root, so the walk filter
 * is pinned to the pre-checked form and cannot drift back to a bare read.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace D = OverseerDecisions;

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

constexpr std::uint32_t DARKSHORE = 148;

// Points on map 1 (x, y).
constexpr float DARNASSUS_X = 9947.0f, DARNASSUS_Y = 2482.0f;
constexpr float SHADOWGLEN_X = 10311.0f, SHADOWGLEN_Y = 832.0f;
constexpr float RUTTHERAN_X = 8640.0f, RUTTHERAN_Y = 841.0f;
constexpr float ORGRIMMAR_X = 1629.0f, ORGRIMMAR_Y = -4373.0f;
constexpr float GADGETZAN_X = -7177.0f, GADGETZAN_Y = -3785.0f;

// A zone reader that counts how often it was asked, as Map::GetZoneId would
// load a grid each time.
struct CountingZone
{
    std::uint32_t zone;
    int* reads;
    std::uint32_t operator()() const
    {
        ++*reads;
        return zone;
    }
};
}  // namespace

int main()
{
    // The box holds the island.
    Check("Darnassus is near Teldrassil", D::NearTeldrassil(1, DARNASSUS_X, DARNASSUS_Y));
    Check("Shadowglen is near Teldrassil", D::NearTeldrassil(1, SHADOWGLEN_X, SHADOWGLEN_Y));
    Check("the Rut'theran dock is near Teldrassil", D::NearTeldrassil(1, RUTTHERAN_X, RUTTHERAN_Y));

    // And none of the mainland.
    Check("Auberdine is not near Teldrassil",
          !D::NearTeldrassil(1, D::AUBERDINE_STOP_X, D::AUBERDINE_STOP_Y));
    Check("Orgrimmar is not near Teldrassil", !D::NearTeldrassil(1, ORGRIMMAR_X, ORGRIMMAR_Y));
    Check("Gadgetzan is not near Teldrassil", !D::NearTeldrassil(1, GADGETZAN_X, GADGETZAN_Y));
    Check("another map is never near Teldrassil", !D::NearTeldrassil(0, DARNASSUS_X, DARNASSUS_Y));

    // A mainland walk to a distant mainland candidate reads no zone: this is
    // the grid load the ticket is about.
    int reads = 0;
    Check("Darkshore to Gadgetzan does not cross the water",
          !D::AcrossTeldrassilShoreAt(1, DARKSHORE, GADGETZAN_X, GADGETZAN_Y,
                                      CountingZone{440, &reads}));
    Check("Darkshore to Orgrimmar does not cross the water",
          !D::AcrossTeldrassilShoreAt(1, DARKSHORE, ORGRIMMAR_X, ORGRIMMAR_Y,
                                      CountingZone{1637, &reads}));
    Check("a distant mainland candidate reads no zone", reads == 0);

    // An island walker to a distant candidate crosses the water, still with no read.
    Check("Teldrassil to Orgrimmar crosses the water",
          D::AcrossTeldrassilShoreAt(1, D::TELDRASSIL_ZONE, ORGRIMMAR_X, ORGRIMMAR_Y,
                                     CountingZone{1637, &reads}));
    Check("Teldrassil to Auberdine crosses the water",
          D::AcrossTeldrassilShoreAt(1, D::TELDRASSIL_ZONE, D::AUBERDINE_STOP_X,
                                     D::AUBERDINE_STOP_Y, CountingZone{DARKSHORE, &reads}));
    Check("a candidate off the island from it reads no zone", reads == 0);

    // A candidate near the island has its zone read, once, and decides as before.
    Check("Darkshore to Darnassus crosses the water",
          D::AcrossTeldrassilShoreAt(1, DARKSHORE, DARNASSUS_X, DARNASSUS_Y,
                                     CountingZone{D::DARNASSUS_ZONE, &reads}));
    Check("a candidate near the island reads its zone once", reads == 1);
    Check("Shadowglen to Darnassus stays on the island",
          !D::AcrossTeldrassilShoreAt(1, D::TELDRASSIL_ZONE, DARNASSUS_X, DARNASSUS_Y,
                                      CountingZone{D::DARNASSUS_ZONE, &reads}));
    Check("an unnamed point in the box from the island crosses the water",
          D::AcrossTeldrassilShoreAt(1, D::TELDRASSIL_ZONE, RUTTHERAN_X, RUTTHERAN_Y,
                                     CountingZone{0, &reads}));
    Check("each candidate near the island is read", reads == 3);

    // Another map neither crosses nor reads.
    reads = 0;
    Check("another map never crosses the island's shore",
          !D::AcrossTeldrassilShoreAt(0, D::TELDRASSIL_ZONE, DARNASSUS_X, DARNASSUS_Y,
                                      CountingZone{DARKSHORE, &reads}));
    Check("another map reads no zone", reads == 0);

    // The walk filter in the adapter goes through the pre-check.
    std::ifstream in("src/mod_overseer.cpp", std::ios::binary);
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Check("the adapter source was read", !text.empty());
    Check("the walk filter uses the pre-checked shore check",
          text.find("D::AcrossTeldrassilShoreAt(") != std::string::npos);
    Check("the walk filter no longer reads a zone for every candidate",
          text.find("D::AcrossTeldrassilShore(\n") == std::string::npos);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
