/*
 * A same-map walk does not cross Teldrassil's shore (#1206).
 *
 * Teldrassil and the Kalimdor mainland are both map 1, so a guild far walk,
 * spawn walk or vendor walk between them never becomes a cross-to-map row and
 * never takes the Rut'theran boat. Measured on the dev realm after #1133
 * shipped: about twenty self deaths a day below the water line off the
 * island's west coast, every one a walk that headed straight at a mainland
 * destination. The walk now leaves out a destination across the water, and
 * holds with a reason when none is left on its own shore.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
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
constexpr std::uint32_t ASHENVALE = 331;
}  // namespace

int main()
{
    // Island to mainland: the vendor walk out of Teldrassil to a Darkshore vendor.
    Check("Teldrassil to Darkshore crosses the water",
          D::AcrossTeldrassilShore(1, D::TELDRASSIL_ZONE, DARKSHORE));
    Check("Darnassus to Ashenvale crosses the water",
          D::AcrossTeldrassilShore(1, D::DARNASSUS_ZONE, ASHENVALE));

    // Mainland to island.
    Check("Darkshore to Teldrassil crosses the water",
          D::AcrossTeldrassilShore(1, DARKSHORE, D::TELDRASSIL_ZONE));
    Check("Ashenvale to Darnassus crosses the water",
          D::AcrossTeldrassilShore(1, ASHENVALE, D::DARNASSUS_ZONE));

    // One shore.
    Check("Teldrassil to Darnassus stays on the island",
          !D::AcrossTeldrassilShore(1, D::TELDRASSIL_ZONE, D::DARNASSUS_ZONE));
    Check("Darnassus to Teldrassil stays on the island",
          !D::AcrossTeldrassilShore(1, D::DARNASSUS_ZONE, D::TELDRASSIL_ZONE));
    Check("Teldrassil to Teldrassil stays on the island",
          !D::AcrossTeldrassilShore(1, D::TELDRASSIL_ZONE, D::TELDRASSIL_ZONE));
    Check("Darkshore to Ashenvale stays on the mainland",
          !D::AcrossTeldrassilShore(1, DARKSHORE, ASHENVALE));

    // A destination the map could not name is off the island.
    Check("an unnamed destination from the island crosses the water",
          D::AcrossTeldrassilShore(1, D::TELDRASSIL_ZONE, 0));
    Check("an unnamed destination from the mainland does not",
          !D::AcrossTeldrassilShore(1, DARKSHORE, 0));

    // Zone 141 on another map is not the island.
    Check("another map never crosses the island's shore",
          !D::AcrossTeldrassilShore(0, D::TELDRASSIL_ZONE, DARKSHORE));

    // The hold says why, the same words for every kind of walk, and is worth
    // asking again: the member may take the boat on a cross-to-map row.
    namespace R = D::MailWalkRefusal;
    Check("the hold has a reason", *R::AcrossTheWater != '\0');
    Check("the reason names the boat",
          std::string(R::AcrossTheWater).find("Rut'theran boat") != std::string::npos);
    Check("a vendor walk keeps the reason",
          std::string(D::WalkRefusalFor(D::WalkGoal::Vendor, R::AcrossTheWater)) ==
              R::AcrossTheWater);
    Check("a spawn walk keeps the reason",
          std::string(D::WalkRefusalFor(D::WalkGoal::Spawn, R::AcrossTheWater)) ==
              R::AcrossTheWater);
    Check("the mailbox walk's hold is retryable", D::MailWalkRefusalRetryable(R::AcrossTheWater));
    Check("an errand walk's hold is retryable", D::ErrandWalkRefusalRetryable(R::AcrossTheWater));
    Check("the hold does not clear by itself", !D::WalkRefusalTransient(R::AcrossTheWater));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
