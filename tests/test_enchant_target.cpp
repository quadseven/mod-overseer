/*
 * The item an Enchanting rung is cast on, and the item a disenchant row
 * names (#1523).
 *
 * Every Enchanting skill-up rung past the rods is SPELL_EFFECT_ENCHANT_ITEM,
 * which the core refuses with SPELL_FAILED_ITEM_NOT_FOUND unless the cast
 * names an item, and which overwrites that item's permanent enchant. The
 * craft drive cast every recipe at the crafter itself, so every such rung was
 * refused on every poll. The pick below is what the drive now asks, and the
 * source checks at the end pin that it does.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::DisenchantTarget;
using OverseerDecisions::EnchantRanks;
using OverseerDecisions::EnchantSpellFacts;
using OverseerDecisions::EnchantTargetCandidate;
using OverseerDecisions::NamedDisenchantFacts;
using OverseerDecisions::ParseDisenchantTarget;
using OverseerDecisions::PickEnchantTarget;

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

EnchantTargetCandidate Bag(uint32_t quality = 2)
{
    EnchantTargetCandidate c;
    c.fitsSpell = true;
    c.levelOk = true;
    c.quality = quality;
    return c;
}

EnchantTargetCandidate Worn(uint32_t permEnchant)
{
    EnchantTargetCandidate c = Bag(3);
    c.equipped = true;
    c.permEnchant = permEnchant;
    return c;
}

// Enchant Bracer - Minor Health: enchant 41, rank 1 in the ladder used here.
EnchantSpellFacts const MinorHealth{41, 1};
// 66 stands for a lower rung's enchant, 1883 for a far better one, 1843 for an
// armor kit (no Enchanting source, so absent from the ranks).
EnchantRanks const Ranks{{41, 1}, {66, 1}, {1883, 270}};

void TheBagComesFirst()
{
    std::vector<EnchantTargetCandidate> const items{Worn(0), Bag()};
    Check("a plain bag item is taken before worn gear",
          PickEnchantTarget(MinorHealth, items, Ranks) == 1);
}

void TheLowestQualityBagItemWins()
{
    std::vector<EnchantTargetCandidate> const items{Bag(2), Bag(1), Bag(0)};
    Check("the poorest bag item is spent first",
          PickEnchantTarget(MinorHealth, items, Ranks) == 2);
}

void BagFilters()
{
    EnchantTargetCandidate kept = Bag();
    kept.kept = true;
    EnchantTargetCandidate rare = Bag(3);
    EnchantTargetCandidate quest = Bag(1);
    quest.questItem = true;
    EnchantTargetCandidate unfit = Bag();
    unfit.fitsSpell = false;
    EnchantTargetCandidate low = Bag();
    low.levelOk = false;
    std::vector<EnchantTargetCandidate> const items{kept, rare, quest, unfit, low};
    Check("kept, rare, quest, unfitting and under-level bag items are never chosen",
          PickEnchantTarget(MinorHealth, items, Ranks) == -1);
}

void WornOnlyWhenNothingBetterIsLost()
{
    Check("an empty worn slot takes the rung",
          PickEnchantTarget(MinorHealth, {Worn(0)}, Ranks) == 0);
    Check("a worn item already holding this enchant takes it again",
          PickEnchantTarget(MinorHealth, {Worn(41)}, Ranks) == 0);
    Check("a worn lower-or-equal Enchanting enchant may be written over",
          PickEnchantTarget(MinorHealth, {Worn(66)}, Ranks) == 0);
    Check("a better Enchanting enchant on worn gear is never replaced",
          PickEnchantTarget(MinorHealth, {Worn(1883)}, Ranks) == -1);
    Check("an enchant with no Enchanting source is never replaced",
          PickEnchantTarget(MinorHealth, {Worn(1843)}, Ranks) == -1);
    Check("empty worn gear is taken before an overwrite",
          PickEnchantTarget(MinorHealth, {Worn(66), Worn(0)}, Ranks) == 1);
    EnchantTargetCandidate unfit = Worn(0);
    unfit.fitsSpell = false;
    EnchantTargetCandidate low = Worn(0);
    low.levelOk = false;
    Check("worn gear still has to fit the spell and pass the level gate",
          PickEnchantTarget(MinorHealth, {unfit, low}, Ranks) == -1);
}

void AnUnknownRankOverwritesNothing()
{
    EnchantRanks const zero{{66, 0}};
    Check("an enchant whose source rank is 0 counts as unknown",
          PickEnchantTarget(MinorHealth, {Worn(66)}, zero) == -1);
    EnchantSpellFacts const unranked{41, 0};
    Check("a spell of unknown rank overwrites nothing",
          PickEnchantTarget(unranked, {Worn(66)}, Ranks) == -1);
    Check("but still takes an empty slot",
          PickEnchantTarget(unranked, {Worn(0)}, Ranks) == 0);
}

void NoCandidates()
{
    Check("nothing held is -1", PickEnchantTarget(MinorHealth, {}, Ranks) == -1);
}

void TheNamedRow()
{
    DisenchantTarget t = ParseDisenchantTarget("guid:123 count:1");
    Check("guid and count:1 name one item", t.named && !t.malformed && t.guid == 123);
    t = ParseDisenchantTarget("guid:77");
    Check("a bare guid names one item", t.named && !t.malformed && t.guid == 77);
    t = ParseDisenchantTarget("  guid:9   count:1 ");
    Check("whitespace is tolerated", t.named && !t.malformed && t.guid == 9);
    t = ParseDisenchantTarget("");
    Check("an empty command is the unnamed form", !t.named && !t.malformed);
    t = ParseDisenchantTarget("disenchant a spare green");
    Check("free text without a guid is the unnamed form", !t.named && !t.malformed);
    Check("guid:0 is malformed", ParseDisenchantTarget("guid:0").malformed);
    Check("guid:abc is malformed", ParseDisenchantTarget("guid:abc").malformed);
    Check("an overflowing guid is malformed",
          ParseDisenchantTarget("guid:99999999999").malformed);
    Check("a count other than 1 is malformed",
          ParseDisenchantTarget("guid:5 count:2").malformed);
    Check("a third token is malformed",
          ParseDisenchantTarget("guid:5 count:1 extra").malformed);
    Check("two guids are malformed", ParseDisenchantTarget("guid:5 guid:6").malformed);
}

NamedDisenchantFacts Disenchantable()
{
    NamedDisenchantFacts f;
    f.found = true;
    f.inBags = true;
    f.quality = 2;
    f.weaponOrArmor = true;
    f.disenchantId = 1;
    f.requiredSkill = 1;
    f.skill = 1;
    return f;
}

using OverseerDecisions::JudgeNamedDisenchant;
using OverseerDecisions::NAMED_DISENCHANT_ARRIVAL_SECONDS;
using OverseerDecisions::NamedDisenchantStep;

bool Is(NamedDisenchantFacts const& f, int64_t age, NamedDisenchantStep step, char const* words)
{
    OverseerDecisions::NamedDisenchantVerdict const v = JudgeNamedDisenchant(f, age);
    return v.step == step && std::string(v.reason).find(words) != std::string::npos;
}

void TheNamedItem()
{
    int64_t const young = 60;
    int64_t const late = NAMED_DISENCHANT_ARRIVAL_SECONDS;
    Check("an owned, bagged, disenchantable green may go",
          Is(Disenchantable(), young, NamedDisenchantStep::Cast, ""));
    NamedDisenchantFacts f = Disenchantable();
    f.quality = 4;
    Check("an epic the writer named may go", Is(f, young, NamedDisenchantStep::Cast, ""));
    f = Disenchantable();
    f.found = false;
    Check("an item not owned yet is held while the give is young",
          Is(f, young, NamedDisenchantStep::Hold, "named item not owned yet"));
    Check("and ends once the arrival window is spent",
          Is(f, late, NamedDisenchantStep::End, "named item never reached the enchanter"));
    Check("the window is fifteen minutes", NAMED_DISENCHANT_ARRIVAL_SECONDS == 900 &&
          Is(f, late - 1, NamedDisenchantStep::Hold, "not owned yet"));
    f = Disenchantable();
    f.inBags = false;
    Check("worn gear is held as not in the bags",
          Is(f, young, NamedDisenchantStep::Hold, "not in the bags"));
    Check("and ends once the window is spent",
          Is(f, late, NamedDisenchantStep::End, "never reached the enchanter's bags"));
    f = Disenchantable();
    f.kept = true;
    Check("a kept item ends the row at once", Is(f, young, NamedDisenchantStep::End, "kept"));
    f = Disenchantable();
    f.requiredSkill = 25;
    f.skill = 1;
    Check("too little Enchanting holds, since skill rises",
          Is(f, late * 10, NamedDisenchantStep::Hold, "skill too low"));
    char const* const notDe = "not disenchantable";
    f = Disenchantable();
    f.quality = 1;
    Check("a white item ends the row", Is(f, young, NamedDisenchantStep::End, notDe));
    f = Disenchantable();
    f.quality = 5;
    Check("a legendary ends the row", Is(f, young, NamedDisenchantStep::End, notDe));
    f = Disenchantable();
    f.weaponOrArmor = false;
    Check("a trade good ends the row", Is(f, young, NamedDisenchantStep::End, notDe));
    f = Disenchantable();
    f.disenchantId = 0;
    Check("no DisenchantID ends the row", Is(f, young, NamedDisenchantStep::End, notDe));
    f = Disenchantable();
    f.requiredSkill = -1;
    Check("a negative required skill ends the row", Is(f, young, NamedDisenchantStep::End, notDe));
}

std::string Body(std::string const& source, char const* head)
{
    std::size_t const start = source.find(head);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = source.find("\n    void ", start + 1);
    return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    std::string const craft = Body(source, "void DriveCraft()");
    std::size_t const station = craft.find("CraftStationStep::Defer");
    std::size_t const pick = craft.find("EnchantTargetFor(bot, info, spellId)");
    std::size_t const target = craft.find("targets.SetItemTarget(enchantTarget)");
    std::size_t const plain = craft.find("bot->CastSpell(bot, spellId, false)");
    Check("DriveCraft picks an enchant target after the station step",
          station != std::string::npos && pick != std::string::npos && station < pick);
    std::string const helper = Body(source, "Item* EnchantTargetFor(Player* bot,");
    Check("and the pick is the pure decision",
          helper.find("OverseerDecisions::PickEnchantTarget(spell, candidates, ranks)") !=
              std::string::npos);
    Check("fed the core's fit and item-level facts",
          helper.find("IsFitToSpellRequirements(info)") != std::string::npos &&
              helper.find("SPELL_ATTR2_ALLOW_LOW_LEVEL_BUFF") != std::string::npos &&
              helper.find("GetEnchantmentId(PERM_ENCHANTMENT_SLOT)") != std::string::npos);
    Check("and casts an enchant-item recipe with that item as its target",
          target != std::string::npos && pick < target);
    Check("other recipes still cast the way they did", plain != std::string::npos);
    Check("no target is a craft-deferred said once",
          craft.find("SayCraftPrepOnce(name, \"no item to enchant\")") != std::string::npos &&
              craft.find("RecordEvent(bot, \"craft-deferred\"") != std::string::npos);
    std::size_t const deferNoItem = craft.find("\"no item to enchant\"");
    std::size_t const clear = craft.find("ClearCraftAim(name)", deferNoItem);
    std::size_t const castAt = craft.find("SpellCastResult const result", deferNoItem);
    Check("and does not clear the errand",
          deferNoItem != std::string::npos && (clear == std::string::npos || clear > castAt));

    std::string const de = Body(source, "void DriveDisenchant()");
    Check("DriveDisenchant gates on MayCraftNow",
          de.find("OverseerDecisions::MayCraftNow(task.job, InAnyDungeonRun(player),") !=
              std::string::npos);
    Check("and no longer asks for job craft alone",
          de.find("task.job != \"craft\"") == std::string::npos);
    Check("a named row is parsed from the command text",
          source.find("OverseerDecisions::ParseDisenchantTarget(") != std::string::npos);
    Check("a named item is judged by the pure verdict, on the row's age",
          de.find("OverseerDecisions::JudgeNamedDisenchant(facts, task.ageSeconds)") !=
              std::string::npos);
    Check("an ended named row is written as error",
          source.find("void EndDisenchant(") != std::string::npos &&
              de.find("EndDisenchant(id, task,") != std::string::npos);

    std::ifstream storeIn("src/overseer_disenchant_store.h");
    std::stringstream storeText;
    storeText << storeIn.rdbuf();
    std::string const store = storeText.str();
    Check("Pending no longer requires job craft",
          store.find("r.job = 'craft'") == std::string::npos);
    Check("Owned carries the command text and the row's age",
          store.find("COALESCE(c.command, '')") != std::string::npos &&
              store.find("UNIX_TIMESTAMP(c.created_at)") != std::string::npos);
    Check("an ended row is status error",
          store.find("SET status = 'error', result = ?") != std::string::npos);
}

}  // namespace

int main()
{
    TheBagComesFirst();
    TheLowestQualityBagItemWins();
    BagFilters();
    WornOnlyWhenNothingBetterIsLost();
    AnUnknownRankOverwritesNothing();
    NoCandidates();
    TheNamedRow();
    TheNamedItem();
    TheAdapterIsWired();
    return failures ? 1 : 0;
}
