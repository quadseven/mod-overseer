/*
 * A bag equip is judged by its slots against the bag it replaces in the
 * position the equip verb fills, never by the gear score (wow-overseer#1014).
 *
 * Measured on the dev realm: the bridge asked Zrog to put on Blue Leather Bag
 * (8 slots) in place of Small Brown Pouch (6 slots), and the module answered
 * every hour with "the bot keeps an empty slot (score 0.00) over Blue Leather
 * Bag (score 0.00)". The gear scorer gives a bag 0, the slot it compared
 * against was below the bag positions and so read as empty, and the upgrade
 * verdict's own bag check skips the first bag position, where the pouch was.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* label, bool got, bool wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", label, got ? "true" : "false",
                wanted ? "true" : "false");
    ++failures;
}

void CheckSize(char const* label, std::size_t got, std::size_t wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got %zu, wanted %zu\n", label, got, wanted);
    ++failures;
}

void CheckText(char const* label, std::string const& got, char const* wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", label, got.c_str(), wanted);
    ++failures;
}

std::string Read(char const* path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}

int main()
{
    // The live case: the pouch is in the FIRST position, the one the upgrade
    // verdict's GetSmallestBagSize never reads. The verb fills that position,
    // and an 8-slot bag beats a 6-slot pouch, so it is whispered.
    std::vector<BagPosition> zrog = {{"Small Brown Pouch", 6},
                                     {"Green Leather Bag", 8},
                                     {"Green Leather Bag", 8},
                                     {"Green Leather Bag", 8}};
    CheckSize("the verb fills the smallest bag's position", BagEquipPosition(zrog), 0);
    EquipVerdict bigger = BagEquipVerdict("Blue Leather Bag", 8, zrog);
    Check("a bigger bag is whispered", bigger.whisper, true);
    CheckText("a whispered bag waits on the read-back", bigger.row.status, "verifying");
    CheckText("a whispered bag carries no detail yet", bigger.row.detail, "");

    // GetSmallestBagSlot keeps scanning on a tie, so the LAST of the equally
    // smallest bags is the one replaced.
    std::vector<BagPosition> tie = {{"Linen Bag", 6},
                                    {"Small Brown Pouch", 6},
                                    {"Green Leather Bag", 8},
                                    {"Green Leather Bag", 8}};
    CheckSize("on a tie the last smallest bag is replaced", BagEquipPosition(tie), 1);

    // The first empty position is filled before any swap, by any bag.
    std::vector<BagPosition> gap = {{"Green Leather Bag", 8},
                                    {"", 0},
                                    {"Small Brown Pouch", 6},
                                    {"", 0}};
    CheckSize("the first empty position is filled", BagEquipPosition(gap), 1);
    EquipVerdict fills = BagEquipVerdict("Small Black Pouch", 4, gap);
    Check("a bag for an empty position is whispered", fills.whisper, true);
    CheckText("a bag for an empty position waits on the read-back", fills.row.status,
              "verifying");

    // An equal bag gains nothing: refused, naming both bags and their slots.
    std::vector<BagPosition> full = {{"Green Leather Bag", 8},
                                     {"Green Leather Bag", 8},
                                     {"Red Leather Bag", 8},
                                     {"Green Leather Bag", 8}};
    EquipVerdict equal = BagEquipVerdict("Blue Leather Bag", 8, full);
    Check("an equal bag is not whispered", equal.whisper, false);
    CheckText("an equal bag ends unchanged", equal.row.status, "unchanged");
    CheckText("an equal bag names both bags and both slot counts", equal.row.detail,
              "the bot keeps Green Leather Bag (8 slots) in bag position 4: "
              "Blue Leather Bag holds 8 slots, not more");

    // A smaller bag loses slots: refused the same way.
    EquipVerdict smaller = BagEquipVerdict("Small Brown Pouch", 6, full);
    Check("a smaller bag is not whispered", smaller.whisper, false);
    CheckText("a smaller bag names both bags and both slot counts", smaller.row.detail,
              "the bot keeps Green Leather Bag (8 slots) in bag position 4: "
              "Small Brown Pouch holds 6 slots, not more");

    // No positions read at all: refused rather than whispered blind.
    EquipVerdict none = BagEquipVerdict("Blue Leather Bag", 8, {});
    Check("no bag position is not whispered", none.whisper, false);
    CheckText("no bag position says so", none.row.detail,
              "the bot has no bag position for Blue Leather Bag");

    // THE ADAPTER ROUTES A CONTAINER HERE, and reads it back from the bag
    // positions. The pure verdict above is only half the fix: the module's
    // equip path has to call it before the gear scorer, and the read-back has
    // to look past the equipment slots, or a bag that went on reads as
    // "the bot did not put the item on".
    std::string const adapter = Read("src/mod_overseer.cpp");
    Check("the adapter source was read", !adapter.empty(), true);
    std::size_t const bagJudged = adapter.find("OverseerDecisions::BagEquipVerdict(");
    std::size_t const gearScored = adapter.find("calculator.CalculateItem(proto->ItemId");
    Check("a container is judged by BagEquipVerdict", bagJudged != std::string::npos, true);
    Check("the bag verdict comes before the gear score",
          bagJudged != std::string::npos && gearScored != std::string::npos &&
              bagJudged < gearScored,
          true);
    Check("the read-back scans the bag positions for a container",
          adapter.find("worn = worn || item->GetGUID() == check.guid;") != std::string::npos,
          true);

    return failures ? 1 : 0;
}
