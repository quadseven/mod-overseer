/*
 * Guild chat hears what a member did or needs, not filler and not
 * invented reputation claims.
 *
 * The live failure this pins (the relayed guild feed, one evening in
 * October 2026): the guild channel carried modern-slang filler, a
 * recruitment line ("<spec link> priest is looking for guild") from a
 * bot already in the guild, and reputation boasts for factions the
 * speaker had no standing with. Traced in the guild-chat realism ticket:
 *
 *   - Every template broadcast in the playerbots tree goes through
 *     BroadcastHelper::BroadcastToChannelWithGlobalChance
 *     (src/Util/BroadcastHelper.cpp), which tries each channel in a
 *     per-broadcast list, guild included if a roll passes
 *     AiPlayerbot.BroadcastToGuildGlobalChance.
 *   - BroadcastSuggestSomething, BroadcastSuggestSomethingToxic and
 *     BroadcastSuggestToxicLinks are pure suggestion filler - 134, 102
 *     and 2 rows of ai_playerbot_texts about nothing the speaker just
 *     did - and each listed {TO_GUILD, 10}. The recruitment line is a
 *     suggest_something row ("%my_role is looking for guild.", text
 *     id 741 at the pin); %my_role embeds the premade spec link, which
 *     is the fragmenting markup in the feed.
 *   - BroadcastSuggestGrindReputation chooses %faction and %rep_level
 *     at random - neither is read from the speaker's standings - and
 *     broadcast suggest_faction with {TO_GUILD, 50} in its list.
 *
 * Patch 0017 removes the guild entry from those four broadcasts and
 * nothing else. Broadcasts grounded in the speaker's own activity
 * (loot, quest progress, kills, level-ups) and genuine asks
 * (suggest_instance, suggest_quest, suggest_trade) keep their guild
 * entry: a guild hearing what its members did and need is the point.
 *
 * Compiled with nothing but the standard library; reads the patch as
 * text (run from the repo root).
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void ThePatchExistsAndIsDocumented()
{
    std::string const patch =
        Read("patches/mod-playerbots/0017-guild-chat-skips-filler-and-invented-rep.patch");
    Check("the patch exists", !patch.empty());
    Check("it patches the broadcast helper",
          patch.find("+++ b/src/Util/BroadcastHelper.cpp") != std::string::npos);
    Check("it says why", patch.find("WHY THIS PATCH EXISTS") != std::string::npos);
    Check("it says what would retire it",
          patch.find("WHAT WOULD LET THIS PATCH BE DELETED") != std::string::npos);
    Check("it names the tree it applies to", patch.find("APPLIES TO") != std::string::npos);
}

void FillerLosesTheGuildChannel()
{
    std::string const patch =
        Read("patches/mod-playerbots/0017-guild-chat-skips-filler-and-invented-rep.patch");
    // The three filler broadcasts and the invented-reputation broadcast
    // each appear as a removed TO_GUILD entry beside their own text class.
    Check("suggest_something loses guild",
          patch.find("PlayerbotTextMgr::instance().GetBotText(\"suggest_something\", placeholders)") !=
                  std::string::npos &&
              patch.find("-            { {TO_GUILD, 10}, {TO_WORLD, 70}, {TO_GENERAL, 100} }") !=
                  std::string::npos);
    Check("the filler lists keep world and general",
          patch.find("+            { {TO_WORLD, 70}, {TO_GENERAL, 100} }") != std::string::npos);
    Check("suggest_faction loses guild",
          patch.find("PlayerbotTextMgr::instance().GetBotText(\"suggest_faction\", placeholders)") !=
                  std::string::npos &&
              patch.find("-            { {TO_LOOKING_FOR_GROUP, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} }") !=
                  std::string::npos);
    Check("the rep list keeps its other channels",
          patch.find("+            { {TO_LOOKING_FOR_GROUP, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} }") !=
              std::string::npos);
}

void GroundedBroadcastsKeepTheGuildChannel()
{
    std::string const patch =
        Read("patches/mod-playerbots/0017-guild-chat-skips-filler-and-invented-rep.patch");
    // No grounded or genuine-ask broadcast may lose guild: their channel
    // lists are untouched, so their text classes never appear as a
    // removed line's neighbour in the patch.
    for (char const* grounded :
         {"\"broadcast_looting_item_poor\"", "\"broadcast_quest_turned_in\"",
          "\"suggest_instance\"", "\"suggest_quest\"", "\"suggest_trade\""})
    {
        std::string const removed = std::string("-            PlayerbotTextMgr::instance().GetBotText(") +
                                    grounded;
        Check(grounded, patch.find(removed) == std::string::npos);
    }
    // The guild global chance gate itself is not the fix and stays.
    Check("the guild global chance gate is untouched",
          patch.find("BroadcastToGuildGlobalChance") == std::string::npos ||
              patch.find("-                    && broadcastRoll <= sPlayerbotAIConfig.BroadcastToGuildGlobalChance") ==
                  std::string::npos);
}

}  // namespace

int main()
{
    ThePatchExistsAndIsDocumented();
    FillerLosesTheGuildChannel();
    GroundedBroadcastsKeepTheGuildChannel();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
