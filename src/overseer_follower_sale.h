#ifndef OVERSEER_FOLLOWER_SALE_H
#define OVERSEER_FOLLOWER_SALE_H

// A GROUPED FOLLOWER WITH FULL BAGS MAY TAKE ITS SALE WALK (#1395).
//
// The walk gate refuses every grouped follower (MailWalkRefusal::Follower): a
// follower moves by following, and the module does not steer it away from its
// leader. For a quester that never ends: questers stay grouped for many hours,
// and in one 30 hour bridge log 651 of 1229 quester sale passes were held for
// this reason while twelve questers sat at 0 free bag slots and could not take
// quests that hand over an item.
//
// THE RULE. A grouped follower's `walk-to-vendor any` (the sale walk: the
// nearest vendor that buys, for the `sell` rows that follow it) passes the
// follower wall when the module itself reads FOLLOWER_SALE_FREE_SLOT_FLOOR or
// fewer free bag slots. The floor is the bridge's quester SELL_FLOOR, so the
// walk the bridge asks for at full bags is the walk let through, and a row
// written for a follower with room is still refused. Every other wall of the
// gate still applies, and every other walk of a follower (an item vendor walk,
// a mailbox, a trainer, a spawn) is still refused as before.
//
// No AzerothCore include: tests/test_follower_sale.cpp compiles this alone.

#include <cstdint>

namespace OverseerDecisions
{

constexpr uint32_t FOLLOWER_SALE_FREE_SLOT_FLOOR = 1;

// Whether a grouped follower's vendor walk passes the follower wall: only the
// sale walk (`any`), and only at the floor or under.
inline bool FollowerSaleWalkPasses(bool anyVendor, uint32_t freeBagSlots)
{
    return anyVendor && freeBagSlots <= FOLLOWER_SALE_FREE_SLOT_FLOOR;
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_FOLLOWER_SALE_H
