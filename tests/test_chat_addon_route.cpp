/*
 * Which group chat lines are speech, and which are addressed to an addon.
 *
 * WHAT IT IS FOR (#269). A status line meant for an in-game addon had only one
 * way out of this module: `channel='party'`, the same route the council, the
 * trades and the crafting requests take. So a tab separated machine payload
 * was drawn in a chat frame, on every stream, and the only defence was the
 * addon's own chat filter on the clients that happened to have it. The sender
 * was left unscheduled rather than run.
 *
 * 3.3.5a already has the right route. LANG_ADDON on a group channel is not a
 * language, it is the transport SendAddonMessage uses: the receiving client
 * hands the packet to CHAT_MSG_ADDON and no chat frame draws it. Choosing it is
 * one argument in DoChat. Choosing it FOR THE RIGHT ROWS is what is pinned
 * here.
 *
 * WHAT IS PINNED, and why each case is worth having:
 *
 *   - SPEECH IS NOT REROUTED. `party` and `raid` still mean what they meant.
 *     This is the whole risk of the change: silently moving every group line
 *     onto the addon language would not make machine text quieter, it would
 *     make every word the family says invisible on five streams at once, which
 *     is strictly worse than the thing being fixed.
 *   - THE ADDON ROUTES CARRY THE SAME PACKET. `party_addon` is party, and
 *     `raid_addon` is raid. The addon flag rides alongside the channel rather
 *     than replacing it, because the chat type still decides who receives it.
 *   - AN UNKNOWN TOKEN IS NOT A GROUP CHANNEL. The caller rejects what this
 *     does not recognise by name, and that rejection is what makes a module too
 *     old to know these tokens mark the row an error instead of delivering the
 *     payload as speech. A near miss is not a hit: `_addon`, `party_addonx`
 *     and a differently cased spelling are all nothing.
 *   - ADDON IMPLIES GROUP. Nothing outside a group channel can ask for the
 *     addon language, because nothing outside a group channel is allowed to
 *     carry it: the core rejects LANG_ADDON on say, yell and emote outright.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::GroupChatRoute;
using OverseerDecisions::GroupChatRouteFor;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckRoute(char const* channel, bool group, bool raid, bool addon)
{
    GroupChatRoute const route = GroupChatRouteFor(channel);
    std::string const what = std::string("'") + channel + "'";
    Check((what + " is a group channel").c_str(), route.group, group);
    Check((what + " is raid").c_str(), route.raid, raid);
    Check((what + " rides the addon language").c_str(), route.addon, addon);
}

// Every channel token this module has ever accepted for kind='chat', plus the
// two the addon route adds. Listed in one place so a case below can say "and
// nothing else" and mean it.
char const* const NOT_A_GROUP_CHANNEL[] = {
    // The other channels DoChat handles, which have their own branches.
    "say", "yell", "emote", "whisper", "guild", "officer",
    // Never a channel here at all: public channels are watched, not spoken to.
    "channel",
    // The empty default the column carries for a kind='bot' row.
    "",
    // Near misses. The suffix is not parsed off, so none of these is a group
    // channel and none of them is party chat either.
    "_addon", "addon", "party_addonx", "raid_addonx", "partyaddon",
    "party_", "party addon", "PARTY_ADDON", "Party_Addon", " party_addon",
    "party_addon ", "party_addon\t",
};

// ------------------------------------------------------------------------

// THE ONE THE WHOLE CHANGE IS RISKED ON. Speech keeps going out as speech.
// The council, a trade and a crafting request are all written to be read, and
// a version of this that quietly moved them onto the addon language would take
// every visible word off five streams at once while looking like a fix.
void SpeechIsNotRerouted()
{
    CheckRoute("party", /*group=*/true, /*raid=*/false, /*addon=*/false);
    CheckRoute("raid", /*group=*/true, /*raid=*/true, /*addon=*/false);
}

// The new routes are the old ones in the language nothing renders. Party stays
// party: the chat type still decides who receives the packet, so the addon
// flag has to ride alongside the channel rather than stand in for it.
void TheAddonRoutesCarryTheSamePacket()
{
    CheckRoute("party_addon", /*group=*/true, /*raid=*/false, /*addon=*/true);
    CheckRoute("raid_addon", /*group=*/true, /*raid=*/true, /*addon=*/true);
}

// A token this does not recognise stays unrecognised, so DoChat answers
// "unknown chat channel" and marks the row an error. That is the honest
// failure for a module reading a row written by a newer sender, and it is why
// the tokens are listed rather than parsed: stripping an `_addon` suffix would
// have accepted every near miss below as party chat.
void AnUnknownTokenIsNotAGroupChannel()
{
    for (char const* channel : NOT_A_GROUP_CHANNEL)
        CheckRoute(channel, /*group=*/false, /*raid=*/false, /*addon=*/false);
}

// Nothing can ask for the addon language without being a group channel. The
// core enforces the same thing from the other end - LANG_ADDON on say, yell or
// emote is rejected as an invalid combination - so a route that answered
// otherwise would be describing a packet the server would refuse to accept
// back.
void AddonImpliesGroup()
{
    char const* const EVERY_TOKEN[] = {
        "party", "raid", "party_addon", "raid_addon",
        "say", "yell", "emote", "whisper", "guild", "officer", "channel", "",
        "_addon", "addon", "party_addonx", "PARTY_ADDON",
    };
    for (char const* channel : EVERY_TOKEN)
    {
        GroupChatRoute const route = GroupChatRouteFor(channel);
        if (route.addon && !route.group)
        {
            std::printf("FAIL '%s' rides the addon language without being a group channel\n",
                        channel);
            ++failures;
        }
        // The mirror of the same rule: a route that is not a group channel
        // decides nothing at all, so neither of the other two flags may be
        // set on it.
        if (!route.group && (route.raid || route.addon))
        {
            std::printf("FAIL '%s' is not a group channel but still carries flags\n", channel);
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    SpeechIsNotRerouted();
    TheAddonRoutesCarryTheSamePacket();
    AnUnknownTokenIsNotAGroupChannel();
    AddonImpliesGroup();

    if (failures != 0)
    {
        std::printf("%d group chat route check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("speech is speech and an addon line is not\n");
    return EXIT_SUCCESS;
}
