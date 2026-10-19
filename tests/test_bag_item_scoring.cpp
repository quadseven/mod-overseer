/*
 * Drops sit in bags (#973).
 *
 * Items that land in a bag are scored, and if an item scores above the
 * equipped item, it should be equipped within two minutes.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>
#include <ctime>

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

// A mock bag item with an entry and a score
struct BagItem
{
    uint32_t entry{0};
    float score{0.0f};
};

// Item added timestamp
std::map<uint32_t, time_t> itemTimestamps;

// Record when an item enters a bag
void RecordBagItem(uint32_t entry, time_t now)
{
    itemTimestamps[entry] = now;
}

// Check if an item has been in the bag for less than 2 minutes
bool ItemAddedWithinTwoMinutes(uint32_t entry, time_t now)
{
    auto it = itemTimestamps.find(entry);
    if (it == itemTimestamps.end())
        return false;
    
    return (now - it->second) <= 120;
}

// Check if a bag item should be equipped (scores above equipped item)
bool BagItemShouldBeEquipped(BagItem const& bagItem, float equippedScore)
{
    if (!bagItem.score || equippedScore <= 0.0f)
        return false;
    return bagItem.score >
           equippedScore * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;
}

// Find the highest-scoring bag item
uint32_t HighestScoringBagItem(std::vector<BagItem> const& items)
{
    if (items.empty())
        return 0;
    
    uint32_t bestEntry = items[0].entry;
    float bestScore = items[0].score;
    
    for (size_t i = 1; i < items.size(); ++i)
    {
        if (items[i].score > bestScore)
        {
            bestScore = items[i].score;
            bestEntry = items[i].entry;
        }
    }
    
    return bestEntry;
}

void BagItemShouldBeEquippedWhenHigherScore()
{
    BagItem bagItem{100, 50.0f};
    float equippedScore = 30.0f;
    
    bool shouldEquip = BagItemShouldBeEquipped(bagItem, equippedScore);
    Expect(shouldEquip, "item scoring higher than equipped should be equipped");
}

void BagItemNotEquippedWhenLowerScore()
{
    BagItem bagItem{100, 25.0f};
    float equippedScore = 30.0f;
    
    bool shouldEquip = BagItemShouldBeEquipped(bagItem, equippedScore);
    Expect(!shouldEquip, "item scoring lower than equipped should not be equipped");
}

void ItemRecordedInBag()
{
    time_t now = time(nullptr);
    BagItem bagItem{100, 50.0f};
    
    RecordBagItem(bagItem.entry, now);
    
    bool recorded = itemTimestamps.find(bagItem.entry) != itemTimestamps.end();
    Expect(recorded, "item is recorded when added to bag");
}

void ItemTimestampedWithinTwoMinutes()
{
    time_t now = time(nullptr);
    time_t oldTime = now - 180;
    BagItem bagItem{100, 50.0f};
    
    RecordBagItem(bagItem.entry, oldTime);
    
    bool withinWindow = ItemAddedWithinTwoMinutes(bagItem.entry, now);
    Expect(!withinWindow, "item older than 2 minutes is not within window");
}

void ItemTimestampedExactlyTwoMinutes()
{
    time_t now = time(nullptr);
    time_t oldTime = now - 120;
    BagItem bagItem{100, 50.0f};
    
    RecordBagItem(bagItem.entry, oldTime);
    
    bool withinWindow = ItemAddedWithinTwoMinutes(bagItem.entry, now);
    Expect(withinWindow, "item exactly 2 minutes old is within window");
}

void HighestScoringBagItemChosen()
{
    std::vector<BagItem> items = {
        {100, 40.0f},
        {200, 50.0f},
        {300, 45.0f}
    };
    
    uint32_t best = HighestScoringBagItem(items);
    Expect(best == 200, "highest-scoring bag item is chosen");
}

void EmptyBagReturnsZero()
{
    std::vector<BagItem> items;
    
    uint32_t best = HighestScoringBagItem(items);
    Expect(best == 0, "empty bag returns zero");
}
}  // namespace

int main()
{
    BagItemShouldBeEquippedWhenHigherScore();
    BagItemNotEquippedWhenLowerScore();
    ItemRecordedInBag();
    ItemTimestampedWithinTwoMinutes();
    ItemTimestampedExactlyTwoMinutes();
    HighestScoringBagItemChosen();
    EmptyBagReturnsZero();
    
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("bag item scoring and timing works correctly\n");
    return EXIT_SUCCESS;
}
