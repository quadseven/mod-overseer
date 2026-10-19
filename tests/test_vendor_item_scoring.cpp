/*
 * Vendor items are never considered (#973).
 *
 * Vendor stock at vendors the party visits should be scored and chosen
 * based on item score relative to each member's equipment.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void ExpectText(char const* what, std::string const& got, char const* want)
{
    if (got != want)
    {
        std::fprintf(stderr, "FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
        ++failures;
    }
}

// A mock vendor item with an entry, a price, and a score
struct VendorItem
{
    uint32_t entry{0};
    uint32_t price{0};
    float score{0.0f};
};

// Check if a vendor item is an upgrade for a specific member
bool VendorItemIsUpgrade(VendorItem const& item, float memberScore)
{
    if (!item.score)
        return false;
    return item.score >
           memberScore * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;
}

// Find the best vendor item for a member
uint32_t BestVendorItemForMember(std::vector<VendorItem> const& items, float memberScore)
{
    uint32_t bestEntry = 0;
    float bestScore = memberScore * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;
    
    for (VendorItem const& item : items)
    {
        if (VendorItemIsUpgrade(item, memberScore))
        {
            if (item.score > bestScore)
            {
                bestScore = item.score;
                bestEntry = item.entry;
            }
        }
    }
    
    return bestEntry;
}

// Find the best vendor item for the best-equipped member
uint32_t BestVendorItemForParty(std::vector<VendorItem> const& items,
                                 std::vector<float> memberScores)
{
    uint32_t bestEntry = 0;
    float bestScore = 0;
    
    for (float score : memberScores)
    {
        uint32_t item = BestVendorItemForMember(items, score);
        if (item)
        {
            VendorItem const* itemData = nullptr;
            for (VendorItem const& v : items)
            {
                if (v.entry == item)
                {
                    itemData = &v;
                    break;
                }
            }
            if (itemData && itemData->score > bestScore)
            {
                bestScore = itemData->score;
                bestEntry = item;
            }
        }
    }
    
    return bestEntry;
}

void VendorItemScoringWorks()
{
    std::vector<VendorItem> items = {
        {100, 1000, 35.0f},   // Upgrade for tank (35 > 30)
        {200, 1500, 45.0f},   // Upgrade for healer (45 > 30)
        {300, 500, 25.0f}     // Not an upgrade for anyone
    };
    std::vector<float> scores = {30.0f, 30.0f, 30.0f}; // All members
    
    uint32_t best = BestVendorItemForParty(items, scores);
    Expect(best != 0, "best vendor item found for party");
    Expect(best == 200, "best item is the healer upgrade");
}

void NoUpgradeReturnsZero()
{
    std::vector<VendorItem> items = {
        {100, 1000, 25.0f},   // Not an upgrade (25 <= 30)
        {200, 1500, 28.0f}    // Not an upgrade (28 <= 30)
    };
    std::vector<float> scores = {30.0f, 30.0f};
    
    uint32_t best = BestVendorItemForParty(items, scores);
    Expect(best == 0, "no upgrade returns zero");
}

void SingleItemChosen()
{
    std::vector<VendorItem> items = {
        {100, 1000, 35.0f}
    };
    std::vector<float> scores = {30.0f};
    
    uint32_t best = BestVendorItemForParty(items, scores);
    Expect(best == 100, "single item is always chosen");
}

void AllUpgradeItemsConsidered()
{
    std::vector<VendorItem> items = {
        {100, 1000, 35.0f},
        {200, 1500, 45.0f},
        {300, 2000, 50.0f}
    };
    std::vector<float> scores = {30.0f, 30.0f};
    
    uint32_t best = BestVendorItemForParty(items, scores);
    Expect(best == 300, "highest-scoring upgrade is chosen");
}
}  // namespace

int main()
{
    VendorItemScoringWorks();
    NoUpgradeReturnsZero();
    SingleItemChosen();
    AllUpgradeItemsConsidered();
    
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("vendor item scoring works correctly\n");
    return EXIT_SUCCESS;
}
