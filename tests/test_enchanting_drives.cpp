/*
 * DriveDisenchant and DriveEnchant: standing item-target errands (#444).
 *
 * The decisions are checked as behaviour: what the command channel may name
 * as an item, and when an errand ends. An errand ends only on what the world
 * reads back, so a refused cast - which changes nothing - leaves it standing,
 * the discipline DriveCraft holds.
 *
 * The adapter is checked as text (run from the repo root): the drives route
 * through those decisions and cast through the SpellInfo-taking item-target
 * CastSpell overload. The cast itself and the inventory read-back need a
 * world, which is the live acceptance check, not this file.
 *
 * Compiled against src/overseer_decisions.cpp and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::DisenchantErrandDone;
using OverseerDecisions::EnchantErrandDone;
using OverseerDecisions::ParseDisenchantItemGuid;

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

std::string Read(char const* path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

bool Refused(std::string const& command)
{
    std::uint32_t low = 7;
    return !ParseDisenchantItemGuid(command, low) && low == 7;
}

void TheItemGuidIsBounded()
{
    std::uint32_t low = 0;
    Check("item:<guid> names that item", ParseDisenchantItemGuid("item:1234", low) && low == 1234);
    Check("the largest 32-bit GUID is accepted",
          ParseDisenchantItemGuid("item:4294967295", low) && low == 4294967295u);
    Check("a GUID past 32 bits is refused, not truncated into another item",
          Refused("item:4294967296"));
    Check("a GUID far past 64 bits is refused", Refused("item:99999999999999999999999"));
    Check("a negative GUID is refused, not wrapped", Refused("item:-1"));
    Check("a signed GUID is refused", Refused("item:+5"));
    Check("leading space is refused", Refused("item: 5"));
    Check("trailing text is refused", Refused("item:5'; DROP TABLE x"));
    Check("GUID 0 means none and is refused", Refused("item:0"));
    Check("an empty GUID is refused", Refused("item:"));
    Check("a missing prefix is refused", Refused("5"));
}

void AnErrandEndsOnTheReadBack()
{
    Check("a refused disenchant (nothing gained, item still carried) stands",
          !DisenchantErrandDone(false, false));
    Check("a reagent in the bags ends the disenchant errand", DisenchantErrandDone(true, false));
    Check("the item gone ends the disenchant errand", DisenchantErrandDone(false, true));
    Check("a refused enchant (slot still empty) stands", !EnchantErrandDone(0));
    Check("a permanent enchant read back ends the enchant errand", EnchantErrandDone(41));
}

void TheAdapterIsWired()
{
    std::string const source = Read("src/mod_overseer.cpp");
    std::string const migration = Read(
        "data/sql/characters/base/2026_10_21_01_overseer_enchanting.sql");
    Check("the module source is readable", !source.empty());
    std::size_t const disenchantStart = source.find("void DriveDisenchant()");
    std::size_t const enchantStart = source.find("void DriveEnchant()");
    Check("both drives exist", disenchantStart != std::string::npos &&
          enchantStart != std::string::npos && disenchantStart < enchantStart);
    if (disenchantStart == std::string::npos || enchantStart == std::string::npos ||
        disenchantStart > enchantStart)
        return;
    std::string const disenchant = source.substr(disenchantStart, enchantStart - disenchantStart);
    std::string const enchant = source.substr(enchantStart, 6000);

    Check("the command channel parses through the bounded decision",
          Has(source, "OverseerDecisions::ParseDisenchantItemGuid(command, low)"));
    Check("disenchant casts through the SpellInfo item-target overload",
          Has(disenchant, "SetItemTarget(item)") &&
          Has(disenchant, "CastSpell(targets, info, nullptr, TRIGGERED_NONE)") &&
          Has(disenchant, "ResolveDisenchantLoot(bot, session, itemGuid)"));
    Check("the loot window ends on the inventory read-back",
          Has(source, "HandleAutostoreLootItemOpcode(take)") &&
          Has(source, "GetItemCount(entry)") &&
          Has(source, "OverseerDecisions::DisenchantErrandDone(gained, consumed)"));
    Check("enchant prechecks reagents and casts through the item-target overload",
          Has(enchant, "HasSpellReagents(bot, info)") &&
          Has(enchant, "SetItemTarget(item)") &&
          Has(enchant, "CastSpell(targets, info, nullptr, TRIGGERED_NONE)"));
    Check("enchant ends on the permanent enchant read back from the item",
          Has(enchant, "OverseerDecisions::EnchantErrandDone(") &&
          Has(enchant, "GetEnchantmentId(PERM_ENCHANTMENT_SLOT)"));
    Check("a refused disenchant cast clears nothing",
          Has(disenchant, "if (cast != SPELL_CAST_OK)\n            {") &&
          !Has(disenchant.substr(disenchant.find("if (cast != SPELL_CAST_OK)"), 600),
               "disenchant_guid = 0"));

    Check("command enum preserves all existing values",
          Has(migration, "ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant')"));
    Check("the disenchant column holds a 32-bit item GUID, as ObjectGuid::LowType does",
          Has(migration, "`disenchant_guid` INT UNSIGNED"));
    Check("standing enchant columns exist",
          Has(migration, "enchant_spell") && Has(migration, "enchant_slot"));
}

}  // namespace

int main()
{
    TheItemGuidIsBounded();
    AnErrandEndsOnTheReadBack();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::puts("ok test_enchanting_drives");
    return 0;
}
