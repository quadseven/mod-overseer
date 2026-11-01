#include "overseer_disenchant.h"
#include <cstdio>
#include <map>
#include <vector>

namespace D = OverseerDisenchant;
struct Inventory
{
    bool owned = true;
    bool acceptLoot = true;
    bool releaseWorks = true;
    unsigned releases = 0;
    std::map<uint32_t, uint32_t> bags{{10940, 7}};
    std::vector<D::LootSlot> loot{{0, 10940, 2, false}};
    bool Owns(uint64_t guid) const { return owned && guid == 42; }
    uint32_t Count(uint32_t item) const
    { auto it = bags.find(item); return it == bags.end() ? 0 : it->second; }
    std::vector<D::LootSlot> Slots(uint64_t) const { return loot; }
    void Store(uint64_t, uint8_t slot)
    {
        if (!acceptLoot) return;
        for (auto& item : loot)
            if (item.slot == slot && !item.taken)
            { bags[item.entry] += item.count; item.taken = true; }
    }
    bool Empty(uint64_t) const
    { for (auto const& item : loot) if (!item.taken) return false; return true; }
    void Release(uint64_t) { ++releases; if (releaseWorks) owned = false; }
};
int failures = 0;
void Check(char const* message, bool value)
{ if (!value) { std::printf("FAIL %s\n", message); ++failures; } }
int main()
{
    {
        Inventory inv;
        D::Proof proof; proof.source = 42;
        Check("an accepted cast without loot proof cannot finish", !D::Finish(inv, proof));
        Check("the source item remains owned before inventory proof", inv.owned && !inv.releases);
        D::Collect(inv, 42, proof);
        Check("reagent count is read from the inventory after storage", inv.Count(10940) == 9);
        Check("storage does not destroy an item inside the send-loot hook", inv.owned && !inv.releases);
        Check("the reachable finish path consumes the source after verified loot", D::Finish(inv, proof));
        Check("only one source was consumed", !inv.owned && inv.releases == 1);
    }
    {
        Inventory inv; inv.acceptLoot = false;
        D::Proof proof; proof.source = 42;
        D::Collect(inv, 42, proof);
        Check("full bags leave the errand and source standing", !D::Finish(inv, proof) && inv.owned && !inv.releases);
        inv.acceptLoot = true;
        D::Collect(inv, 42, proof);
        Check("retry reuses the original baseline and can finish", D::Finish(inv, proof));
    }
    {
        Inventory inv;
        D::Proof proof; proof.source = 42;
        D::Collect(inv, 99, proof);
        Check("another loot window cannot complete this errand", !D::Finish(inv, proof) && inv.Count(10940) == 7);
    }
    {
        Inventory inv; inv.loot.push_back({1, 10938, 1, false});
        D::Proof proof; proof.source = 42;
        D::Collect(inv, 42, proof);
        inv.bags[10938] = 0;
        Check("all rolled reagent counts must be present", !D::Finish(inv, proof) && inv.owned);
    }
    {
        Inventory inv; inv.releaseWorks = false;
        D::Proof proof; proof.source = 42;
        D::Collect(inv, 42, proof);
        Check("a failed source release cannot clear an errand", !D::Finish(inv, proof));
        inv.releaseWorks = true;
        Check("a later release can finish without a second cast", D::Finish(inv, proof));
    }
    {
        Inventory inv; inv.owned = false;
        D::Proof proof; proof.source = 42;
        D::Collect(inv, 42, proof);
        Check("an absent or foreign item supplies no loot proof", !D::Finish(inv, proof));
    }
    return failures ? 1 : 0;
}
