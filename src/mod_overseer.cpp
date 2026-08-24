/*
 * mod-overseer: the bridge between the outside world and the simulation.
 *
 * Four jobs, all running inside the worldserver:
 *
 *  1. Command delivery. Rows inserted into acore_characters.overseer_command
 *     (by the Discord bridge) are carried out. Three kinds share one queue:
 *       kind='bot'  - delivered to the named bot as if whispered, via
 *                     PlayerbotAI::HandleCommand(CHAT_MSG_WHISPER, ...) with
 *                     the bot itself as the speaker - the same self-command
 *                     pattern mod-playerbots uses internally.
 *       kind='chat' - the character actually SPEAKS in the world, on the
 *                     named channel, exactly as a seated player would.
 *       kind='gm'   - a dot-command run through the character's own session,
 *                     with that account's own security level. No escalation:
 *                     a non-GM account gets the same refusal it would get by
 *                     typing the command itself.
 *
 *  2. Presence snapshots. Every few seconds the positions and vitals of every
 *     online character are written to acore_characters.overseer_snapshot,
 *     which is what the live map reads. The characters table itself only
 *     persists position on the periodic save, which is minutes stale.
 *
 *  3. Chat capture. Every line a WATCHED character can hear is written to
 *     acore_characters.overseer_chat for the bridge to relay. This is the
 *     receive half of the conversation, and the reason the relay is a chat
 *     log rather than a firehose: audibility is evaluated against the same
 *     rules the client uses, so a watcher sees what it would really see.
 *
 *  4. Retention. Chat is swept on a timer. A 500-bot world talks constantly
 *     and nothing here is worth keeping for long.
 *
 * The database is the whole interface on purpose: no listening socket, no new
 * network surface on the worldserver. Anything that can reach MySQL (which is
 * cluster-internal) can observe and command; nothing else can.
 *
 * THREADING. OnUpdate runs on the world thread. The chat hooks do NOT: bots
 * speak from Player::Say during map update, which AzerothCore may run on a
 * map-update thread. The watch list is therefore guarded by a mutex, with an
 * atomic union of watched channel kinds as a lock-free early-out so the
 * common "nobody is listening to this kind" case costs one atomic load.
 */

#include "CharacterCache.h"
#include "Chat.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "Bag.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr uint32 COMMAND_POLL_MS = 2000;
constexpr uint32 SNAPSHOT_MS = 5000;
constexpr uint32 WATCH_RELOAD_MS = 30000;
constexpr uint32 CHAT_SWEEP_MS = 300000;

// How often to check that the roster is still logged in. A login is a query
// holder plus a world-thread callback, so this is not free; 30s is far below
// any interval a viewer would notice and far above the cost.
constexpr uint32 ROSTER_POLL_MS = 30000;

// Bots logged in per pass. A cap because the first pass after a restart would
// otherwise queue every roster login into one world tick.
constexpr uint32 ROSTER_LOGINS_PER_POLL = 3;

// How often to check the roster is still one party. Cheap - a pointer compare
// per member until something is actually wrong.
constexpr uint32 PARTY_POLL_MS = 30000;

// How often the roster is checked for training it has not had yet. Slower than
// the other sweeps on purpose: the check is one indexed SELECT, but the work it
// guards walks the talent DBC and every trainer list for the class, and nothing
// is lost by a character carrying a new level for a minute before it knows what
// that level taught it.
constexpr uint32 TRAIN_POLL_MS = 60000;

// How often the traveller is pointed at a quest. Faster than training because
// it is one indexed SELECT and a quest-log walk, and because a character that
// finishes an objective should pick up the next one while the party is still
// standing there rather than a minute later.
constexpr uint32 QUEST_POLL_MS = 20000;

// The highest DBC talent tabpage. spec_tab holds a tabpage, and anything above
// this means no tree was chosen - which is a decision the module must not make
// on a named character's behalf.
constexpr uint8 MAX_TALENT_TAB = 2;
// Fast enough that a relayed conversation still feels live.
constexpr uint32 CHAT_FLUSH_MS = 1000;
// One world tick must never stall on a burst of queued commands.
constexpr uint32 COMMANDS_PER_POLL = 20;
// WoW's own chat limit, in BYTES - which is what the client sends and what
// std::string measures. Escaping can nearly double it (a backslash per quote),
// and overseer_chat.text is VARCHAR(512) CHARACTERS, so the worst case still
// fits with room to spare.
constexpr size_t MAX_CHAT_BYTES = 255;
// Relayed lines are the bridge's problem once sent; unrelayed ones are kept
// a little longer so a bridge outage does not lose the conversation.
constexpr uint32 CHAT_RETENTION_MINUTES = 180;

enum ChatKindMask : uint32
{
    KIND_NONE    = 0,
    KIND_SAY     = 1u << 0,
    KIND_YELL    = 1u << 1,
    KIND_EMOTE   = 1u << 2,
    KIND_WHISPER = 1u << 3,
    KIND_PARTY   = 1u << 4,
    KIND_RAID    = 1u << 5,
    KIND_GUILD   = 1u << 6,
    KIND_OFFICER = 1u << 7,
    KIND_CHANNEL = 1u << 8,
};

struct WatchEntry
{
    std::string name;
    uint32 channels = KIND_NONE;
};

// Identifies THIS worldserver run when claiming a command. Hex only, so it is
// safe to embed in a statement. Without it, a conditional UPDATE that some
// other process won still reads back as status='claimed', and both would
// believe they held the claim.
std::string MakeRunToken()
{
    std::random_device rd;
    std::ostringstream ss;
    ss << std::hex << rd() << rd();
    return ss.str();
}
std::string const g_runToken = MakeRunToken();

std::mutex g_watchMutex;
std::vector<WatchEntry> g_watch;
// Union of every watched entry's channels. Read without the lock on the hot
// path: a stale read only costs one needless locked pass, never a wrong one.
std::atomic<uint32> g_watchUnion{KIND_NONE};

uint32 KindFromName(std::string const& name)
{
    if (name == "say")     return KIND_SAY;
    if (name == "yell")    return KIND_YELL;
    if (name == "emote")   return KIND_EMOTE;
    if (name == "whisper") return KIND_WHISPER;
    if (name == "party")   return KIND_PARTY;
    if (name == "raid")    return KIND_RAID;
    if (name == "guild")   return KIND_GUILD;
    if (name == "officer") return KIND_OFFICER;
    return KIND_NONE;
}

char const* NameFromKind(uint32 kind)
{
    switch (kind)
    {
        case KIND_SAY:     return "say";
        case KIND_YELL:    return "yell";
        case KIND_EMOTE:   return "emote";
        case KIND_WHISPER: return "whisper";
        case KIND_PARTY:   return "party";
        case KIND_RAID:    return "raid";
        case KIND_GUILD:   return "guild";
        case KIND_OFFICER: return "officer";
        case KIND_CHANNEL: return "channel";
        default:           return "";
    }
}

// Map the wire chat type onto our coarser kinds. Leader and warning variants
// collapse into their base channel: the relay cares who could hear it, not
// which flavour of raid line it was.
uint32 KindFromChatType(uint32 type)
{
    switch (type)
    {
        case CHAT_MSG_SAY:            return KIND_SAY;
        case CHAT_MSG_YELL:           return KIND_YELL;
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_TEXT_EMOTE:     return KIND_EMOTE;
        case CHAT_MSG_WHISPER:        return KIND_WHISPER;
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:   return KIND_PARTY;
        case CHAT_MSG_RAID:
        case CHAT_MSG_RAID_LEADER:
        case CHAT_MSG_RAID_WARNING:   return KIND_RAID;
        case CHAT_MSG_GUILD:          return KIND_GUILD;
        case CHAT_MSG_OFFICER:        return KIND_OFFICER;
        case CHAT_MSG_CHANNEL:        return KIND_CHANNEL;
        default:                      return KIND_NONE;
    }
}

