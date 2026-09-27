/*
 * A crossing member whose inn is far nearer the berth hearths before it walks
 * (2026-09-27).
 *
 * Measured on wow-dev: the Alliance leader stood at the Darnassus auctioneer on
 * Teldrassil when the campaign sent him to the Theramore berth for 'Ship (The
 * Lady Mehley)'. Every walk ended 14,664 yards short and every bearing was
 * refused, because no leg of a family's route may be a boat. His stone was bound
 * at Ratchet.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>

using OverseerDecisions::CROSSING_HEARTH_SAVES_YARDS;
using OverseerDecisions::CrossingMemberHearths;
using OverseerDecisions::HomeBind;

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

constexpr float BERTH_X = -4010.7f;  // Theramore, 'Ship (The Lady Mehley)'
constexpr float BERTH_Y = -4727.5f;

HomeBind Ratchet()
{
    HomeBind bind;
    bind.known = true;
    bind.mapId = 1;
    bind.areaId = 392;
    bind.x = -1046.f;
    bind.y = -3665.f;
    bind.z = 6.f;
    return bind;
}

float ToBerth(float x, float y)
{
    return std::hypot(x - BERTH_X, y - BERTH_Y);
}
}  // namespace

int main()
{
    HomeBind const inn = Ratchet();

    // Grug at the Darnassus auctioneer, 03:36 UTC.
    Check("a leader on Teldrassil bound at Ratchet hearths",
          CrossingMemberHearths(true, false, inn, 1, ToBerth(9850.f, 2307.f), BERTH_X, BERTH_Y));
    // Grog in Darkshore, following him north.
    Check("a member in Darkshore bound at Ratchet hearths",
          CrossingMemberHearths(true, false, inn, 1, ToBerth(6658.f, 470.f), BERTH_X, BERTH_Y));

    // Og in Dustwallow, already nearer the berth than the inn is.
    Check("a member near the berth walks",
          !CrossingMemberHearths(true, false, inn, 1, ToBerth(-3081.f, -3883.f), BERTH_X,
                                 BERTH_Y));
    // Bork in the Barrens: the stone would save under the margin.
    Check("a member the stone saves less than the margin walks",
          !CrossingMemberHearths(true, false, inn, 1, ToBerth(-179.f, -3247.f), BERTH_X,
                                 BERTH_Y));

    Check("a member already aboard never hearths",
          !CrossingMemberHearths(true, true, inn, 1, ToBerth(9850.f, 2307.f), BERTH_X, BERTH_Y));
    Check("a member not on the crossing's map never hearths",
          !CrossingMemberHearths(false, false, inn, 1, ToBerth(9850.f, 2307.f), BERTH_X,
                                 BERTH_Y));

    HomeBind other = inn;
    other.mapId = 0;
    Check("a stone bound on another continent never hearths",
          !CrossingMemberHearths(true, false, other, 1, ToBerth(9850.f, 2307.f), BERTH_X,
                                 BERTH_Y));
    HomeBind unread;
    Check("an unread bind never hearths",
          !CrossingMemberHearths(true, false, unread, 1, ToBerth(9850.f, 2307.f), BERTH_X,
                                 BERTH_Y));
    Check("no reading of the berth distance never hearths",
          !CrossingMemberHearths(true, false, inn, 1, -1.f, BERTH_X, BERTH_Y));
    Check("the margin is a positive distance", CROSSING_HEARTH_SAVES_YARDS > 0.f);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("crossing hearth: all passed\n");
    return 0;
}
