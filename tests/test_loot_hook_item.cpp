/*
 * A loot hook never reads the Item it was handed (mod-overseer#572).
 *
 * The #568 build segfaulted seconds after the bots began to loot. The core
 * hands every script's OnPlayerLootItem the same Item*, and a script ahead of
 * this module's (mod-junk-to-gold, for every grey) destroys it; a freshly
 * looted item is deleted on the spot, so this module's hook read freed memory
 * when it asked the item for its quality.
 *
 * The fix: OnPlayerStoreNewItem, which runs inside Player::StoreNewItem while
 * the item is alive, notes a notable item's guid, and the loot hook takes the
 * note and looks the guid up in the looter's bags. This file pins what the
 * note answers, above all that a grey (the item another script destroys)
 * leaves the loot hook nothing to look up.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using OverseerDecisions::LootStoreNote;
using OverseerDecisions::NoteStoredItem;
using OverseerDecisions::TakeLootedItemGuid;

namespace
{

int failures = 0;

void CheckGuid(char const* what, std::uint32_t got, std::uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, want %u\n", what, static_cast<unsigned>(got),
                static_cast<unsigned>(want));
    ++failures;
}

constexpr std::uint64_t UGGA = 0x0000000000000011ULL;
constexpr std::uint64_t GROG = 0x0000000000000012ULL;

void AGreyLeavesNothingToRead()
{
    // The crash: a grey is stored, the junk script destroys it, and the loot
    // hook must have no guid to chase, and must never need the pointer.
    LootStoreNote note;
    NoteStoredItem(note, UGGA, 9001, 1, false);
    CheckGuid("a grey store leaves no guid", TakeLootedItemGuid(note, UGGA, 1), 0);
}

void ANotableStoreIsFoundAgain()
{
    LootStoreNote note;
    NoteStoredItem(note, UGGA, 4242, 1, true);
    CheckGuid("a notable store names its guid", TakeLootedItemGuid(note, UGGA, 1), 4242);
}

void TheNoteIsSpentOnce()
{
    LootStoreNote note;
    NoteStoredItem(note, UGGA, 4242, 1, true);
    TakeLootedItemGuid(note, UGGA, 1);
    CheckGuid("a second loot hook finds no note", TakeLootedItemGuid(note, UGGA, 1), 0);
}

void ALaterStoreOverwritesAnEarlierOne()
{
    // A rare bought from a vendor, then a grey looted: the loot must not be
    // mistaken for the rare.
    LootStoreNote note;
    NoteStoredItem(note, UGGA, 4242, 1, true);
    NoteStoredItem(note, UGGA, 9001, 1, false);
    CheckGuid("the last store decides", TakeLootedItemGuid(note, UGGA, 1), 0);
}

void OnlyTheLooterAndTheCountMatch()
{
    LootStoreNote note;
    NoteStoredItem(note, GROG, 4242, 1, true);
    CheckGuid("another character's store is not this loot", TakeLootedItemGuid(note, UGGA, 1),
              0);

    NoteStoredItem(note, UGGA, 4242, 1, true);
    CheckGuid("a different count is not this loot", TakeLootedItemGuid(note, UGGA, 5), 0);

    NoteStoredItem(note, UGGA, 4242, 1, true);
    CheckGuid("no looter reads nothing", TakeLootedItemGuid(note, 0, 1), 0);
    CheckGuid("and still spends the note", TakeLootedItemGuid(note, UGGA, 1), 0);
}

void AnEmptyNoteReadsNothing()
{
    LootStoreNote note;
    CheckGuid("no store, no guid", TakeLootedItemGuid(note, UGGA, 1), 0);
}

}  // namespace

int main()
{
    AGreyLeavesNothingToRead();
    ANotableStoreIsFoundAgain();
    TheNoteIsSpentOnce();
    ALaterStoreOverwritesAnEarlierOne();
    OnlyTheLooterAndTheCountMatch();
    AnEmptyNoteReadsNothing();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a loot hook finds the looted item again and never reads a freed one\n");
    return EXIT_SUCCESS;
}