// Cut a string to a byte budget WITHOUT splitting a UTF-8 code point.
//
// std::string::resize counts bytes. WoW's 255 is a byte limit, but a Discord
// line is capped in CHARACTERS, so an emoji-heavy message arrives several
// times longer in bytes and a plain resize lands mid-code-point. The result
// is invalid utf8mb4, which MySQL rejects - and since the flush is ONE
// multi-row INSERT, a single mangled line would take the whole batch of
// everybody's conversation down with it.
void TruncateUtf8(std::string& s, size_t maxBytes)
{
    if (s.size() <= maxBytes)
        return;
    size_t cut = maxBytes;
    // Continuation bytes are 10xxxxxx. Step back off them to the lead byte,
    // which is where the previous complete code point ended.
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    s.resize(cut);
}

std::string Esc(std::string s)
{
    TruncateUtf8(s, MAX_CHAT_BYTES);
    CharacterDatabase.EscapeString(s);
    return s;
}

// A probe result is JSON and can run to a few KB - a spell list alone passes
// MAX_CHAT_BYTES by level 11 - so it needs an escape that does not truncate to
// chat length. The cap is still finite: `result` is MEDIUMTEXT, and a runaway
// probe should be cut off rather than handed to MySQL as a 16MB row.
constexpr size_t MAX_PROBE_BYTES = 262144;

std::string EscLong(std::string s)
{
    TruncateUtf8(s, MAX_PROBE_BYTES);
    CharacterDatabase.EscapeString(s);
    return s;
}

// Minimal JSON string escaping. Item and character names come from the world,
// not from the probe, so they can carry anything the client allows.
std::string J(std::string const& s)
{
    std::ostringstream out;
    out << '"';
    for (char const c : s)
    {
        switch (c)
        {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out << ' ';
                else
                    out << c;
        }
    }
    out << '"';
    return out.str();
}

struct PendingLine
{
    std::string heardBy;
    std::string senderName;
    std::string text;
    std::string channelName;
    uint32 senderGuid = 0;
    uint32 kind = KIND_NONE;
    bool isBot = false;
};

std::mutex g_chatMutex;
std::vector<PendingLine> g_chatQueue;
// A chat storm must cost a bounded amount of memory, never an unbounded one.
// Overflow is dropped and counted rather than queued forever.
constexpr size_t MAX_QUEUED_LINES = 2000;
uint64 g_droppedLines = 0;

// The one place a heard line is captured. `sender` may be a bot or a seated
// player; `heardBy` is the watched character it reached.
//
// This runs on whatever thread the speech happened on - for bots, a map-update
// thread - so it does NO database work. It only copies strings under a lock.
// The write happens on the world thread in FlushChat, for two reasons:
// DatabaseWorkerPool::EscapeString borrows the shared synchronous connection
// with no lock of its own, and batching turns a 500-bot conversation into one
// multi-row INSERT per tick instead of one statement per line.
void RecordHeard(Player* sender, std::string const& heardBy, uint32 kind, std::string const& text,
                 std::string const& channelName)
{
    if (!sender || text.empty())
        return;

    PendingLine line;
    line.heardBy = heardBy;
    line.senderName = sender->GetName();
    line.text = text;
    line.channelName = channelName;
    line.senderGuid = sender->GetGUID().GetCounter();
    line.kind = kind;
    line.isBot = GET_PLAYERBOT_AI(sender) != nullptr;

    std::lock_guard<std::mutex> guard(g_chatMutex);
    if (g_chatQueue.size() >= MAX_QUEUED_LINES)
    {
        ++g_droppedLines;
        return;
    }
    g_chatQueue.push_back(std::move(line));
}

// Lock-free early-out. Costs one atomic load on the hot path.
bool Interested(uint32 kind)
{
    return kind != KIND_NONE && (g_watchUnion.load(std::memory_order_relaxed) & kind) != 0;
}

// Resolve each watcher that wants this kind, skipping any who are offline.
template <typename F>
void ForEachWatcher(uint32 kind, F&& fn)
{
    std::lock_guard<std::mutex> guard(g_watchMutex);
    for (WatchEntry const& entry : g_watch)
    {
        if (!(entry.channels & kind))
            continue;
        if (Player* watcher = ObjectAccessor::FindPlayerByName(entry.name))
            if (watcher->IsInWorld())
                fn(watcher);
    }
}

// Mirror of Guild::BroadcastToGuild's recipient test: guild membership plus
// the matching listen right. Keeping this in one place is what stops the hook
// path and the Discord-origin path drifting apart.
bool GuildCanHear(Guild* guild, Player* watcher, uint32 kind)
{
    if (!guild || !watcher || watcher->GetGuildId() != guild->GetId())
        return false;
    return guild->HasRankRight(
        watcher, kind == KIND_OFFICER ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN);
}

// Capture a line for a send that did NOT pass through the chat hooks.
//
// Group::BroadcastPacket and Guild::BroadcastToGuild deliver the packet
// themselves and never call OnPlayerCanUseChat, so a party/raid/guild/officer
// line sent from Discord would otherwise arrive in the game and never reach
// the relay - and, because a spoken line's acknowledgement is deliberately
// suppressed on the assumption the relay echoes it, the sender would see
// nothing at all on four of the advertised channels.
template <typename Pred>
void CaptureBypassed(Player* sender, uint32 kind, std::string const& text, Pred&& heard)
{
    if (!Interested(kind))
        return;
    ForEachWatcher(kind, [&](Player* watcher)
    {
        if (heard(watcher))
            RecordHeard(sender, watcher->GetName(), kind, text, "");
    });
}
}  // namespace

/*
 * Chat capture.
 *
 * These are OnPlayerCanUseChat overloads - a gate hook that can veto a line.
 * We never veto: every override returns true unconditionally. It is used here
 * purely as the observation point, because it is the ONLY hook that sees both
 * halves of the world. Seated players reach it through ChatHandler.cpp's
 * opcode handler; bots never touch an opcode, but Player::Say/Yell/TextEmote/
 * Whisper call it directly, so one hook catches both.
 */
class OverseerChatScript : public PlayerScript
{
public:
    // Only the five hooks this script implements. An empty list would enable
    // ALL player hooks, putting this script in the dispatch loop for every
    // player event on a 500-bot world for no reason.
    //
    // These names matter and are easy to get wrong: the chat GATE hooks are
    // PLAYERHOOK_CAN_PLAYER_USE_*. The similarly-named PLAYERHOOK_ON_CHAT*
    // values guard a different family, and registering for those would leave
    // this script silently never called.
    OverseerChatScript() : PlayerScript("OverseerChatScript", {
        PLAYERHOOK_CAN_PLAYER_USE_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
    }) {}

