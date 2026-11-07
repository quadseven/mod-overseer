/*
 * A grouped follower with full bags may take its sale walk, and nothing else
 * (#1395).
 *
 * The bridge asks a quester at 1 or fewer free bag slots to `walk-to-vendor
 * any` and sell. The walk gate refused every grouped follower, so a quester in
 * a quest group never sold: 651 of 1229 sale passes held in one 30 hour log.
 * The rule lets the sale walk through the follower wall at the floor and keeps
 * every other follower walk refused. The gate itself is unchanged: the adapter
 * clears the follower fact only when this rule says so, and the other walls
 * still apply in MailWalkGate's own order.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"
#include "overseer_follower_sale.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using OverseerDecisions::FOLLOWER_SALE_FREE_SLOT_FLOOR;
using OverseerDecisions::FollowerSaleWalkPasses;
using OverseerDecisions::MailWalkGate;
using OverseerDecisions::MailWalkGateFacts;
namespace R = OverseerDecisions::MailWalkRefusal;

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

MailWalkGateFacts Follower()
{
    MailWalkGateFacts f;
    f.hasBotAI = true;
    f.inWorld = true;
    f.alive = true;
    f.groupedFollower = true;
    return f;
}

// What the adapter does: the follower fact is cleared only for a vendor walk
// the rule lets through.
char const* Gate(MailWalkGateFacts f, bool vendorWalk, bool anyVendor, uint32_t freeSlots)
{
    if (f.groupedFollower && vendorWalk && FollowerSaleWalkPasses(anyVendor, freeSlots))
        f.groupedFollower = false;
    return MailWalkGate(f);
}

bool Is(char const* got, char const* want)
{
    return std::strcmp(got, want) == 0;
}

void TheFloorIsTheBridgesSellFloor()
{
    Check("floor is 1, the bridge's quester SELL_FLOOR", FOLLOWER_SALE_FREE_SLOT_FLOOR == 1);
}

void AFullFollowersSaleWalkPasses()
{
    Check("0 free slots: the sale walk starts", Is(Gate(Follower(), true, true, 0), ""));
    Check("1 free slot: the sale walk starts", Is(Gate(Follower(), true, true, 1), ""));
}

void AFollowerWithRoomIsStillRefused()
{
    Check("2 free slots: refused as a follower",
          Is(Gate(Follower(), true, true, 2), R::Follower));
    Check("an empty bag: refused as a follower",
          Is(Gate(Follower(), true, true, 80), R::Follower));
}

void EveryOtherFollowerWalkIsStillRefused()
{
    Check("an item vendor walk at full bags", Is(Gate(Follower(), true, false, 0), R::Follower));
    Check("a mailbox, trainer or spawn walk at full bags",
          Is(Gate(Follower(), false, true, 0), R::Follower));
}

void TheOtherWallsStillApply()
{
    MailWalkGateFacts f = Follower();
    f.inCombat = true;
    Check("in combat is still refused", Is(Gate(f, true, true, 0), R::InCombat));
    f = Follower();
    f.heldByAnother = true;
    Check("held by another verb is still refused", Is(Gate(f, true, true, 0), R::HeldByAnother));
    f = Follower();
    f.alreadyWalking = true;
    Check("a walk under way is still refused", Is(Gate(f, true, true, 0), R::AlreadyWalking));
}

void ALeaderOrUngroupedCharacterIsUnchanged()
{
    MailWalkGateFacts f = Follower();
    f.groupedFollower = false;
    Check("ungrouped with room walks", Is(Gate(f, true, true, 40), ""));
    Check("the rule alone: an item walk never passes", !FollowerSaleWalkPasses(false, 0));
}

}  // namespace

int main()
{
    TheFloorIsTheBridgesSellFloor();
    AFullFollowersSaleWalkPasses();
    AFollowerWithRoomIsStillRefused();
    EveryOtherFollowerWalkIsStillRefused();
    TheOtherWallsStillApply();
    ALeaderOrUngroupedCharacterIsUnchanged();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a grouped follower with full bags takes its sale walk and nothing else\n");
    return EXIT_SUCCESS;
}
