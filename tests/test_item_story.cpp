/*
 * The story of a notable item: who looted it, who it went to, and when it went
 * on (mod-overseer#567).
 *
 * overseer_event had no loot event and no hand-over event, and wrote
 * item_equip for roster characters only, so an epic looted by a guild member
 * and handed to a roster character left one row out of three. This file pins
 * the gates that decide which of those moments are written:
 *
 *   - only rare (quality 3) and better is notable, for loot and hand-overs,
 *   - a roster character still gets every equip it always got,
 *   - a guild member's equip is written only for an item the record already
 *     knows was looted or handed over, so factory-issued bot gear stays out,
 *   - a stranger is never written,
 *   - and the sentences a person reads name what happened.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ClassifyItemStoryCharacter;
using OverseerDecisions::IsNotableItemQuality;
using OverseerDecisions::ItemGivenDetail;
using OverseerDecisions::ItemLootDetail;
using OverseerDecisions::ItemStoryBook;
using OverseerDecisions::ItemStoryWho;
using OverseerDecisions::RememberStoryItem;
using OverseerDecisions::ShouldRecordItemEquip;
using OverseerDecisions::ShouldRecordItemGiven;
using OverseerDecisions::ShouldRecordItemLoot;
using OverseerDecisions::StoryKnowsItem;
namespace ItemVia = OverseerDecisions::ItemVia;

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

void CheckWho(char const* what, ItemStoryWho got, ItemStoryWho want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, static_cast<int>(got),
                static_cast<int>(want));
    ++failures;
}

std::vector<unsigned> const kGuilds{23, 24};

void RareAndUpAreNotableAndUncommonIsNot()
{
    CheckFalse("poor", IsNotableItemQuality(0));
    CheckFalse("uncommon", IsNotableItemQuality(2));
    CheckTrue("rare", IsNotableItemQuality(3));
    CheckTrue("epic", IsNotableItemQuality(4));
    CheckTrue("legendary", IsNotableItemQuality(5));
}

void MembershipComesFromTheRosterGuilds()
{
    CheckWho("roster outranks guild", ClassifyItemStoryCharacter(true, 23, kGuilds),
             ItemStoryWho::Roster);
    CheckWho("roster with no guild", ClassifyItemStoryCharacter(true, 0, kGuilds),
             ItemStoryWho::Roster);
    CheckWho("member of the second guild", ClassifyItemStoryCharacter(false, 24, kGuilds),
             ItemStoryWho::GuildMember);
    CheckWho("member of some other guild", ClassifyItemStoryCharacter(false, 7, kGuilds),
             ItemStoryWho::Stranger);
    CheckWho("no guild", ClassifyItemStoryCharacter(false, 0, kGuilds), ItemStoryWho::Stranger);
    CheckWho("guild 0 is never a story guild",
             ClassifyItemStoryCharacter(false, 0, std::vector<unsigned>{0, 23}),
             ItemStoryWho::Stranger);
    CheckWho("no roster guilds at all", ClassifyItemStoryCharacter(false, 23, {}),
             ItemStoryWho::Stranger);
}

void LootIsWrittenForTheGuildsAndNotableOnly()
{
    CheckTrue("guild member loots an epic", ShouldRecordItemLoot(4, ItemStoryWho::GuildMember));
    CheckTrue("roster loots a rare", ShouldRecordItemLoot(3, ItemStoryWho::Roster));
    CheckFalse("guild member loots a green", ShouldRecordItemLoot(2, ItemStoryWho::GuildMember));
    CheckFalse("stranger loots an epic", ShouldRecordItemLoot(4, ItemStoryWho::Stranger));
}

void AHandOverNeedsOneSideInTheRecord()
{
    CheckTrue("member to roster", ShouldRecordItemGiven(4, ItemStoryWho::GuildMember,
                                                        ItemStoryWho::Roster));
    CheckTrue("stranger to member", ShouldRecordItemGiven(3, ItemStoryWho::Stranger,
                                                          ItemStoryWho::GuildMember));
    CheckFalse("stranger to stranger", ShouldRecordItemGiven(4, ItemStoryWho::Stranger,
                                                             ItemStoryWho::Stranger));
    CheckFalse("a green between roster characters",
               ShouldRecordItemGiven(2, ItemStoryWho::Roster, ItemStoryWho::Roster));
}

void EquipKeepsTheRosterAndGatesTheGuildOnTheStory()
{
    // Unchanged from before #567: every roster equip, any quality.
    CheckTrue("roster equips a green", ShouldRecordItemEquip(2, ItemStoryWho::Roster, false));
    CheckTrue("roster equips an epic", ShouldRecordItemEquip(4, ItemStoryWho::Roster, false));

    // The factory-gear flood this gate exists to keep out.
    CheckFalse("guild member equips issued rare gear",
               ShouldRecordItemEquip(3, ItemStoryWho::GuildMember, false));
    CheckTrue("guild member equips the epic it looted",
              ShouldRecordItemEquip(4, ItemStoryWho::GuildMember, true));
    CheckFalse("guild member equips a known green",
               ShouldRecordItemEquip(2, ItemStoryWho::GuildMember, true));
    CheckFalse("stranger", ShouldRecordItemEquip(4, ItemStoryWho::Stranger, true));
}

void TheSentencesNameWhatHappened()
{
    CheckText("give", ItemGivenDetail(ItemVia::Give, "Grog", ""), "given to Grog");
    CheckText("trade", ItemGivenDetail(ItemVia::Trade, "Grog", ""), "traded to Grog");
    CheckText("mail with box", ItemGivenDetail(ItemVia::Mail, "Grog", "Mailbox"),
              "mailed to Grog from Mailbox");
    CheckText("mail without box", ItemGivenDetail(ItemVia::Mail, "Grog", ""), "mailed to Grog");
    CheckText("a mailbox is not named for a trade",
              ItemGivenDetail(ItemVia::Trade, "Grog", "Mailbox"), "traded to Grog");

    CheckText("loot", ItemLootDetail(ItemVia::Loot, "Defias Pillager"),
              "looted from Defias Pillager");
    CheckText("need", ItemLootDetail(ItemVia::Need, "Edwin VanCleef"),
              "won on a need roll from Edwin VanCleef");
    CheckText("greed", ItemLootDetail(ItemVia::Greed, ""), "won on a greed roll");
    CheckText("unnamed source", ItemLootDetail(ItemVia::Loot, ""), "looted");
    CheckTrue("long source fits the column",
              ItemLootDetail(ItemVia::Loot, std::string(400, 'x')).size() <= 255);
}

void TheBookRemembersBoundedAndOnce()
{
    ItemStoryBook book;
    book.capacity = 3;
    CheckFalse("empty book", StoryKnowsItem(book, 647));
    RememberStoryItem(book, 647);
    CheckTrue("remembered", StoryKnowsItem(book, 647));
    RememberStoryItem(book, 647);
    CheckTrue("no duplicate", book.order.size() == 1);
    RememberStoryItem(book, 0);
    CheckTrue("0 is not a guid", book.order.size() == 1);
    CheckFalse("0 is never known", StoryKnowsItem(book, 0));

    RememberStoryItem(book, 2);
    RememberStoryItem(book, 3);
    RememberStoryItem(book, 4);
    CheckTrue("bounded", book.order.size() == 3);
    CheckFalse("oldest forgotten first", StoryKnowsItem(book, 647));
    CheckTrue("newest kept", StoryKnowsItem(book, 4));
}

}  // namespace

int main()
{
    RareAndUpAreNotableAndUncommonIsNot();
    MembershipComesFromTheRosterGuilds();
    LootIsWrittenForTheGuildsAndNotableOnly();
    AHandOverNeedsOneSideInTheRecord();
    EquipKeepsTheRosterAndGatesTheGuildOnTheStory();
    TheSentencesNameWhatHappened();
    TheBookRemembersBoundedAndOnce();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a notable item's loot, hand-over and equip are written for the families' guilds\n");
    return EXIT_SUCCESS;
}