    // say / yell / emote - audibility is distance on the same map.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg) override
    {
        uint32 kind = KindFromChatType(type);
        if (!Interested(kind))
            return true;

        float range = (kind == KIND_YELL) ? sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_YELL)
                    : (kind == KIND_EMOTE) ? sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE)
                    : sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY);

        ForEachWatcher(kind, [&](Player* watcher)
        {
            // The speaker hears themselves - that line belongs in their log.
            if (watcher == player || player->IsWithinDistInMap(watcher, range))
                RecordHeard(player, watcher->GetName(), kind, msg, "");
        });
        return true;
    }

    // whisper - audible to exactly two characters.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg,
                            Player* receiver) override
    {
        if (!Interested(KIND_WHISPER))
            return true;

        ForEachWatcher(KIND_WHISPER, [&](Player* watcher)
        {
            if (watcher == player || watcher == receiver)
                RecordHeard(player, watcher->GetName(), KIND_WHISPER, msg, "");
        });
        return true;
    }

    // party / raid - audible to the group, but party chat inside a RAID
    // reaches only the speaker's own subgroup. The real handler passes
    // Group::GetMemberGroup for exactly this reason; recording the whole raid
    // would put lines in the relay that the watcher's client never showed.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg,
                            Group* group) override
    {
        uint32 kind = KindFromChatType(type);
        if (!group || !Interested(kind))
            return true;

        bool subgroupOnly = (kind == KIND_PARTY) && group->isRaidGroup();
        uint8 senderSub = subgroupOnly ? group->GetMemberGroup(player->GetGUID()) : 0;

        ForEachWatcher(kind, [&](Player* watcher)
        {
            if (!group->IsMember(watcher->GetGUID()))
                return;
            if (subgroupOnly && group->GetMemberGroup(watcher->GetGUID()) != senderSub)
                return;
            RecordHeard(player, watcher->GetName(), kind, msg, "");
        });
        return true;
    }

    // guild / officer - being in the guild is NOT enough. Guild::
    // BroadcastToGuild only delivers to members holding the matching LISTEN
    // right, so an ordinary member never sees officer chat; copying it into
    // their relay would show them something their client refused them.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg,
                            Guild* guild) override
    {
        uint32 kind = KindFromChatType(type);
        if (!guild || !Interested(kind))
            return true;

        ForEachWatcher(kind, [&](Player* watcher)
        {
            if (GuildCanHear(guild, watcher, kind))
                RecordHeard(player, watcher->GetName(), kind, msg, "");
        });
        return true;
    }

    // Public channels (Trade, General) are NOT captured, and the reason is
    // worth stating: Channel::IsOn is PRIVATE, and the core exposes no public
    // way to ask whether a player is in a given channel (Player::m_channels is
    // private too, with no getter). Recording without that test would relay
    // lines the watcher is not actually in - which is precisely the "wiretap
    // rather than chat log" failure the rest of this file works to avoid.
    //
    // So the capability is absent rather than approximate. KindFromName
    // refuses "channel" for the same reason, so a watch row asking for it
    // fails loudly at parse time instead of silently doing nothing.
};

class OverseerWorldScript : public WorldScript
{
public:
    OverseerWorldScript() : WorldScript("OverseerWorldScript") {}

