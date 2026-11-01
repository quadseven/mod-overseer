#ifndef OVERSEER_DISENCHANT_H
#define OVERSEER_DISENCHANT_H

#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace OverseerDisenchant
{
struct LootSlot
{
    uint8_t slot;
    uint32_t entry;
    uint32_t count;
    bool taken;
};

struct Proof
{
    uint64_t source = 0;
    bool observed = false;
    std::map<uint32_t, uint32_t> before;
    std::map<uint32_t, uint64_t> expected;
};

// Runs before the loot window reaches a client. Never release here: the
// core's SendLoot still reads the source item's loot after this hook returns.
template <class Inventory>
void Collect(Inventory& inventory, uint64_t opened, Proof& proof)
{
    if (!opened || opened != proof.source || !inventory.Owns(opened))
        return;
    auto const slots = inventory.Slots(opened);
    if (!proof.observed)
    {
        for (auto const& slot : slots)
        {
            // A new window is needed to establish a baseline. A window
            // already partly looted by another path cannot prove its delta.
            if (slot.taken || !slot.entry || !slot.count)
                return;
        }
        for (auto const& slot : slots)
        {
            proof.before[slot.entry] = inventory.Count(slot.entry);
            proof.expected[slot.entry] += slot.count;
        }
        proof.observed = true;
    }
    for (auto const& slot : slots)
        if (!slot.taken)
            inventory.Store(opened, slot.slot);
}

// Runs after SendLoot returns, on the drive poll. Neither a cast result nor
// an empty loot window substitutes for the reagent inventory read-back.
template <class Inventory>
bool Finish(Inventory& inventory, Proof const& proof)
{
    if (!proof.source || !proof.observed || proof.expected.empty())
        return false;
    for (auto const& [entry, count] : proof.expected)
    {
        uint64_t const needed = uint64_t(proof.before.at(entry)) + count;
        if (needed > std::numeric_limits<uint32_t>::max() ||
            inventory.Count(entry) < needed)
            return false;
    }
    if (inventory.Owns(proof.source))
    {
        if (!inventory.Empty(proof.source))
            return false;
        inventory.Release(proof.source);
    }
    return !inventory.Owns(proof.source);
}
}

#endif
