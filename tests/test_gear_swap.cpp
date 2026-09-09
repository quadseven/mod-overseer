/*
 * What an equip displaced, and the three answers that are not each other.
 *
 * mod-overseer#372. `kind='item_equip'` recorded the item that went on and the
 * slot it went into, and its whole detail column was the literal `slot <n>` -
 * measured over all 453 rows ever written on the dev realm. An upgrade is a
 * comparison and the other half was thrown away at the one moment it existed,
 * so the armory could list equips and say nothing about progression, and a row
 * recording a weapon going into a slot that later held a different weapon could
 * not be told apart from an equip that never stuck.
 *
 * The previous occupant is not reachable from the equip hook - the core
 * visualises the new item into the slot before it calls the hook, and has
 * already removed the old one before it calls EquipItem at all - so the answer
 * comes from a small shadow of the roster's equipment slots, fed by hooks that
 * already fire. This file pins what that shadow decides:
 *
 *   - a straight swap names what came out,
 *   - a slot known to be bare says so,
 *   - a slot nobody ever looked at says THAT, and never "bare",
 *   - and a clear that happened in some earlier world update is not a
 *     displacement, because nothing displaced anything: the slot was empty.
 *
 * That last one is the reason the shadow carries a stamp at all. Compiled
 * against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::GearPriorWord;
using OverseerDecisions::GearSlotBefore;
using OverseerDecisions::GearSlotCleared;
using OverseerDecisions::GearSlotDisplaced;
using OverseerDecisions::GearSlotFilled;
using OverseerDecisions::GearSlotOccupant;
using OverseerDecisions::GearSlotSeen;
using OverseerDecisions::GearSlotShadow;
using OverseerDecisions::GearSlotState;
using OverseerDecisions::GearSwapDetail;

namespace
{

int failures = 0;

void CheckState(char const* what, GearSlotState got, GearSlotState want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, GearPriorWord(got),
                GearPriorWord(want));
    ++failures;
}

void CheckUnsigned(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got [%s], wanted [%s]\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckTrue(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// The pair out of the measured case. A one-handed weapon in the main hand,
// slot 15, replaced by another - which is the swap the table could not tell
// apart from an equip that never took.
GearSlotOccupant TheWornWeapon()
{
    GearSlotOccupant item;
    item.entry = 12969;
    item.name = "Jagged Star";
    item.quality = 2;
    item.itemLevel = 22;
    return item;
}

GearSlotOccupant TheDrop()
{
    GearSlotOccupant item;
    item.entry = 10796;
    item.name = "Stinging Viper";
    item.quality = 3;
    item.itemLevel = 23;
    return item;
}

constexpr unsigned MAIN_HAND = 15;

// Three readings of the core's per-world-update millisecond stamp. The world
// loop holds a 50ms interval, so the second of these is the very next update
// after the first and the third is about an hour later.
constexpr std::uint64_t ONE_UPDATE = 4200000;
constexpr std::uint64_t A_LATER_UPDATE = 4200050;
constexpr std::uint64_t AN_HOUR_LATER = 7800000;

// A login seed, which is the only place this shadow reads a character
// directly, and the reason an empty slot is a fact rather than an absence.
GearSlotShadow SeededWith(GearSlotOccupant const& item)
{
    GearSlotShadow shadow;
    GearSlotSeen(shadow, true, item);
    return shadow;
}

GearSlotShadow SeededBare()
{
    GearSlotShadow shadow;
    GearSlotSeen(shadow, false, GearSlotOccupant{});
    return shadow;
}

// ---------------------------------------------------------------------------

// The whole point of #372: the row can name what came out.
void AStraightSwapNamesWhatCameOut()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());

    // Player::SwapItem removes the destination and equips into it as two
    // consecutive statements, so both carry the same world update stamp.
    GearSlotCleared(shadow, ONE_UPDATE);
    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);

    CheckState("a swap displaced an item", before.state, GearSlotState::Occupied);
    CheckUnsigned("the displaced entry", before.item.entry, 12969);
    CheckText("the displaced name", before.item.name, "Jagged Star");
    CheckText("the detail sentence", GearSwapDetail(MAIN_HAND, before),
              "slot 15 over Jagged Star");
    CheckText("the column word", GearPriorWord(before.state), "item");
}

// The point of storing quality and item level on both sides: the reader can
// say "a rare beat an uncommon" off the row, with no second lookup.
void BothSidesOfTheComparisonAreOnTheRow()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);
    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);

    GearSlotOccupant const arriving = TheDrop();
    CheckUnsigned("the prior quality", before.item.quality, 2);
    CheckUnsigned("the prior item level", before.item.itemLevel, 22);
    CheckTrue("a rare beats an uncommon from the row alone",
              arriving.quality > before.item.quality);
    CheckTrue("and the item level agrees", arriving.itemLevel > before.item.itemLevel);
}

// A clear with nothing put back is not a displacement, and an equip an hour
// later filled an empty slot. This is what the stamp is for: without it the
// core's own AutoUnequipOffhandIfNeed would make every later equip in that slot
// claim to have replaced a weapon taken off long before.
void AClearFromAnEarlierUpdateIsNotADisplacement()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);

    GearSlotBefore const nextUpdate = GearSlotDisplaced(shadow, A_LATER_UPDATE);
    CheckState("the very next update is already too late", nextUpdate.state,
               GearSlotState::Empty);

    GearSlotBefore const muchLater = GearSlotDisplaced(shadow, AN_HOUR_LATER);
    CheckState("and an hour later certainly is", muchLater.state, GearSlotState::Empty);
    CheckText("the detail says so", GearSwapDetail(MAIN_HAND, muchLater),
              "slot 15 over nothing");
    CheckText("the column word", GearPriorWord(muchLater.state), "empty");
}

// A slot seeded bare at login is an exact answer, not a shrug.
void AKnownBareSlotIsNotAnUnknownOne()
{
    GearSlotShadow const shadow = SeededBare();
    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);

    CheckState("a seeded bare slot", before.state, GearSlotState::Empty);
    CheckText("reads as empty", GearPriorWord(before.state), "empty");
}

// And a slot nobody ever looked at says exactly that. Folding this into
// "empty" is the mistake the death context columns document at length: it
// would write "this was a first ever piece" over an equip that replaced
// something this module simply had not seen.
void AnUnobservedSlotIsNeverReportedAsEmpty()
{
    GearSlotShadow const shadow;
    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);

    CheckState("never looked at", before.state, GearSlotState::Unobserved);
    CheckText("reads as unknown", GearPriorWord(before.state), "unknown");
    CheckText("and the detail does not claim the slot was bare",
              GearSwapDetail(MAIN_HAND, before), "slot 15 over an unobserved slot");
}

// One clear can explain one fill and no more. A two-hander into the main hand
// makes the core empty the off hand in the same update, so a second fill of the
// same slot in that update must not claim the same displaced item again.
void OneClearExplainsOnlyOneFill()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);

    GearSlotBefore const first = GearSlotDisplaced(shadow, ONE_UPDATE);
    CheckState("the first fill takes it", first.state, GearSlotState::Occupied);
    GearSlotFilled(shadow, TheDrop());

    // Filled, so a fresh question in the same update is answered from the
    // occupant and not a second time from the spent removal.
    GearSlotBefore const second = GearSlotDisplaced(shadow, ONE_UPDATE);
    CheckState("the second is answered from the occupant", second.state,
               GearSlotState::Occupied);
    CheckUnsigned("which is the item just put on", second.item.entry, 10796);
}

// Equipping over a slot the shadow believes is still occupied is answered from
// the occupant rather than from any pending removal.
void AnOccupiedSlotIsAnsweredFromItsOccupant()
{
    GearSlotShadow shadow;
    GearSlotFilled(shadow, TheWornWeapon());

    GearSlotBefore const before = GearSlotDisplaced(shadow, AN_HOUR_LATER);
    CheckState("an occupied slot", before.state, GearSlotState::Occupied);
    CheckUnsigned("names its occupant", before.item.entry, 12969);
}

// A clear of a slot that was already bare must not wipe a pending removal:
// the core clears the visible-item fields of empty equipment slots on more
// than one path, and one of those landing between the removal and the equip
// would lose the only copy of the displaced item.
void ClearingAnEmptySlotKeepsThePendingRemoval()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);
    GearSlotCleared(shadow, ONE_UPDATE);

    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);
    CheckState("still a displacement", before.state, GearSlotState::Occupied);
    CheckUnsigned("and still the right item", before.item.entry, 12969);
}

// A login seed is a direct reading of the world and supersedes anything the
// shadow half remembers, so a character that relogs does not carry a
// displacement across the gap.
void ASeedSupersedesAPendingRemoval()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);
    GearSlotSeen(shadow, false, GearSlotOccupant{});

    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);
    CheckState("the seed wins", before.state, GearSlotState::Empty);
}

// The existing detail is `slot <n>` and the website reads it, so the slot
// number stays exactly where it was and everything new is appended.
void TheExistingDetailPrefixIsUntouched()
{
    GearSlotShadow shadow = SeededWith(TheWornWeapon());
    GearSlotCleared(shadow, ONE_UPDATE);
    GearSlotBefore const before = GearSlotDisplaced(shadow, ONE_UPDATE);

    unsigned const measured[] = {2u, 4u, 6u, 7u, 8u, 9u, 14u, 15u};
    for (unsigned slot : measured)
    {
        std::string const detail = GearSwapDetail(slot, before);
        std::string const prefix = "slot " + std::to_string(slot) + " ";
        CheckTrue("the detail still opens with the slot number",
                  detail.compare(0, prefix.size(), prefix) == 0);
    }
}

// An item whose template could not be resolved has an empty name, and the
// sentence must not read "slot 15 over " with nothing after it.
void AnUnnamedItemStillReadsAsASentence()
{
    GearSlotBefore before;
    before.state = GearSlotState::Occupied;
    before.item.entry = 12969;

    CheckText("an unnamed displaced item", GearSwapDetail(MAIN_HAND, before),
              "slot 15 over an item with no template");
}

// The column is VARCHAR(255). A name long enough to overflow it is truncated
// here, where it is a decision this file can hold, rather than by MySQL.
void ALongNameCannotOverflowTheColumn()
{
    GearSlotBefore before;
    before.state = GearSlotState::Occupied;
    before.item.name = std::string(400, 'x');

    std::string const detail = GearSwapDetail(MAIN_HAND, before);
    CheckTrue("the detail fits the column", detail.size() <= 255);
    CheckTrue("and still opens with the slot", detail.compare(0, 8, "slot 15 ") == 0);
}

// The three words are what the column holds, so they are pinned rather than
// left to be retyped by a reader.
void TheColumnWordsAreTheOnesARowCarries()
{
    CheckText("occupied", GearPriorWord(GearSlotState::Occupied), "item");
    CheckText("empty", GearPriorWord(GearSlotState::Empty), "empty");
    CheckText("unobserved", GearPriorWord(GearSlotState::Unobserved), "unknown");
}

}  // namespace

int main()
{
    AStraightSwapNamesWhatCameOut();
    BothSidesOfTheComparisonAreOnTheRow();
    AClearFromAnEarlierUpdateIsNotADisplacement();
    AKnownBareSlotIsNotAnUnknownOne();
    AnUnobservedSlotIsNeverReportedAsEmpty();
    OneClearExplainsOnlyOneFill();
    AnOccupiedSlotIsAnsweredFromItsOccupant();
    ClearingAnEmptySlotKeepsThePendingRemoval();
    ASeedSupersedesAPendingRemoval();
    TheExistingDetailPrefixIsUntouched();
    AnUnnamedItemStillReadsAsASentence();
    ALongNameCannotOverflowTheColumn();
    TheColumnWordsAreTheOnesARowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: an equip names what it displaced, and never guesses that it displaced nothing\n");
    return EXIT_SUCCESS;
}
