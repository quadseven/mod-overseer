/*
 * Vendor trades, gold changes and long buffs for guild members
 * (mod-overseer#960).
 *
 * overseer_event logged item_loot, quest_reward, craft, item_given, learn,
 * level_up, item_equip, tidy, town_trip, repair and death, but nothing for
 * vendor buys, vendor sales, gold changes or auras gained, so a gold or buff
 * grant was invisible. This file pins the gates and the row shapes that close
 * the gap:
 *
 *   - a vendor buy names the item, the count and the cost,
 *   - a vendor sale names the item, the count and the price,
 *   - a gold change names its source (loot, quest, sale, mail, trade, repair,
 *     buy, training, other) and the signed amount, and a change with no known
 *     source is recorded as "other" rather than dropped,
 *   - an aura gained is recorded only when it is a buff lasting longer than
 *     60 seconds, with the caster named (self, party, player, creature, none).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::AuraCaster;
using OverseerDecisions::AuraCasterName;
using OverseerDecisions::AuraGainedFields;
using OverseerDecisions::GoldChangeFields;
using OverseerDecisions::GoldSource;
using OverseerDecisions::GoldSourceName;
using OverseerDecisions::ItemStoryWho;
using OverseerDecisions::OverseerEventFields;
using OverseerDecisions::ResolveGoldSource;
using OverseerDecisions::ShouldRecordAuraGained;
using OverseerDecisions::ShouldRecordGoldChange;
using OverseerDecisions::ShouldRecordVendorTrade;
using OverseerDecisions::VendorBuyFields;
using OverseerDecisions::VendorSaleFields;

namespace
{

int failures = 0;

void CheckTrue(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckFalse(char const* what, bool got)
{
    CheckTrue(what, !got);
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got [%s], wanted [%s]\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckSource(char const* what, GoldSource got, GoldSource want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what,
                static_cast<unsigned>(got), static_cast<unsigned>(want));
    ++failures;
}

void AVendorBuyNamesTheItemCountAndCost()
{
    OverseerEventFields fields = VendorBuyFields(1234, "Dwarven Mild", 3, 1200);
    CheckText("buy kind", fields.kind, "vendor_buy");
    CheckTrue("buy entry", fields.subjectId == 1234);
    CheckText("buy name", fields.subjectName, "Dwarven Mild");
    CheckText("buy detail", fields.detail, "3 for 1200 copper");
    CheckText("buy source column empty", fields.source, "");
}

void AVendorSaleNamesTheItemCountAndPrice()
{
    OverseerEventFields fields = VendorSaleFields(5678, "Worn Dagger", 1, 45);
    CheckText("sale kind", fields.kind, "vendor_sale");
    CheckTrue("sale entry", fields.subjectId == 5678);
    CheckText("sale name", fields.subjectName, "Worn Dagger");
    CheckText("sale detail", fields.detail, "1 for 45 copper");
    CheckText("sale source column empty", fields.source, "");
}

void VendorTradesAreWrittenForGuildMembersOnly()
{
    CheckTrue("roster trades", ShouldRecordVendorTrade(ItemStoryWho::Roster));
    CheckTrue("guild member trades", ShouldRecordVendorTrade(ItemStoryWho::GuildMember));
    CheckFalse("stranger trades", ShouldRecordVendorTrade(ItemStoryWho::Stranger));
}

void ALootedGoldChangeNamesItsSource()
{
    OverseerEventFields fields = GoldChangeFields(52, GoldSource::Loot);
    CheckText("gold kind", fields.kind, "gold_change");
    CheckText("gold source named", fields.subjectName, "loot");
    CheckText("gold detail", fields.detail, "+52 copper");
    CheckTrue("gold subject splits per source",
              fields.subjectId == static_cast<unsigned>(GoldSource::Loot));
}

void AQuestGoldChangeNamesItsSource()
{
    OverseerEventFields fields = GoldChangeFields(1250, GoldSource::Quest);
    CheckText("quest source named", fields.subjectName, "quest");
    CheckText("quest detail", fields.detail, "+1250 copper");
}

void ASpendingGoldChangeNamesItsSource()
{
    OverseerEventFields fields = GoldChangeFields(-103, GoldSource::Repair);
    CheckText("repair source named", fields.subjectName, "repair");
    CheckText("repair detail signed", fields.detail, "-103 copper");
}

void AGoldChangeWithNoKnownSourceIsOther()
{
    CheckSource("null note is other", ResolveGoldSource(nullptr), GoldSource::Other);
    CheckSource("empty note is other", ResolveGoldSource(""), GoldSource::Other);
    CheckSource("unknown word is other", ResolveGoldSource("gm"), GoldSource::Other);
    CheckSource("loot note is loot", ResolveGoldSource("loot"), GoldSource::Loot);
    CheckSource("buy note is buy", ResolveGoldSource("buy"), GoldSource::Buy);

    OverseerEventFields fields = GoldChangeFields(7, ResolveGoldSource(nullptr));
    CheckText("other gold kind", fields.kind, "gold_change");
    CheckText("other gold source named", fields.subjectName, "other");
    CheckText("other gold detail", fields.detail, "+7 copper");
}

void TheSourcesHaveStableWords()
{
    CheckText("loot", GoldSourceName(GoldSource::Loot), "loot");
    CheckText("quest", GoldSourceName(GoldSource::Quest), "quest");
    CheckText("sale", GoldSourceName(GoldSource::Sale), "sale");
    CheckText("mail", GoldSourceName(GoldSource::Mail), "mail");
    CheckText("trade", GoldSourceName(GoldSource::Trade), "trade");
    CheckText("repair", GoldSourceName(GoldSource::Repair), "repair");
    CheckText("buy", GoldSourceName(GoldSource::Buy), "buy");
    CheckText("training", GoldSourceName(GoldSource::Training), "training");
    CheckText("other", GoldSourceName(GoldSource::Other), "other");
}

void AZeroDeltaIsNotAnEvent()
{
    CheckFalse("zero delta", ShouldRecordGoldChange(0));
    CheckTrue("gain", ShouldRecordGoldChange(1));
    CheckTrue("loss", ShouldRecordGoldChange(-1));
}

void ASelfCastBuffProducesOneEventWithTheCasterNamed()
{
    CheckTrue("long self buff", ShouldRecordAuraGained(3600, true));
    CheckFalse("short buff", ShouldRecordAuraGained(30, true));
    CheckFalse("exactly sixty seconds is not long", ShouldRecordAuraGained(60, true));
    CheckFalse("long debuff is not a buff", ShouldRecordAuraGained(3600, false));

    OverseerEventFields fields = AuraGainedFields(133, "Fireball", 3600, AuraCaster::Self);
    CheckText("aura kind", fields.kind, "aura_gained");
    CheckTrue("aura spell id", fields.subjectId == 133);
    CheckText("aura name", fields.subjectName, "Fireball");
    CheckText("aura detail", fields.detail, "3600 seconds");
    CheckText("aura caster named", fields.source, "self");
}

void TheCastersHaveStableWords()
{
    CheckText("self", AuraCasterName(AuraCaster::Self), "self");
    CheckText("party", AuraCasterName(AuraCaster::Party), "party");
    CheckText("player", AuraCasterName(AuraCaster::Player), "player");
    CheckText("creature", AuraCasterName(AuraCaster::Creature), "creature");
    CheckText("none", AuraCasterName(AuraCaster::None), "none");

    OverseerEventFields fields = AuraGainedFields(1978, "Serpent Sting", 7200, AuraCaster::None);
    CheckText("unknown caster is none", fields.source, "none");
}

}  // namespace

int main()
{
    AVendorBuyNamesTheItemCountAndCost();
    AVendorSaleNamesTheItemCountAndPrice();
    VendorTradesAreWrittenForGuildMembersOnly();
    ALootedGoldChangeNamesItsSource();
    AQuestGoldChangeNamesItsSource();
    ASpendingGoldChangeNamesItsSource();
    AGoldChangeWithNoKnownSourceIsOther();
    TheSourcesHaveStableWords();
    AZeroDeltaIsNotAnEvent();
    ASelfCastBuffProducesOneEventWithTheCasterNamed();
    TheCastersHaveStableWords();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: vendor trades, gold changes and long buffs are recorded with their source\n");
    return EXIT_SUCCESS;
}
