#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::string Read(char const* path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

void Check(char const* label, bool condition)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
}

int main()
{
    std::string const source = Read("src/mod_overseer.cpp");
    std::string const migration = Read(
        "data/sql/characters/base/2026_10_21_01_overseer_enchanting.sql");
    std::size_t const disenchantStart = source.find("void DriveDisenchant()");
    std::size_t const enchantStart = source.find("void DriveEnchant()");
    Check("both drives exist", disenchantStart != std::string::npos &&
          enchantStart != std::string::npos && disenchantStart < enchantStart);
    std::string const disenchant = source.substr(disenchantStart, enchantStart - disenchantStart);
    std::string const enchant = source.substr(enchantStart);
    Check("disenchant uses item-target cast and resolves loot",
          disenchant.find("SetItemTarget(item)") != std::string::npos &&
          disenchant.find("CastSpell(targets, DISENCHANT_SPELL, false)") != std::string::npos &&
          disenchant.find("ResolveDisenchantLoot(bot, session, itemGuid)") != std::string::npos &&
          source.find("HandleAutostoreLootItemOpcode(take)") != std::string::npos &&
          source.find("GetItemCount(entry)") != std::string::npos);
    Check("enchant prechecks reagents and reads the item back",
          enchant.find("HasItemCount") != std::string::npos &&
          enchant.find("SetItemTarget(item)") != std::string::npos &&
          enchant.find("GetEnchantmentId(PERM_ENCHANTMENT_SLOT)") != std::string::npos);
    Check("transient cast refusal leaves both errands standing",
          disenchant.find("if (cast != SPELL_CAST_OK)\n                continue;") != std::string::npos &&
          enchant.find("if (result == SPELL_CAST_OK") != std::string::npos &&
          enchant.find("enchant_spell = 0") != std::string::npos);
    Check("command enum preserves all existing values",
          migration.find("ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant')") !=
              std::string::npos);
    Check("standing enchant columns exist", migration.find("enchant_spell") != std::string::npos &&
          migration.find("enchant_slot") != std::string::npos);
    std::puts("ok test_enchanting_drives");
}