    void OnUpdate(uint32 diff) override
    {
        _commandTimer += diff;
        _snapshotTimer += diff;
        _watchTimer += diff;
        _sweepTimer += diff;
        _chatFlushTimer += diff;
        _rosterTimer += diff;
        _partyTimer += diff;
        _trainTimer += diff;
        _questTimer += diff;

        // Load the watch list before the first poll, not 30s after startup.
        if (!_watchLoaded)
        {
            _watchLoaded = true;
            ReloadWatchList();
        }
        if (_commandTimer >= COMMAND_POLL_MS)
        {
            _commandTimer = 0;
            DeliverPendingCommands();
        }
        if (_snapshotTimer >= SNAPSHOT_MS)
        {
            _snapshotTimer = 0;
            WriteSnapshot();
        }
        if (_watchTimer >= WATCH_RELOAD_MS)
        {
            _watchTimer = 0;
            ReloadWatchList();
        }
        if (_rosterTimer >= ROSTER_POLL_MS)
        {
            _rosterTimer = 0;
            KeepRosterOnline();
        }
        if (_partyTimer >= PARTY_POLL_MS)
        {
            _partyTimer = 0;
            KeepRosterGrouped();
        }
        if (_trainTimer >= TRAIN_POLL_MS)
        {
            _trainTimer = 0;
            TrainRoster();
        }
        if (_questTimer >= QUEST_POLL_MS)
        {
            _questTimer = 0;
            DriveQuests();
        }
        if (_chatFlushTimer >= CHAT_FLUSH_MS)
        {
            _chatFlushTimer = 0;
            FlushChat();
        }
        if (_sweepTimer >= CHAT_SWEEP_MS)
        {
            _sweepTimer = 0;
            CharacterDatabase.Execute(
                "DELETE FROM overseer_chat WHERE created_at < NOW() - INTERVAL {} MINUTE",
                CHAT_RETENTION_MINUTES);
        }
    }

private:
    // Log in the characters that are supposed to be playing (infra#2656).
    //
    // AddPlayerBot with a master account of 0 is the whole trick. The
    // permission gate in PlayerbotHolder::AddPlayerBot reads
    //
    //     bool isRndbot = !masterAccountId;
    //     if (!isRndbot && !sameAccount && !sameGuild && !addClassBot && !linkedAccount)
    //
    // so passing 0 skips it, and the login callback then takes the branch that
    // needs no master session. That is what makes this unattended: no client,
    // nobody logged in, no `.bot add` typed by a human.
    //
    // These bots are deliberately NOT enrolled in RandomPlayerbotMgr's pool.
    // Its bots come from `add` rows in playerbots_random_bots, which these
    // characters do not have, so the periodic Randomize() that would re-roll a
    // level 1 character's level and gear never reaches them.
    void KeepRosterOnline()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        uint32 started = 0;
        do
        {
            if (started >= ROSTER_LOGINS_PER_POLL)
                break;

            std::string const name = result->Fetch()[0].Get<std::string>();

            // Already playing. Checked by name rather than by tracking what we
            // launched, so a character that logs out for any reason - a crash,
            // a real player taking it over and leaving - comes back on the next
            // pass without the module needing to have noticed why it went.
            if (ObjectAccessor::FindPlayerByName(name))
                continue;

            ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
            if (!guid)
            {
                LOG_WARN("module.overseer", "overseer: roster character '{}' does not exist", name);
                continue;
            }

            LOG_INFO("module.overseer", "overseer: logging in roster character '{}'", name);
            sRandomPlayerbotMgr.AddPlayerBot(guid, 0);
            ++started;
        } while (result->NextRow());
    }

    // Keep the roster in one party, so they can actually talk to each other.
    //
    // WHY. /say carries about 25 yards. The family grinds in different zones,
    // so a conversation held in /say is five characters talking to themselves
    // in empty air - which is exactly what happened: every captured line came
    // back with heard_by equal to sender_name, and Discord showed a coherent
    // conversation that never occurred. Party chat has no range limit.
    //
    // WHY NOT THE PLAYERBOT INVITE ACTION. InviteToGroupAction::Execute reads
    // `Player* master = event.getOwner(); return Invite(bot, master);` - it
    // invites the WHISPERER and ignores any name given. This module runs
    // commands through the character's own session, so that is Invite(x, x),
    // which the action refuses. There is no owner to hand it, so the group has
    // to be formed here.
    //
    // WHY NOT `.group join`. It requires the first argument to already be in a
    // group, so it can add to a party but cannot create one.
    void KeepRosterGrouped()
    {
        // Designated leader first, so a freshly formed party starts in the
        // right hands rather than being corrected afterwards.
        QueryResult result = CharacterDatabase.Query(
            // `lead` is BACKTICKED because it is a reserved word in MySQL 8 -
            // the LEAD() window function. Unquoted it is a syntax error, the
            // core treats a malformed query as unrecoverable, and the
            // worldserver aborts on the first roster poll. It shipped that way
            // and took the server down in a crash loop.
            "SELECT name, `lead` FROM overseer_roster WHERE enabled = 1 "
            "ORDER BY `lead` DESC, name");
        if (!result)
            return;

        std::string wantsToLead;
        std::vector<Player*> present;
        do
        {
            Field* row = result->Fetch();
            std::string const name = row[0].Get<std::string>();
            if (row[1].Get<uint8>() && wantsToLead.empty())
                wantsToLead = name;
            if (Player* p = ObjectAccessor::FindPlayerByName(name))
                present.push_back(p);
        } while (result->NextRow());

        if (present.size() < 2)
            return;

        // Prefer a group the roster is already in over making a new one, so a
        // party someone formed by hand is joined rather than competed with.
        Group* group = nullptr;
        for (Player* p : present)
        {
            if (Group* existing = p->GetGroup())
            {
                group = existing;
                break;
            }
        }

        if (!group)
        {
            Player* leader = present.front();
            group = new Group();
            if (!group->Create(leader))
            {
                delete group;
                LOG_WARN("module.overseer", "overseer: could not form the roster party");
                return;
            }
            sGroupMgr->AddGroup(group);
            LOG_INFO("module.overseer", "overseer: formed the roster party under '{}'",
                     leader->GetName());
        }

        for (Player* p : present)
        {
            if (p->GetGroup())
                continue;   // already with us, or in a party of their own
            if (group->IsFull())
            {
                LOG_WARN("module.overseer", "overseer: party full, '{}' left out",
                         p->GetName());
                break;
            }
            if (group->AddMember(p))
                LOG_INFO("module.overseer", "overseer: '{}' joined the party", p->GetName());
        }
        // Leadership drifts on its own: when the leader logs out the server
        // promotes whoever is left, so a character taken over at the keyboard
        // and handed back comes home a member. Corrected here rather than with
        // `.group leader`, which needs GM security a bot session does not have.
        if (!wantsToLead.empty())
        {
            if (Player* head = ObjectAccessor::FindPlayerByName(wantsToLead))
            {
                if (head->GetGroup() == group && group->GetLeaderGUID() != head->GetGUID())
                {
                    group->ChangeLeader(head->GetGUID());
                    group->SendUpdate();
                    LOG_INFO("module.overseer", "overseer: '{}' now leads the party",
                             wantsToLead);
                }
            }
        }

        group->BroadcastGroupUpdate();
    }

    // Point the roster at the quests they keep talking about.
    //
    // WHAT WAS WRONG. The council reaches a decision - "Grug need 2 Bundle of
    // Wood for A Bundle of Trouble" - and it goes nowhere, because nothing
    // turns a decision into behaviour. Minutes later the goal supervisor
    // re-asserts `grind`, which means "kill what is in front of you", and that
    // is exactly what they do. They were never being stupid; they were doing
    // the last thing anybody actually told them.
    //
    // mod-playerbots already has the mechanism. RPG_DO_QUEST reads the quest
    // log, resolves the objective's POI and walks the bot to it. Its chat
    // command cannot reach this family:
    //
    //     bool isMaster = master && master->GetGUID() == owner->GetGUID();
    //     bool isGM = owner->GetSession() && owner->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    //     if (!isMaster && !isGM) ... "Only your master or a GM can change my rpg status."
    //
    // These bots are masterless by design and their sessions carry no GM
    // security, so both halves are false forever. Third mechanism in this epic
    // behind that same wall, after talent picking and trainer spells.
    //
    // rpgInfo is public and NewRpgInfo is a struct, so the module sets the
    // state directly - which is the same permission the chat command would have
    // granted, arrived at without pretending to be a master.
    //
    // ONLY THE TRAVELLER. Followers run `nc -new rpg` and `nc +follow` on
    // purpose: `new rpg` acts at relevance 3.0 to 11.0 against follow's 1.0, so
    // a follower given both wanders off every tick. Sending them to a quest
    // objective individually would scatter the family across the zone, which is
    // the failure the follow work fixed. They travel by following whoever is
    // travelling.
    void DriveQuests()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1 AND `lead` = 1");
        if (!result)
            return;

        do
        {
            std::string const name = result->Fetch()[0].Get<std::string>();
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!bot)
                continue;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI)
                continue;

            // Already on one. Re-issuing would restart the travel from here
            // every poll, which is a character that walks toward an objective
            // forever and never arrives.
            if (botAI->rpgInfo.GetStatus() == RPG_DO_QUEST)
                continue;

            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 const questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;

                // INCOMPLETE means there is work left to do. COMPLETE is
                // deliberately included: the objective is met and the walk back
                // to the giver is the rest of the quest, and a family that
                // never turns anything in is not playing either.
                QuestStatus const status = bot->GetQuestStatus(questId);
                if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;

                LOG_INFO("module.overseer", "overseer: '{}' now working quest {} ({})",
                         name, questId, quest->GetTitle());
                botAI->rpgInfo.ChangeToDoQuest(questId, quest);
                break;
            }
        } while (result->NextRow());
    }

    // Teach the roster what a character of its level would already know.
    //
    // WHY THIS EXISTS AT ALL. mod-playerbots does this job properly on levelup -
    // AutoMaintenanceOnLevelupAction picks talents, learns every trainer spell,
    // and re-runs the skill init - and every branch of it is behind
    // IsRandomBot(bot). That needs the account to be in the "<prefix>0..N"
    // random-bot list AND the character to be in the currentBots pool AND the
    // bot not to be a selfbot. This family fails all three, permanently and by
    // design: named accounts cannot enter the list, they are kept out of the
    // pool so RandomPlayerbotMgr never re-rolls them, and SelfBotLevel is 3.
    //
    // The result was invisible because the config said otherwise.
    // AutoLearnTrainerSpells and AutoPickTalents were both 1, both parsed, and
    // both dead on arrival for these five. What it looked like from the outside
    // was a family that fought badly: a level 11 warrior with 13 spells and one
    // talent, and a level 9 paladin with no spells whatsoever - no seal, no
    // judgement, auto-attack and nothing else.
    //
    // WHY THE FACTORY AND NOT A SPELL LIST. PlayerbotFactory::InitAvailableSpells
    // walks the real trainer tables, so a character learns exactly what a trainer
    // in the world would teach it and nothing more. Writing our own list would be
    // a second, worse copy of data the server already has, and would drift the
    // first time a class changed.
    //
    // WHAT IS DELIBERATELY NOT CALLED. AutoTeleportForLevel, which yanks a bot to
    // a level-appropriate zone: the family is questing somewhere on purpose and
    // being teleported out of it mid-quest is not training, it is abduction.
    // Randomize() and ClearEverything() likewise - they re-roll gear and level,
    // which is the exact fate the roster exists to protect these characters from.
    void TrainRoster()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, spec_tab, trained_level FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            std::string const name = fields[0].Get<std::string>();
            uint8 const specTab = fields[1].Get<uint8>();
            uint8 const trainedLevel = fields[2].Get<uint8>();

            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!bot)
                continue;

            uint8 const level = bot->GetLevel();

            // Two reasons to do the work, and the talent one has to be gated on
            // specTab as well: with no tree chosen the points are never spent,
            // so "has free points" would be true forever and this would re-run
            // the whole trainer walk every single poll.
            bool const hasTree = specTab <= MAX_TALENT_TAB;
            bool const wantsTalents = hasTree && bot->GetFreeTalentPoints() > 0;
            if (level == trainedLevel && !wantsTalents)
                continue;

            LOG_INFO("module.overseer", "overseer: training '{}' at level {} (spec_tab {})",
                     name, static_cast<uint32>(level), static_cast<uint32>(specTab));

            PlayerbotFactory factory(bot, level);
            factory.InitSkills();
            factory.InitClassSpells();
            factory.InitAvailableSpells();

            if (hasTree)
                SpendTalents(bot, static_cast<uint32>(specTab));

            bot->SendTalentsInfoData(false);

            CharacterDatabase.Execute(
                "UPDATE overseer_roster SET trained_level = {} WHERE name = '{}'",
                static_cast<uint32>(level), Esc(name));
        } while (result->NextRow());
    }

    // Spend every free talent point in one tree, lowest row first.
    //
    // WHY NOT PlayerbotFactory::InitTalentsTree. It picks the tree at random
    // from AiPlayerbot.RandomClassSpecProb, which is right for a bot nobody
    // knows and wrong for a named character whose role is a decision - Grug
    // tanks because he is the father who goes first, not because a die landed
    // on protection. It then applies the premade spec link, which is configured
    // only at levels 60 and 80: InitTalentsByTemplate walks DOWN from the
    // character's level looking for one, finds nothing below 60, and spends no
    // points at all. Every talent the family could have had before level 60 was
    // being left on the table by a path that reported success.
    //
    // WHY ROW ORDER MATTERS. A talent tier only unlocks once enough points sit
    // in the tiers above it, so an out-of-order walk silently learns nothing
    // past the first locked row. std::map iterates ascending, which is the
    // order the tree itself requires.
    //
    // WHY FIVE POINTS PER ROW. That is the tier requirement, so it is also the
    // least that opens the next row. Without the cap a five-rank talent on row
    // 0 can eat every point a low-level character has and the tree never opens.
    void SpendTalents(Player* bot, uint32 tabpage)
    {
        uint32 const classMask = bot->getClassMask();

        std::map<uint32, std::vector<TalentEntry const*>> rows;
        for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
        {
            TalentEntry const* talent = sTalentStore.LookupEntry(i);
            if (!talent)
                continue;

            TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
            if (!tab || tab->tabpage != tabpage)
                continue;

            if ((classMask & tab->ClassMask) == 0)
                continue;

            rows[talent->Row].push_back(talent);
        }

        for (auto const& row : rows)
        {
            uint32 const before = bot->GetFreeTalentPoints();
            for (TalentEntry const* talent : row.second)
            {
                uint32 const free = bot->GetFreeTalentPoints();
                if (!free || before - free >= 5)
                    break;

                uint32 maxRank = 0;
                for (uint32 rank = 0; rank < std::min<uint32>(MAX_TALENT_RANK, free); ++rank)
                    if (talent->RankID[rank])
                        maxRank = rank;

                // A talent behind a prerequisite is unlearnable until the
                // prerequisite is held, and LearnTalent refuses silently rather
                // than complaining, so the dependency is satisfied first.
                if (talent->DependsOn)
                    bot->LearnTalent(talent->DependsOn,
                                     std::min<uint32>(talent->DependsOnRank, free - 1));

                bot->LearnTalent(talent->TalentID, maxRank);
            }
        }
    }

    // Rebuilt wholesale rather than diffed: the list is a handful of rows and
    // a torn update would silently stop capturing a character's chat.
    void ReloadWatchList()
    {
        std::vector<WatchEntry> next;
        uint32 unionMask = KIND_NONE;

        if (QueryResult result = CharacterDatabase.Query("SELECT name, channels FROM overseer_chat_watch"))
        {
            do
            {
                Field* fields = result->Fetch();
                WatchEntry entry;
                entry.name = fields[0].Get<std::string>();

                std::istringstream parts(fields[1].Get<std::string>());
                std::string token;
                while (std::getline(parts, token, ','))
                {
                    // Tolerate "say, yell" as well as "say,yell".
                    size_t begin = token.find_first_not_of(" \t");
                    if (begin == std::string::npos)
                        continue;
                    size_t end = token.find_last_not_of(" \t");
                    entry.channels |= KindFromName(token.substr(begin, end - begin + 1));
                }

                if (entry.channels != KIND_NONE)
                {
                    unionMask |= entry.channels;
                    next.push_back(std::move(entry));
                }
            } while (result->NextRow());
        }

        {
            std::lock_guard<std::mutex> guard(g_watchMutex);
            g_watch = std::move(next);
        }
        // Published last: a hot-path reader that sees the new union always
        // finds the matching list already in place.
        g_watchUnion.store(unionMask, std::memory_order_relaxed);
    }

    // Drain what the chat hooks captured into one multi-row INSERT. Runs on
    // the world thread, which is what makes the EscapeString calls safe.
    //
    // DURABILITY, stated plainly so nobody reads more into it than is there.
    // The batch is handed to the async writer and the in-memory copy is then
    // dropped. CharacterDatabase::Execute reports nothing back, so if MySQL is
    // unavailable or rejects the statement, those lines are gone. There is no
    // retry buffer here on purpose:
    //
    //   - the pool exposes no success-aware async write for a plain statement,
    //     and DirectExecute is both still void and a world-thread stall;
    //   - WriteSnapshot has had exactly this property since before the chat
    //     bridge existed, so this is the module's established bargain rather
    //     than a new one;
    //   - a worldserver that cannot reach its own database is not quietly
    //     losing conversation, it is already broken in ways the operator will
    //     hear about first.
    //
    // So: the DISCORD leg is the one with the guarantee - a row is marked
    // relayed only after Discord accepts it, so that hop never silently drops
    // a line. The CAPTURE leg is best-effort. Queue overflow, by contrast, IS
    // counted and logged, because that one is ours to bound.
    void FlushChat()
    {
        std::vector<PendingLine> batch;
        uint64 dropped = 0;
        {
            std::lock_guard<std::mutex> guard(g_chatMutex);
            if (g_chatQueue.empty() && !g_droppedLines)
                return;
            batch.swap(g_chatQueue);
            dropped = g_droppedLines;
            g_droppedLines = 0;
        }

        if (dropped)
            LOG_WARN("module.overseer", "overseer: dropped {} chat lines (queue full)", dropped);

        if (batch.empty())
            return;

        std::ostringstream ss;
        ss << "INSERT INTO overseer_chat (heard_by, sender_name, sender_guid, sender_is_bot, "
              "channel, channel_name, text) VALUES ";
        bool first = true;
        for (PendingLine const& line : batch)
        {
            if (!first)
                ss << ',';
            first = false;
            ss << "('" << Esc(line.heardBy)
               << "','" << Esc(line.senderName)
               << "'," << line.senderGuid
               << ',' << (line.isBot ? 1 : 0)
               << ",'" << NameFromKind(line.kind)
               << "','" << Esc(line.channelName)
               << "','" << Esc(line.text)
               << "')";
        }
        CharacterDatabase.Execute(ss.str().c_str());
    }

    // ONE COMMAND PER VERB PER CHARACTER PER POLL.
    //
    // WHAT WAS WRONG. The goal supervisor hands the party leader three orders
    // in one batch, and they are all inserted in the same second:
    //
    //     03:02:00  Grug  nc +new rpg   overseer:life  delivered
    //     03:02:00  Grug  nc +grind     overseer:life  delivered
    //     03:02:00  Grug  co +flee      overseer:life  delivered
    //
    // At 03:07 a live read of Grug's non-combat engine had `grind` and no
    // `new rpg`, with no `-new rpg` ever sent to him. `new rpg` is the only
    // strategy that makes a character travel and work quests, so the family
    // stood still: all five moved 0.0 yards in 45 seconds, 11 yards from the
    // kobolds they had just agreed in party chat to go kill.
    //
    // WHY. A whispered command does not act when it is delivered. It lands in
    // PlayerbotAI::chatCommands (PlayerbotAI.cpp:1107) and is drained on the
    // bot's next AI tick by HandleCommands (PlayerbotAI.cpp:553-588), which
    // empties the WHOLE queue in one pass and, for each entry, calls
    // ChatCommandTrigger::ExternalEvent. That trigger keeps ONE param:
    //
    //     void ChatCommandTrigger::ExternalEvent(std::string const paramName, Player* eventPlayer)
    //     { param = paramName; owner = eventPlayer; triggered = true; }
    //                          -- ChatCommandTrigger.cpp:15-20
    //
    // and there is one trigger instance per verb, cached for the life of the
    // bot by NamedObjectContextList::GetContextObject (NamedObjectContext.h:203).
    // So both `nc` commands write the same slot before the engine ever reads
    // it, and Check() (ChatCommandTrigger.cpp:22-28) returns only the last one
    // written. `+grind` overwrote `+new rpg`, which was never applied at all -
    // this was never an eviction, it was a command that never arrived.
    // `co +flee` survived because `co` is a different trigger.
    //
    // The three deliveries all reported `delivered` truthfully: HandleCommand
    // did accept every one of them. The loss happens a tick later, inside
    // mod-playerbots, where nothing reports anything.
    //
    // THE FIX. Hand a character at most one command per verb per poll and
    // leave the rest pending. They go out on the next poll 2s later
    // (COMMAND_POLL_MS), after the bot's AI has ticked and consumed the first,
    // so each one gets the trigger slot to itself. Keying on the verb rather
    // than on the character alone is what keeps `co +flee` in the same batch:
    // it cannot collide with `nc` and has no reason to wait.
    //
    // Deliberately NOT done by folding them into one `nc +new rpg,+grind`
    // command in the supervisor. That would fix this batch and nothing else -
    // the goal loop and the roster loop insert independently, so any two `nc`
    // rows that happen to be pending together collide the same way. The queue
    // is where the constraint actually lives.
    void DeliverPendingCommands()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT id, target_name, command, kind, channel, target_arg FROM overseer_command "
            "WHERE status = 'pending' ORDER BY id ASC LIMIT {}",
            COMMANDS_PER_POLL);
        if (!result)
            return;

        // "<character>\n<verb>" already handed to a bot in THIS poll. Local to
        // the poll on purpose: the trigger slot is only contended between
        // deliveries that share one drain of the queue.
        std::set<std::string> spoken;

        do
        {
            Field* fields = result->Fetch();
            uint32 id = fields[0].Get<uint32>();
            std::string targetName = fields[1].Get<std::string>();
            std::string command = fields[2].Get<std::string>();
            std::string kind = fields[3].Get<std::string>();
            std::string channel = fields[4].Get<std::string>();
            std::string targetArg = fields[5].Get<std::string>();

            char const* status = "error";
            char const* detail = "";

            // Bot orders only. 'chat', 'gm' and 'probe' do not go through
            // PlayerbotAI::HandleCommand and share no trigger, so nothing they
            // do can be overwritten by the row after them.
            if (kind != "chat" && kind != "gm" && kind != "probe")
            {
                // The verb is the first word - `nc`, `co`, `d`. What the rest
                // of the line says does not matter here; two commands with the
                // same verb reach the same ChatCommandTrigger whatever their
                // arguments are.
                std::string const verb = command.substr(0, command.find(' '));
                if (!spoken.insert(targetName + "\n" + verb).second)
                {
                    // Left PENDING, not claimed and not failed. The sender is
                    // still waiting and will get its answer 2s from now.
                    LOG_DEBUG("module.overseer",
                              "overseer: holding command {} ('{}' for '{}') until next poll; "
                              "'{}' already sent this poll and they share a trigger",
                              id, command, targetName, verb);
                    continue;
                }
            }

            // CLAIM BEFORE RUNNING, synchronously.
            //
            // This queue was at-least-once: the row was only moved off
            // 'pending' after the work was done, so a crash or a lost database
            // connection between the two left it pending and it ran a second
            // time on recovery. That was survivable while every command was an
            // idempotent bot order, and is not now that a dot-command can
            // create items or move a character. Claiming first makes it
            // at-most-once instead: a crash mid-command loses the command,
            // which is the right way round for something non-idempotent, and
            // the absent acknowledgement is what tells the sender.
            CharacterDatabase.DirectExecute(
                "UPDATE overseer_command SET status = 'claimed', claimed_by = '{}' "
                "WHERE id = {} AND status = 'pending'",
                g_runToken, id);

            // ...and CONFIRM that WE won it. DirectExecute reports nothing
            // back, so without this read a failed write would leave the row
            // pending while the command ran anyway - the at-least-once
            // behaviour this exists to remove, only harder to see.
            //
            // Reading the token rather than just the status is what makes the
            // claim exclusive: 'claimed' alone is equally true when somebody
            // else claimed it, so two processes racing the same row would both
            // conclude they held it and both run the command. Query is
            // synchronous, so this is all settled before anything happens.
            {
                QueryResult claimed = CharacterDatabase.Query(
                    "SELECT claimed_by FROM overseer_command "
                    "WHERE id = {} AND status = 'claimed'",
                    id);
                if (!claimed || claimed->Fetch()[0].Get<std::string>() != g_runToken)
                {
                    LOG_WARN("module.overseer",
                             "overseer: did not win the claim on command {}; not running it", id);
                    continue;
                }
            }

            // Names are server-enforced to letters only, so they are safe to
            // embed; `detail` is one of the literals below; `command` is never
            // echoed back into SQL.
            // Cleared per row: a probe that fails must not inherit the answer
            // the previous row produced, which would be a wrong reading served
            // with every appearance of a fresh one.
            std::string probeResult;

            Player* player = ObjectAccessor::FindPlayerByName(targetName);
            if (!player)
                detail = "target not online";
            else if (kind == "chat")
                detail = DoChat(player, channel, command, targetArg, status);
            else if (kind == "gm")
                detail = DoGmCommand(player, command, status);
            else if (kind == "probe")
                detail = DoProbe(player, command, status, probeResult);
            else if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
            {
                botAI->HandleCommand(CHAT_MSG_WHISPER, command, player);
                status = "delivered";
            }
            else
                detail = "target has no bot AI (selfbot not enabled?)";

            // Conditional on the row still being OURS. If the bridge gave up
            // waiting and already ended this row, writing over it would
            // resurrect a command the sender has been told nothing came of,
            // and the two sides would disagree about what happened.
            // `result` is written as its own statement shape rather than always
            // being included, so a non-probe row does not have NULL written over
            // whatever a reader might already have taken from it.
            if (probeResult.empty())
                CharacterDatabase.Execute(
                    "UPDATE overseer_command SET status = '{}', detail = '{}' "
                    "WHERE id = {} AND status = 'claimed' AND claimed_by = '{}'",
                    status, detail, id, g_runToken);
            else
                CharacterDatabase.Execute(
                    "UPDATE overseer_command SET status = '{}', detail = '{}', result = '{}' "
                    "WHERE id = {} AND status = 'claimed' AND claimed_by = '{}'",
                    status, detail, EscLong(probeResult), id, g_runToken);
        } while (result->NextRow());
    }

    // Speak as the character would. Language matches mod-playerbots' own
    // convention so a Discord-sent line is indistinguishable from a real one:
    // racial for say/yell, universal for the group channels.
    static char const* DoChat(Player* player, std::string const& channel, std::string const& text,
                              std::string const& targetArg, char const*& status)
    {
        if (text.empty())
            return "empty message";

        Language racial = (player->GetTeamId() == TeamId::TEAM_ALLIANCE) ? LANG_COMMON : LANG_ORCISH;

        if (channel == "say")
            player->Say(text, racial);
        else if (channel == "yell")
            player->Yell(text, racial);
        else if (channel == "emote")
            player->TextEmote(text);
        else if (channel == "whisper")
        {
            Player* receiver = ObjectAccessor::FindPlayerByName(targetArg);
            if (!receiver)
                return "whisper target not online";
            player->Whisper(text, LANG_UNIVERSAL, receiver);
        }
        else if (channel == "party" || channel == "raid")
        {
            Group* group = player->GetGroup();
            if (!group)
                return "not in a group";
            if (channel == "raid" && !group->isRaidGroup())
                return "not in a raid";
            bool isRaid = (channel == "raid");
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, isRaid ? CHAT_MSG_RAID : CHAT_MSG_PARTY,
                                         LANG_UNIVERSAL, player, nullptr, text);

            // Party chat inside a raid goes to the speaker's subgroup only -
            // the real handler passes GetMemberGroup here. Broadcasting to
            // the whole raid would make /party from Discord behave unlike
            // /party at the keyboard.
            bool subgroupOnly = !isRaid && group->isRaidGroup();
            uint8 senderSub = group->GetMemberGroup(player->GetGUID());
            if (subgroupOnly)
                group->BroadcastPacket(&data, false, senderSub);
            else
                group->BroadcastPacket(&data, false);

            CaptureBypassed(player, KindFromName(channel), text, [&](Player* w)
            {
                if (!group->IsMember(w->GetGUID()))
                    return false;
                return !subgroupOnly || group->GetMemberGroup(w->GetGUID()) == senderSub;
            });
        }
        else if (channel == "guild" || channel == "officer")
        {
            if (!player->GetSession())
                return "character has no session";
            Guild* guild = player->GetGuildId() ? sGuildMgr->GetGuildById(player->GetGuildId()) : nullptr;
            if (!guild)
                return "not in a guild";
            bool officerOnly = (channel == "officer");
            // BroadcastToGuild silently does nothing if the speaker lacks the
            // SPEAK right, so checking first is the difference between
            // reporting "delivered" and reporting the truth.
            if (!guild->HasRankRight(
                    player, officerOnly ? GR_RIGHT_OFFCHATSPEAK : GR_RIGHT_GCHATSPEAK))
                return officerOnly ? "no officer chat rights in this guild"
                                   : "no guild chat rights in this guild";

            guild->BroadcastToGuild(player->GetSession(), officerOnly, text, LANG_UNIVERSAL);
            uint32 kind = KindFromName(channel);
            CaptureBypassed(player, kind, text,
                            [&](Player* w) { return GuildCanHear(guild, w, kind); });
        }
        else
            return "unknown chat channel";

        status = "delivered";
        return "";
    }

    // Ask a LIVING character what it is doing, and answer from memory.
    //
    // WHY THIS EXISTS. Every check on this family used to go through
    // acore_characters, which is up to fifteen minutes stale - PlayerSaveInterval
    // is 900000 and each player's save timer is staggered from its own login.
    // After the training pass in #2756 the database reported a paladin with zero
    // spells for a quarter of an hour after he had been taught them. Worse in the
    // other direction: a command reports `delivered` the moment it is handed
    // over, so `talents spec prot pve` reported success and did nothing at all.
    //
    // Nothing here mutates. A probe cannot be the reason an experiment appeared
    // to work, which is the whole point of separating it from kind='bot'.
    static std::string ProbeState(Player* bot)
    {
        std::ostringstream o;
        Unit* target = bot->GetSelectedUnit();
        Unit* victim = bot->GetVictim();
        Group* group = bot->GetGroup();

        o << "{";
        o << "\"name\":" << J(bot->GetName());
        o << ",\"level\":" << uint32(bot->GetLevel());
        o << ",\"class\":" << uint32(bot->getClass());
        o << ",\"alive\":" << (bot->IsAlive() ? "true" : "false");
        o << ",\"health\":" << bot->GetHealth() << ",\"max_health\":" << bot->GetMaxHealth();
        o << ",\"power\":" << bot->GetPower(bot->getPowerType());
        o << ",\"max_power\":" << bot->GetMaxPower(bot->getPowerType());
        o << ",\"in_combat\":" << (bot->IsInCombat() ? "true" : "false");
        o << ",\"mounted\":" << (bot->IsMounted() ? "true" : "false");
        // Stance matters for a warrior tank: Defensive Stance is the difference
        // between holding a mob and merely standing near it.
        o << ",\"shapeshift\":" << uint32(bot->GetShapeshiftForm());
        o << ",\"money\":" << bot->GetMoney();
        o << ",\"zone\":" << bot->GetZoneId() << ",\"area\":" << bot->GetAreaId();
        o << ",\"map\":" << bot->GetMapId();
        o << ",\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY()
          << ",\"z\":" << bot->GetPositionZ();
        o << ",\"target\":" << (target ? J(target->GetName()) : "null");
        o << ",\"victim\":" << (victim ? J(victim->GetName()) : "null");
        o << ",\"free_talent_points\":" << bot->GetFreeTalentPoints();
        o << ",\"in_group\":" << (group ? "true" : "false");
        if (group)
        {
            Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
            o << ",\"group_leader\":" << (leader ? J(leader->GetName()) : "null");
            o << ",\"group_size\":" << uint32(group->GetMembersCount());
        }
        o << ",\"has_bot_ai\":" << (GET_PLAYERBOT_AI(bot) ? "true" : "false");
        o << "}";
        return o.str();
    }

    static std::string ProbeSpells(Player* bot)
    {
        std::ostringstream o;
        uint32 count = 0;
        o << "{\"spells\":[";
        for (auto const& entry : bot->GetSpellMap())
        {
            // Removed and inactive entries are spells the character has lost or
            // superseded; counting them would inflate the number the probe
            // exists to check. This is upstream's own filter verbatim
            // (ListSpellsAction) - PlayerSpell has `Active`, and the `disabled`
            // this once read does not exist on it, which failed the build.
            if (entry.second->State == PLAYERSPELL_REMOVED || !entry.second->Active)
                continue;
            if (count++)
                o << ",";
            o << entry.first;
        }
        o << "],\"count\":" << count << "}";
        return o.str();
    }

    static std::string ProbeTalents(Player* bot)
    {
        std::ostringstream o;
        // Points per tree, counted the way AiFactory::GetPlayerSpecTab counts
        // them, because that is what decides which strategies the bot runs.
        uint32 perTab[3] = {0, 0, 0};
        for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
        {
            TalentEntry const* talent = sTalentStore.LookupEntry(i);
            if (!talent)
                continue;
            TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
            if (!tab || tab->tabpage > 2)
                continue;
            if ((bot->getClassMask() & tab->ClassMask) == 0)
                continue;
            for (uint32 rank = 0; rank < MAX_TALENT_RANK; ++rank)
                if (talent->RankID[rank] && bot->HasSpell(talent->RankID[rank]))
                    perTab[tab->tabpage] += rank + 1;
        }
        uint32 best = 0;
        for (uint32 t = 1; t < 3; ++t)
            if (perTab[t] > perTab[best])
                best = t;

        o << "{\"per_tab\":[" << perTab[0] << "," << perTab[1] << "," << perTab[2] << "]";
        o << ",\"spec_tab\":" << best;
        o << ",\"free_points\":" << bot->GetFreeTalentPoints() << "}";
        return o.str();
    }

    // The LIVE strategy lists, off the engines themselves. Not the persisted
    // copy in playerbots_db_store, which is written on change and can lag or
    // disagree with what the bot is running this second.
    static std::string ProbeStrategies(Player* bot)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return "{\"error\":\"no bot ai\"}";

        std::ostringstream o;
        o << "{";
        bool firstState = true;
        for (auto const& pair : {std::make_pair("combat", BOT_STATE_COMBAT),
                                 std::make_pair("non_combat", BOT_STATE_NON_COMBAT),
                                 std::make_pair("dead", BOT_STATE_DEAD)})
        {
            if (!firstState)
                o << ",";
            firstState = false;
            o << J(pair.first) << ":[";
            bool first = true;
            for (std::string const& name : botAI->GetStrategies(pair.second))
            {
                if (!first)
                    o << ",";
                first = false;
                o << J(name);
            }
            o << "]";
        }
        o << "}";
        return o.str();
    }

    // Equipped items and how broken they are. "Should this character go and
    // repair" is otherwise a guess, and it has already been guessed wrong.
    static std::string ProbeGear(Player* bot)
    {
        std::ostringstream o;
        o << "{\"equipped\":[";
        bool first = true;
        uint32 broken = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;
            ItemTemplate const* proto = item->GetTemplate();
            uint32 const cur = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
            uint32 const max = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
            if (max && cur == 0)
                ++broken;
            if (!first)
                o << ",";
            first = false;
            o << "{\"slot\":" << uint32(slot);
            o << ",\"entry\":" << item->GetEntry();
            o << ",\"name\":" << J(proto ? proto->Name1 : "");
            o << ",\"quality\":" << (proto ? proto->Quality : 0);
            o << ",\"durability\":" << cur << ",\"max_durability\":" << max << "}";
        }
        o << "],\"broken\":" << broken << "}";
        return o.str();
    }

    // Free space, and what is worth selling. Grey items are the junk a
    // character is supposed to take to a vendor.
    static std::string ProbeBags(Player* bot)
    {
        uint32 slots = 0;
        uint32 used = 0;
        uint32 junk = 0;
        uint32 junkValue = 0;

        auto visit = [&](Item* item)
        {
            if (!item)
                return;
            ++used;
            ItemTemplate const* proto = item->GetTemplate();
            if (proto && proto->Quality == ITEM_QUALITY_POOR)
            {
                ++junk;
                junkValue += proto->SellPrice * item->GetCount();
            }
        };

        // The backpack.
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            ++slots;
            visit(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        }
        // Then each equipped bag, which is where the space actually comes from -
        // a character with no bags has 16 slots and fills them in an hour.
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        {
            Bag* bag = bot->GetBagByPos(bagSlot);
            if (!bag)
                continue;
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
            {
                ++slots;
                visit(bag->GetItemByPos(i));
            }
        }

        std::ostringstream o;
        o << "{\"slots\":" << slots << ",\"used\":" << used
          << ",\"free\":" << (slots - used)
          << ",\"junk\":" << junk << ",\"junk_copper\":" << junkValue << "}";
        return o.str();
    }

    static char const* DoProbe(Player* bot, std::string const& what, char const*& status,
                               std::string& out)
    {
        if (what == "state")
            out = ProbeState(bot);
        else if (what == "spells")
            out = ProbeSpells(bot);
        else if (what == "talents")
            out = ProbeTalents(bot);
        else if (what == "strategies")
            out = ProbeStrategies(bot);
        else if (what == "gear")
            out = ProbeGear(bot);
        else if (what == "bags")
            out = ProbeBags(bot);
        else
            return "unknown probe (state|spells|talents|strategies|gear|bags)";

        status = "delivered";
        return "";
    }

    // Run a dot-command through the character's OWN session, so it carries
    // that account's real security level. A non-GM account is refused by the
    // core exactly as if the player had typed it.
    static char const* DoGmCommand(Player* player, std::string const& command, char const*& status)
    {
        WorldSession* session = player->GetSession();
        if (!session)
            return "character has no session";
        if (command.empty() || command[0] != '.')
            return "gm command must start with a dot";

        ChatHandler handler(session);
        // ParseCommands takes the command WITH its leading dot: it rejects
        // anything that does not start with '.' or '!'.
        if (!handler.ParseCommands(command))
            return "command not found";

        // ParseCommands returning true means only that the input was HANDLED
        // as a command - a handler that refused, or that was given a bad
        // target or bad arguments, reports the error to the session and still
        // returns true. Reporting that as "done" would tell Discord a state
        // change happened when it did not. HasSentErrorMessage is the real
        // outcome.
        if (handler.HasSentErrorMessage())
            return "command failed or was refused (detail went to the game chat log)";

        status = "delivered";
        return "";
    }

    void WriteSnapshot()
    {
        auto const& allPlayers = ObjectAccessor::GetPlayers();

        std::ostringstream ss;
        bool first = true;
        for (auto const& itr : allPlayers)
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld())
                continue;

            Group* group = p->GetGroup();
            if (first)
                ss << "REPLACE INTO overseer_snapshot (guid, name, level, race, class, "
                      "map_id, zone_id, area_id, pos_x, pos_y, pos_z, health, max_health, "
                      "in_combat, is_bot, guild_id, group_leader, target_guid) VALUES ";
            else
                ss << ',';
            first = false;

            ss << '(' << p->GetGUID().GetCounter()
               << ",'" << p->GetName() << "',"
               << uint32(p->GetLevel()) << ','
               << uint32(p->getRace()) << ','
               << uint32(p->getClass()) << ','
               << p->GetMapId() << ','
               << p->GetZoneId() << ','
               << p->GetAreaId() << ','
               << p->GetPositionX() << ','
               << p->GetPositionY() << ','
               << p->GetPositionZ() << ','
               << p->GetHealth() << ','
               << p->GetMaxHealth() << ','
               << (p->IsInCombat() ? 1 : 0) << ','
               << (GET_PLAYERBOT_AI(p) ? 1 : 0) << ','
               << p->GetGuildId() << ','
               << (group ? group->GetLeaderGUID().GetCounter() : 0) << ','
               << p->GetTarget().GetCounter()
               << ')';
        }

        if (!first)
            CharacterDatabase.Execute(ss.str().c_str());

        // Logged-out characters age out rather than being tracked explicitly:
        // one sweep interval of grace, then the map stops drawing them.
        CharacterDatabase.Execute(
            "DELETE FROM overseer_snapshot WHERE updated_at < NOW() - INTERVAL 60 SECOND");
    }

    uint32 _commandTimer = 0;
    uint32 _snapshotTimer = 0;
    uint32 _watchTimer = 0;
    uint32 _sweepTimer = 0;
    uint32 _chatFlushTimer = 0;
    // Seeded so the first roster check runs one poll after startup rather than
    // immediately: the character cache and the world are still settling in the
    // first ticks, and a login queued into that is a login that quietly fails.
    uint32 _rosterTimer = 0;
    uint32 _partyTimer = 0;
    uint32 _trainTimer = 0;
    uint32 _questTimer = 0;
    bool _watchLoaded = false;
};

void Addmod_overseerScripts()
{
    new OverseerWorldScript();
    new OverseerChatScript();
}
