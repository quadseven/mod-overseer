/*
 * mod-overseer: the bridge between the outside world and the simulation.
 *
 * Five jobs, all running inside the worldserver:
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
 *       kind='probe' - read-only. Answers from the live Player*, never from
 *                     the up-to-fifteen-minutes-stale characters table.
 *       kind='give' - move one item from target_name's bags into target_arg's,
 *                     inside a CharacterDatabase transaction. There is no chat
 *                     command that can do this between two bots; see the
 *                     migration 2026_08_24_00_overseer_give.sql for why.
 *       kind='share' - put a quest target_name is carrying into target_arg's
 *                     quest log, behind the core's own eligibility checks.
 *                     mod-playerbots' two sharing paths both need a master and
 *                     these bots have none, and the packet path would make an
 *                     unconditional null-master dereference reachable
 *                     (AcceptQuestAction.cpp:139). Neither divider nor packet
 *                     is touched here; see 2026_08_24_03_overseer_share.sql.
 *
 *     WHAT A FINISHED ROW CLAIMS. `delivered` means the module carried the
 *     row out. It has never meant the BOT CHANGED, because a whispered
 *     command is only queued when HandleCommand accepts it and acts a tick
 *     later - and it was read as "applied" anyway, for a whole night, by two
 *     people. So a strategy command (`nc +x`, `nc -x`, `co +x`, `co -x`) now
 *     parks in `verifying`, its effect is read back off the live engine, and
 *     it ends as `applied` or `unchanged`. See the `outcome` section below
 *     and 2026_08_24_04_overseer_outcome.sql for the whole argument.
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
 *  4. Event record. Discrete things that HAPPEN to a roster character - a
 *     level gained, a quest taken or finished or handed in, an item equipped,
 *     a death - are written to acore_characters.overseer_event as they occur.
 *     This exists because every diagnosis in this epic so far has ended at a
 *     screenshot: the server knew the fact and kept no record of it. Scoped to
 *     overseer_roster and de-duplicated per hour; the migration
 *     2026_08_24_02_overseer_event.sql carries the reasoning in full.
 *
 *  5. Retention. Chat and events are swept on a timer. A 500-bot world talks
 *     constantly and nothing here is worth keeping for long.
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
 *
 * The event hooks are on the same footing and follow the same discipline: a
 * bot levels up and dies inside Player::Update, so those hooks land on a map
 * thread too. They do NO database work. They take a lock, touch memory, and
 * leave; the world thread does the INSERT. This is not fastidiousness - the
 * worldserver was measured at 285% CPU with the map pool doing the work, and
 * DatabaseWorkerPool::EscapeString borrows the shared synchronous connection
 * with no lock of its own, so a query from a map thread is both a stall and a
 * race.
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
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
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

// HOW MANY TIMES A QUEST MAY BE CHOSEN AND ABANDONED BEFORE WE STOP CHOOSING
// IT (infra#2801). Measured on the live realm: the leader was handed quest 109
// thirty-eight times in twelve minutes and travelled 0.0 yards, because its
// turn-in is in Westfall and he is in Elwynn. Three is a compromise - one
// idle-out is ordinary (combat, death, the quest leaving the log), and waiting
// longer than a minute to notice a wedge is a minute of a character doing
// nothing.
constexpr uint8 QUEST_REPICK_STRIKES = 3;

// How long a given-up quest stays given up. A quest unreachable from Elwynn is
// reachable from Goldshire: the bot moves, the world moves, and a refusal that
// outlives its reason is its own bug. Fifteen minutes is long enough that a
// wedge does not churn and short enough that a walk to the next zone gets a
// fresh chance.
constexpr time_t QUEST_GIVE_UP_COOLDOWN_SECONDS = 900;

// How far the traveller must have got from where it stood when a quest was
// chosen for the attempt to count as "tried" rather than "wedged".
//
// THIS NUMBER IS GENEROUS ON PURPOSE, and a small one would have quietly
// disabled the whole feature. The failure path MOVES THE BOT: when MoveFarTo
// cannot resolve a route, upstream nudges with MoveRandomNear(10.0f)
// (NewRpgAction.cpp:342, :357, :513, :607) so the next tick starts somewhere
// else. The AI ticks many times inside one twenty-second poll, so a bot going
// nowhere still random-walks a respectable distance. A threshold near the
// ten-yard nudge radius therefore resets the strike count on exactly the bot
// it exists to catch, and the result is a silent no-op that looks like the
// feature working.
//
// Being generous costs almost nothing, because `!onQuest` already does most
// of the discriminating: a bot on a long slow route stays IN RPG_DO_QUEST and
// never reaches this code at all. This check only has to avoid striking a bot
// that is demonstrably going somewhere, and directional travel clears forty
// yards easily while a bounded random walk does not.
constexpr float QUEST_PROGRESS_YARDS = 45.0f;

// How long a chosen quest is allowed to sit unfulfilled before the aim is given
// up on. The PRIMARY release is the aimed quest being handed in; this is only
// the backstop for an aim that can never land - a quest sharing never delivered,
// a beneficiary who abandoned it, a council decision nothing can satisfy. It is
// deliberately longer than mod-playerbots' own statusDoQuestDuration of 30
// minutes (NewRpgAction.h:68), so an aim is never released merely because the
// RPG lease lapsed: that is what re-assertion is for.
constexpr time_t DRIVE_AIM_BACKSTOP_SECONDS = 60 * 60;

// The highest DBC talent tabpage. spec_tab holds a tabpage, and anything above
// this means no tree was chosen - which is a decision the module must not make
// on a named character's behalf.
constexpr uint8 MAX_TALENT_TAB = 2;
// Fast enough that a relayed conversation still feels live.
constexpr uint32 CHAT_FLUSH_MS = 1000;
// One world tick must never stall on a burst of queued commands.
constexpr uint32 COMMANDS_PER_POLL = 20;
// How long a strategy command may go unobserved before the queue says so
// (infra#2819). The bot's AI ticks far faster than this; the window is three
// command polls rather than one so that a bot which happens to be mid-anything
// is not reported as broken for being a tick late. A check that starts holding
// earlier is resolved earlier - this is only the point at which "it still has
// not changed" becomes the answer instead of the question.
constexpr uint32 VERIFY_GRACE_MS = 6000;
// WoW's own chat limit, in BYTES - which is what the client sends and what
// std::string measures. Escaping can nearly double it (a backslash per quote),
// and overseer_chat.text is VARCHAR(512) CHARACTERS, so the worst case still
// fits with room to spare.
constexpr size_t MAX_CHAT_BYTES = 255;
// Relayed lines are the bridge's problem once sent; unrelayed ones are kept
// a little longer so a bridge outage does not lose the conversation.
constexpr uint32 CHAT_RETENTION_MINUTES = 180;

// How often queued events are written. Slower than the chat flush on purpose:
// chat is a live conversation somebody is reading in Discord, events are a
// record somebody queries after the fact. A longer window is also a bigger
// one to coalesce repeats in, so the slower timer costs fewer rows, not more.
constexpr uint32 EVENT_FLUSH_MS = 5000;

// A fortnight. Long enough that "when did this start" has an answer for
// anything anyone is still arguing about, short enough that a bug firing every
// ten seconds cannot grow the table without bound.
constexpr uint32 EVENT_RETENTION_DAYS = 14;

// The hour an event fell in, as the grouping half of the unique key. See
// 2026_08_24_02_overseer_event.sql for why the timeline is coarsened to an
// hour rather than collapsed entirely or kept per-occurrence.
constexpr uint32 EVENT_BUCKET_SECONDS = 3600;

// DISTINCT keys held between two flushes, not occurrences - a repeat costs
// nothing, because it increments a counter on a key that is already there.
// Five characters producing 500 genuinely different events in five seconds is
// not a busy world, it is a bug, and the bound is what stops that bug becoming
// an out-of-memory instead of a log line.
constexpr size_t MAX_EVENT_KEYS = 500;

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

/*
 * ---------------------------------------------------------------------------
 * The event record (infra#2597).
 * ---------------------------------------------------------------------------
 *
 * THE ROSTER GATE IS THE FIRST AND LARGEST VOLUME CONTROL. The player hooks
 * below fire for every one of the 500 random bots as well as for the family of
 * five, so the very first thing every hook does is ask whether this character
 * is one of ours. Everything else - resolving a quest title, taking the queue
 * lock, formatting a string - happens only after that question is answered
 * yes, roughly a hundred times less often than it is asked.
 *
 * The roster is cached rather than queried, because the alternative is a
 * SELECT per player event on a 500-bot world, which is exactly the synchronous
 * per-event query the world loop cannot afford. It is refreshed from
 * overseer_roster on the poll that already reads that table, so this adds no
 * query of its own.
 *
 * NAMES ARE COMPARED CASE-INSENSITIVELY. overseer_roster is written by the
 * bridge and Player::GetName returns the canonical capitalisation the world
 * uses; if those two ever disagreed by a letter's case the hooks would record
 * nothing at all, forever, with nothing in the logs to say so. That is the
 * precise failure this whole change exists to abolish, so it is not left to
 * chance for the sake of a strcmp.
 */

std::string LowerName(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::mutex g_rosterMutex;
std::set<std::string> g_rosterLower;
// Lock-free early-out, same trick as g_watchUnion: before the roster has ever
// been read there is nobody to record, and a stale read costs one needless
// locked lookup rather than a wrong answer.
std::atomic<bool> g_rosterKnown{false};

void SetRosterNames(std::vector<std::string> const& names)
{
    std::lock_guard<std::mutex> guard(g_rosterMutex);
    g_rosterLower.clear();
    for (std::string const& name : names)
        g_rosterLower.insert(LowerName(name));
    g_rosterKnown.store(!g_rosterLower.empty(), std::memory_order_relaxed);
}

bool OnRoster(std::string const& name)
{
    if (!g_rosterKnown.load(std::memory_order_relaxed))
        return false;
    std::lock_guard<std::mutex> guard(g_rosterMutex);
    return g_rosterLower.find(LowerName(name)) != g_rosterLower.end();
}

// Exactly the unique key of overseer_event. Keeping the two identical is what
// makes the in-memory coalescing and the ON DUPLICATE KEY UPDATE agree: a
// repeat that collapses in RAM is the same repeat that would collapse in the
// database, so a flush boundary never changes what the table ends up holding.
struct EventKey
{
    std::string characterName;
    std::string kind;
    uint32 subjectId = 0;
    uint32 bucket = 0;

    bool operator<(EventKey const& other) const
    {
        return std::tie(characterName, kind, subjectId, bucket) <
               std::tie(other.characterName, other.kind, other.subjectId, other.bucket);
    }
};

struct PendingEvent
{
    std::string subjectName;
    std::string detail;
    uint32 characterGuid = 0;
    uint32 zoneId = 0;
    uint32 occurrences = 0;
    uint16 mapId = 0;
    uint8 level = 0;
};

// A MAP, not a vector, and that is the whole de-duplication design. The chat
// queue is a vector because every line is different and losing one loses
// speech. Events repeat: the axe error fired every ten seconds forever, and a
// vector would have grown a copy of the same sentence six times a minute until
// the cap dropped it. Keyed, the hundredth occurrence is an increment.
std::mutex g_eventMutex;
std::map<EventKey, PendingEvent> g_eventQueue;
uint64 g_droppedEvents = 0;

// The one place an event is captured. Runs on whatever thread the event
// happened on - for bots, a map-update thread - so it does NO database work
// and does not resolve anything it was not handed.
void RecordEvent(Player* actor, char const* kind, uint32 subjectId,
                 std::string const& subjectName, std::string const& detail)
{
    if (!actor || !kind)
        return;
    if (!OnRoster(actor->GetName()))
        return;

    EventKey key;
    key.characterName = actor->GetName();
    key.kind = kind;
    key.subjectId = subjectId;
    key.bucket = static_cast<uint32>(std::time(nullptr) / EVENT_BUCKET_SECONDS);

    std::lock_guard<std::mutex> guard(g_eventMutex);
    auto it = g_eventQueue.find(key);
    if (it == g_eventQueue.end())
    {
        if (g_eventQueue.size() >= MAX_EVENT_KEYS)
        {
            ++g_droppedEvents;
            return;
        }
        it = g_eventQueue.emplace(key, PendingEvent()).first;
    }

    // The LAST occurrence in a bucket wins for everything outside the key.
    // Where a character was and what level it had when a thing last happened
    // is the useful answer; where it was the first of forty times is not.
    PendingEvent& pending = it->second;
    pending.subjectName = subjectName;
    pending.detail = detail;
    pending.characterGuid = actor->GetGUID().GetCounter();
    pending.zoneId = actor->GetZoneId();
    pending.mapId = static_cast<uint16>(actor->GetMapId());
    pending.level = actor->GetLevel();
    ++pending.occurrences;
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

/*
 * The event record's observation points.
 *
 * EVERY HOOK HERE WAS VERIFIED AGAINST THE PINNED CORE, declaration and call
 * site both, because the C++ in this repository only compiles on a push to
 * main - a member that does not exist is not a failed check on a pull request,
 * it is a broken main branch. Core efe123fab543c5faf3c477674ec17a18fd59f09f:
 *
 *   OnPlayerLevelChanged          PlayerScript.h:267   Player.cpp:2583
 *   OnPlayerQuestAccept           PlayerScript.h:752   PlayerQuest.cpp:426
 *   OnPlayerBeforeQuestComplete   PlayerScript.h:457   PlayerQuest.cpp:618
 *   OnPlayerCompleteQuest         PlayerScript.h:249   PlayerQuest.cpp:903
 *   OnPlayerEquip                 PlayerScript.h:418   PlayerStorage.cpp:2928,
 *                                                      2936, 2960
 *   OnPlayerJustDied              PlayerScript.h:234   Player.cpp:4651
 *
 * TWO OF THOSE NAMES LIE, AND BOTH LIES MATTER.
 *
 * OnPlayerCompleteQuest is NOT "the quest's objectives are done". Its call
 * site is the last line of Player::RewardQuest (PlayerQuest.cpp:675-904), so
 * it fires when a quest is HANDED IN and paid out. It is recorded here as
 * 'quest_reward' for that reason; calling it quest_complete would have put a
 * turn-in under a name that means something else, and a report built on that
 * column would have been quietly wrong about when the family finished things.
 *
 * OnPlayerBeforeQuestComplete IS the objectives-satisfied moment - it is the
 * first thing Player::CompleteQuest does (PlayerQuest.cpp:611-621) - but it is
 * a GATE hook returning bool, not an observer. It is used the same way this
 * module already uses OnPlayerCanUseChat: as the only available vantage point,
 * returning true unconditionally so it can never veto anything. If it ever
 * returned false the family would stop being able to complete quests at all,
 * so the return is a bare `return true` on its own line and should stay that
 * way.
 *
 * SPELL CAST FAILURES ARE ABSENT, AND THAT IS THE POINT OF THIS COMMENT.
 * They are the reason this table was asked for: "Must have a Axe equipped"
 * repeated every ten seconds for hours and had to be read off a screenshot,
 * because the server computed the refusal and wrote it down nowhere. There is
 * no hook at the pinned SHA that can see it. AllSpellScript::OnSpellCheckCast
 * (AllSpellScript.h:57) is the ONLY hook in the entire core taking a
 * SpellCastResult - verified by grepping ScriptMgr.h, which declares exactly
 * one (ScriptMgr.h:650) - and Spell::CheckCast calls it at the very top with
 * `res` initialised to SPELL_CAST_OK immediately beforehand
 * (Spell.cpp:5673-5678). It is an input: a chance for a script to REFUSE a
 * cast. The real reason is computed in the six hundred lines after it and goes
 * straight to Spell::SendCastResult (Spell.cpp:3546 from prepare, 3854 from
 * cast) with no script call in between. AllSpellScript::CanPrepare is no help
 * either - Spell.cpp:3465, sixty-five lines BEFORE the CheckCast whose result
 * it would need. Nor does the module side hold it: mod-playerbots at
 * 8d9f6aa6bc6d45f9ae0ee0675b9b1f8aa6937312 keeps no last-cast-result anywhere
 * in PlayerbotAI.h.
 *
 * Recording it therefore needs a core patch adding a hook at SendCastResult -
 * which production/docker/azerothcore-playerbots/patches/ now makes possible,
 * and which is deliberately NOT bundled here. A patch that touches ScriptMgr,
 * AllSpellScript and Spell.cpp is exactly the kind of change that discovers it
 * was wrong ninety minutes into a compile on main. The kind string 'cast_fail'
 * is reserved in the migration so the reporting layer can be built against it
 * now and the column meanings cannot drift when it lands.
 */
class OverseerEventScript : public PlayerScript
{
public:
    // Only the six hooks this script implements. An empty list would enable
    // ALL player hooks, which on a 500-bot world puts this script in the
    // dispatch loop for every player event the core has.
    OverseerEventScript() : PlayerScript("OverseerEventScript", {
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_PLAYER_QUEST_ACCEPT,
        PLAYERHOOK_ON_BEFORE_QUEST_COMPLETE,
        PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
        PLAYERHOOK_ON_EQUIP,
        PLAYERHOOK_ON_PLAYER_JUST_DIED,
    }) {}

    // Fires on the way DOWN as well - the hook is named for a change, not a
    // gain - so the level reached is the subject and the level left is the
    // detail. That way a row can never be ambiguous about which way it went.
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!player)
            return;
        RecordEvent(player, "level_up", player->GetLevel(), "",
                    "from " + std::to_string(static_cast<uint32>(oldLevel)));
    }

    void OnPlayerQuestAccept(Player* player, Quest const* quest) override
    {
        if (!player || !quest)
            return;
        RecordEvent(player, "quest_accept", quest->GetQuestId(), quest->GetTitle(), "");
    }

    // Objectives satisfied. A gate hook used as an observer - see above.
    bool OnPlayerBeforeQuestComplete(Player* player, uint32 questId) override
    {
        if (player)
        {
            // GetQuestTemplate rather than carrying a title from elsewhere:
            // this hook is handed an id and nothing else, and the same lookup
            // is already how DriveQuests names a quest.
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            RecordEvent(player, "quest_complete", questId,
                        quest ? quest->GetTitle() : "", "");
        }
        return true;
    }

    // Handed in and paid out.
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        if (!player || !quest)
            return;
        RecordEvent(player, "quest_reward", quest->GetQuestId(), quest->GetTitle(), "");
    }

    void OnPlayerEquip(Player* player, Item* item, uint8 /*bag*/, uint8 slot,
                       bool /*update*/) override
    {
        if (!player || !item)
            return;

        // A LOGIN IS NOT AN EQUIP. Player::_LoadInventory puts every worn item
        // back on through QuickEquipItem (PlayerStorage.cpp:6000), which calls
        // this hook with update=true unconditionally (PlayerStorage.cpp:2960)
        // - so the `update` argument cannot be used to tell the two apart, and
        // without this test every restart would file seventeen item_equip rows
        // per character for gear nobody touched. PlayerLoading is the core's
        // own test for the same situation; Spell::SendCastResult uses it to
        // decide not to report cast failures during a load (Spell.cpp:4691).
        if (player->GetSession() && player->GetSession()->PlayerLoading())
            return;

        ItemTemplate const* proto = item->GetTemplate();
        RecordEvent(player, "item_equip", item->GetEntry(), proto ? proto->Name1 : "",
                    "slot " + std::to_string(static_cast<uint32>(slot)));
    }

    // Deaths carry no subject, so every death in an hour lands on one row with
    // a count. That is deliberate: "Grug died nine times between 02:00 and
    // 03:00" is the observation worth having, and nine rows saying "died" with
    // nothing to tell them apart is not nine times more information.
    void OnPlayerJustDied(Player* player) override
    {
        if (!player)
            return;
        RecordEvent(player, "death", 0, "", "");
    }
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
        _eventTimer += diff;

        // Load the watch list before the first poll, not 30s after startup.
        if (!_watchLoaded)
        {
            _watchLoaded = true;
            ReloadWatchList();
            // And the roster, for the same reason and a sharper one: the event
            // hooks record NOTHING until they know who is on it, so leaving
            // this to the 30-second roster poll would mean a blind window
            // after every restart - which is precisely when a character is
            // most likely to do something worth having a record of.
            ReloadRosterNames();
        }
        if (_commandTimer >= COMMAND_POLL_MS)
        {
            // The REAL elapsed time, read before the reset. The read-back
            // window is measured in it, and a world under load overshoots
            // COMMAND_POLL_MS - reporting the nominal interval would make
            // `waited_ms` a restatement of the constant instead of a fact.
            uint32 const sincePoll = _commandTimer;
            _commandTimer = 0;
            DeliverPendingCommands(sincePoll);
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
        if (_eventTimer >= EVENT_FLUSH_MS)
        {
            _eventTimer = 0;
            FlushEvents();
        }
        if (_sweepTimer >= CHAT_SWEEP_MS)
        {
            _sweepTimer = 0;
            CharacterDatabase.Execute(
                "DELETE FROM overseer_chat WHERE created_at < NOW() - INTERVAL {} MINUTE",
                CHAT_RETENTION_MINUTES);
            // Swept on last_seen, not first_seen: a row that is still being
            // incremented is a problem that is still happening, and deleting
            // it because it started three weeks ago would restart its history
            // and lose the one fact that made it interesting.
            CharacterDatabase.Execute(
                "DELETE FROM overseer_event WHERE last_seen < NOW() - INTERVAL {} DAY",
                EVENT_RETENTION_DAYS);
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
    // Read the roster and publish it to the event hooks. Split out of
    // KeepRosterOnline so startup can prime the hooks without also queueing
    // logins into the first ticks, which the roster timer's seeding
    // deliberately avoids.
    std::vector<std::string> ReloadRosterNames()
    {
        std::vector<std::string> names;
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT name FROM overseer_roster WHERE enabled = 1"))
        {
            do
            {
                names.push_back(result->Fetch()[0].Get<std::string>());
            } while (result->NextRow());
        }
        SetRosterNames(names);
        return names;
    }

    void KeepRosterOnline()
    {
        // The whole list is read before any login is attempted, because the
        // login loop stops after ROSTER_LOGINS_PER_POLL and the event hooks
        // need the names of everybody on the roster, not of the first three.
        std::vector<std::string> const names = ReloadRosterNames();

        uint32 started = 0;
        for (std::string const& name : names)
        {
            if (started >= ROSTER_LOGINS_PER_POLL)
                break;

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
        }
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
    // WHERE THE CHOSEN QUEST COMES FROM. The council decides, the supervisor
    // persists the decision, and the bridge writes the quest id onto the
    // traveller's roster row (bridge._aim_traveller, renewed by
    // goals._reconcile_quest; migration
    // 2026_08_24_00_overseer_roster_drive_quest.sql). Until this change nothing
    // read that column: the family agreed in party chat to get Ugga her last
    // Large Candle and then stood still, 0.0 yards in 45 seconds, eleven yards
    // from the kobolds that drop it. The decision was made, persisted, and
    // dropped on the floor one step short of the ground.
    //
    // `drive_quest` = 0 means "no opinion", and that is the case that still
    // picks the first eligible quest out of the leader's own log. A stale or
    // unreachable aim degrades to that same behaviour rather than to standing
    // still - which is the whole reason the fallback is kept.
    //
    // The column and this reader ship in the same image: mod-overseer applies
    // its own SQL at worldserver startup, so the two cannot disagree. The
    // bridge is a separate deployment and guards its write on 1054 instead.
    void DriveQuests()
    {
        QueryResult result = CharacterDatabase.Query(
            // EVERY enabled member, not only the leader (infra#2801, "quest
            // together"). Handing a quest in is reachable only through the rpg
            // strategy, so a follower that cannot be aimed cannot ever turn
            // anything in - which is why the four hoarded completed quests
            // while the leader handed his own in. `lead` still comes back
            // because the FALLBACK below stays leader-only: an unaimed
            // follower carrying `new rpg` is the 937-yard scatter, and the aim
            // is the only thing holding the party to one destination.
            "SELECT name, drive_quest, `lead` FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            std::string const name = fields[0].Get<std::string>();
            uint32 const aim = fields[1].Get<uint32>();
            bool const isLead = fields[2].Get<uint8>() != 0;

            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!bot)
                continue;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI)
                continue;

            // What it is working on RIGHT NOW, which is a different question
            // from whether it is questing at all. RPG_DO_QUEST self-expires
            // after statusDoQuestDuration = 30 minutes (NewRpgAction.h:68,
            // checked at NewRpgAction.cpp:288) and the bot then re-rolls its
            // own status - including a RANDOM quest out of its log. So an aim
            // is a LEASE, not a one-shot, and knowing which quest the lease
            // currently names is what lets it be re-asserted without
            // restarting the travel every twenty seconds.
            //
            // rpgInfo is a public member of PlayerbotAI (PlayerbotAI.h:603),
            // rpgInfo.data is a public std::variant (NewRpgInfo.h:97), and
            // DoQuest::questId is a public field on it (NewRpgInfo.h:47-54);
            // upstream reads the same variant the same way for TravelFlight
            // (NewRpgAction.cpp:297).
            bool const onQuest = botAI->rpgInfo.GetStatus() == RPG_DO_QUEST;  // NewRpgInfo.h:99
            uint32 working = 0;
            if (onQuest)
            {
                if (NewRpgInfo::DoQuest const* doQuest =
                        std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data))
                    working = doQuest->questId;
            }

            // The aim as this loop last saw it, so a standing complaint is made
            // once rather than three times a minute forever. Nothing here is
            // read as a decision: the roster row remains the only real state.
            AimState& state = _lastAim[name];
            uint32 const previous = state.questId;
            bool const aimChanged = aim != previous;
            if (aimChanged)
            {
                state.questId = aim;
                state.since = std::time(nullptr);
            }

            // THE OPPORTUNISTIC TURN-IN, WHICH IS THE FAILURE THIS EXISTS TO
            // MAKE VISIBLE. Measured in the dev world: a bot given `new rpg`
            // and left alone walked 79 yards, handed in the ONE parked quest
            // whose giver it happened to path past, re-rolled to something
            // else and never came back for the other two - stalled at 79 yards,
            // out of combat, for eight minutes. "A quest got turned in while an
            // aim was set" is therefore NOT evidence the aim did anything, and
            // a reader has to be able to tell the two apart at a glance.
            bool const aimed = aim != 0;
            if (aimed && state.lastWorking && state.lastWorking != aim &&
                bot->GetQuestRewardStatus(state.lastWorking))  // Player.h:1491
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' turned in quest {} while aimed at quest {} - "
                         "that is an opportunistic turn-in, not the errand it was sent "
                         "on", name, state.lastWorking, aim);
            }
            state.lastWorking = working;

            bool stillAimed = aimed;
            if (aimed)
            {
                // PRIMARY RELEASE: the AIMED quest, specifically, was handed
                // in. Not any quest completing - see the turn-in line above -
                // and not a timer. This is what makes a backlog clearable one
                // errand at a time instead of one errand per lucky encounter.
                // GetQuestRewardStatus is the rewarded set, so it stays true
                // after the quest leaves the log (Player.h:1491).
                if (bot->GetQuestRewardStatus(aim))  // Player.h:1491
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' handed in chosen quest {} - releasing the "
                             "aim, the errand is done", name, aim);
                    ClearAim(name);
                    // Clear the AIM, not the whole state: `givenUp`,
                    // `lastPicked` and `strikes` are what this traveller has
                    // learned about quests it cannot reach, and a wholesale
                    // `state = AimState()` threw that away. The backstop
                    // release below fires precisely for an unreachable aim,
                    // and the fallback walk runs in the SAME iteration - so
                    // wiping the memory here handed the quest straight back
                    // (infra#2801 review).
                    state.questId = 0;
                    state.since = 0;
                    state.lastWorking = 0;
                    stillAimed = false;
                }
                // BACKSTOP: an aim that can never land - never shared, since
                // abandoned, or simply impossible - must not pin the traveller
                // to it forever. Releasing hands it back to its own log, which
                // is playing badly rather than not playing at all.
                else if (state.since &&
                         std::time(nullptr) - state.since > DRIVE_AIM_BACKSTOP_SECONDS)
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' was aimed at quest {} for over {} minutes "
                             "without handing it in - releasing the aim as unreachable",
                             name, aim,
                             static_cast<uint32>(DRIVE_AIM_BACKSTOP_SECONDS / 60));
                    ClearAim(name);
                    // Clear the AIM, not the whole state: `givenUp`,
                    // `lastPicked` and `strikes` are what this traveller has
                    // learned about quests it cannot reach, and a wholesale
                    // `state = AimState()` threw that away. The backstop
                    // release below fires precisely for an unreachable aim,
                    // and the fallback walk runs in the SAME iteration - so
                    // wiping the memory here handed the quest straight back
                    // (infra#2801 review).
                    state.questId = 0;
                    state.since = 0;
                    state.lastWorking = 0;
                    stillAimed = false;
                }
            }

            if (stillAimed)
            {
                if (DriveChosenQuest(name, bot, botAI, aim, working, aimChanged))
                    continue;
                // Fell through on purpose: the aim could not land, and a
                // traveller doing something is better than one standing still.
            }
            else if (!aim && aimChanged)
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' quest aim cleared (was quest {}) - back to "
                         "picking from its own log", name, previous);
            }

            // Already on one. Re-issuing would restart the travel from here
            // every poll, which is a character that walks toward an objective
            // forever and never arrives.
            if (onQuest)
                continue;

            // AN UNAIMED FOLLOWER STAYS PUT. Everything below picks a quest out
            // of the character's OWN log, which is per-character and therefore
            // divergent - five bots free-roaming their own logs is precisely
            // the 937-yard spread that taking `new rpg` off them was meant to
            // cure. A follower questing with the family does so because it was
            // aimed at the SAME quest as everyone else; with no aim there is no
            // shared destination, and standing still beats scattering.
            if (!isLead)
                continue;

            // REACHING HERE MEANS THE BOT IS NOT ON A QUEST. If this loop chose
            // one for it on an earlier poll, that choice has since been
            // abandoned by upstream without the quest being finished. Every
            // idle site that can get us here is a trouble signal - :483 and
            // :580 are both "can't find a poi pos", and :547 and :622 have
            // already recorded themselves in lowPriorityQuest - so the only
            // question left is whether the character is going anywhere.
            time_t const nowSec = std::time(nullptr);
            // `auto&`, not `RepickMemory&`: the type is nested inside AimState,
            // so the unqualified name is not in scope here and naming it
            // outright BROKE THE BUILD ON MAIN (infra#2801). `auto&` binds the
            // same reference, needs no qualification, and cannot drift if the
            // struct is ever moved or renamed.
            auto& repick = state.repick;
            uint32 justFailed = 0;
            if (repick.lastPicked)
            {
                // A quest that has left the log is not a wedge and must not be
                // logged as one. The slot walk below would never offer it
                // again, but the strike above is assessed before the walk.
                QuestStatus const heldStatus =
                    bot->GetQuestStatus(repick.lastPicked);  // Player.h:1492
                bool const stillActionable = heldStatus == QUEST_STATUS_INCOMPLETE ||
                                             heldStatus == QUEST_STATUS_COMPLETE;

                float const dx = bot->GetPositionX() - repick.fromX;
                float const dy = bot->GetPositionY() - repick.fromY;
                bool const moved =
                    (dx * dx + dy * dy) > (QUEST_PROGRESS_YARDS * QUEST_PROGRESS_YARDS);

                if (!stillActionable)
                {
                    // Handed in, abandoned, or otherwise gone. Forget it
                    // silently: nothing here is evidence about reachability.
                    repick.strikes = 0;
                    repick.lastPicked = 0;
                }
                else if (moved)
                {
                    // Going somewhere. Restart the count and the origin so the
                    // next streak is measured from where it actually is.
                    repick.strikes = 0;
                    repick.fromX = bot->GetPositionX();
                    repick.fromY = bot->GetPositionY();
                }
                else if (++repick.strikes >= QUEST_REPICK_STRIKES)
                {
                    repick.givenUp[repick.lastPicked] = nowSec;
                    // Every argument is an integer or a string, and the uint8
                    // is cast, exactly as the training log above does: fmt
                    // renders an unsigned char as a character in some
                    // configurations, and this file cannot be compiled on a PR
                    // to find that out.
                    LOG_INFO("module.overseer",
                             "overseer: '{}' gave up on quest {} - chosen {} times and "
                             "abandoned without getting more than {} yards from ({}, {}); "
                             "its turn-in is likely somewhere it cannot route to. Leaving "
                             "it alone for {} minutes",
                             name, repick.lastPicked, static_cast<uint32>(repick.strikes),
                             static_cast<uint32>(QUEST_PROGRESS_YARDS),
                             static_cast<int32>(repick.fromX),
                             static_cast<int32>(repick.fromY),
                             static_cast<uint32>(QUEST_GIVE_UP_COOLDOWN_SECONDS / 60));
                    repick.strikes = 0;
                    repick.lastPicked = 0;
                }
                else
                {
                    // Struck but not yet given up on. THE STRIKE COUNT DECIDES
                    // WHEN TO STOP TRYING; IT MUST NOT DECIDE WHETHER TO TRY
                    // SOMETHING ELSE NOW. Acceptance criterion 2 is that a
                    // quest which has just failed is not re-picked in
                    // preference to an untried one, so it is deferred for this
                    // poll and used only if nothing else is eligible.
                    justFailed = repick.lastPicked;
                }
            }

            // A refusal that outlives its reason is its own bug.
            for (auto it = repick.givenUp.begin(); it != repick.givenUp.end();)
            {
                if (nowSec - it->second >= QUEST_GIVE_UP_COOLDOWN_SECONDS)
                    it = repick.givenUp.erase(it);
                else
                    ++it;
            }

            bool picked = false;
            bool skipped = false;
            uint32 deferred = 0;
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 const questId = bot->GetQuestSlotQuestId(slot);  // Player.h:1510
                if (!questId)
                    continue;

                // UPSTREAM'S OWN GIVE-UP SET, which its picker already honours
                // at NewRpgBaseAction.cpp:1149 and :1241. Without this we hand
                // back the very quest mod-playerbots just rejected, which is
                // the general case of the same defect.
                if (botAI->lowPriorityQuest.find(questId) !=  // PlayerbotAI.h:605
                    botAI->lowPriorityQuest.end())
                {
                    skipped = true;
                    continue;
                }

                // Ours, for the failure upstream never records.
                if (repick.givenUp.count(questId))
                {
                    skipped = true;
                    continue;
                }

                // Deferred, not refused: the quest that just failed goes to
                // the back of this poll rather than being handed straight
                // back ahead of one that has not been tried.
                if (questId == justFailed)
                {
                    deferred = questId;
                    continue;
                }

                // INCOMPLETE means there is work left to do. COMPLETE is
                // deliberately included: the objective is met and the walk back
                // to the giver is the rest of the quest, and a family that
                // never turns anything in is not playing either.
                QuestStatus const status = bot->GetQuestStatus(questId);  // Player.h:1492
                if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;

                LOG_INFO("module.overseer",
                         "overseer: '{}' now working quest {} ({}) - picked from its own "
                         "log, nothing chosen", name, questId, quest->GetTitle());
                botAI->rpgInfo.ChangeToDoQuest(questId, quest);  // NewRpgInfo.h:106

                // Strikes count consecutive failures of the SAME quest, so
                // moving to a different one starts the count again. The
                // position is the baseline the next poll measures against.
                // The origin is set only when the chosen quest CHANGES, so
                // the distance above is measured across the whole strike
                // streak. Rewriting it every poll would shrink the window to
                // twenty seconds, and a random walk wins over twenty seconds.
                if (questId != repick.lastPicked)
                {
                    repick.strikes = 0;
                    repick.fromX = bot->GetPositionX();
                    repick.fromY = bot->GetPositionY();
                }
                repick.lastPicked = questId;
                repick.stuckLogged = false;
                picked = true;
                break;
            }

            // NOTHING UNTRIED WAS ELIGIBLE, so the quest that just failed is
            // better than standing still - it has been struck but not given up
            // on, and a leader carrying only one quest would otherwise stop
            // questing altogether. It keeps its strike count, so if it goes
            // nowhere again it still reaches the give-up.
            if (!picked && deferred)
            {
                // Re-check the status here too. The deferral `continue`d past
                // the walk's own check, so without this the quest reaches
                // ChangeToDoQuest validated only for template existence - and
                // NewRpgDoQuestAction::Execute would hit its `default:` and
                // ChangeToIdle immediately (NewRpgAction.cpp:444), burning a
                // poll and taking a strike for the wrong reason.
                QuestStatus const deferredStatus =
                    bot->GetQuestStatus(deferred);  // Player.h:1492
                Quest const* quest = (deferredStatus == QUEST_STATUS_INCOMPLETE ||
                                      deferredStatus == QUEST_STATUS_COMPLETE)
                                         ? sObjectMgr->GetQuestTemplate(deferred)
                                         : nullptr;
                if (quest)
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' has nothing untried, so it is retrying "
                             "quest {} ({}) - strike {} against it",
                             name, deferred, quest->GetTitle(),
                             static_cast<uint32>(repick.strikes));
                    botAI->rpgInfo.ChangeToDoQuest(deferred, quest);  // NewRpgInfo.h:106
                    repick.lastPicked = deferred;
                    repick.stuckLogged = false;
                    picked = true;
                }
            }

            // EVERY ELIGIBLE QUEST HAS BEEN GIVEN UP ON. This is the leader's
            // real state, not a hypothetical: all three of his completed
            // quests turn in outside his zone. Saying so once is the difference
            // between a diagnosable stall and the silent one that hid this bug
            // for a day - and falling through to `grind` is playing badly,
            // which beats not playing at all.
            if (!picked && skipped && !repick.stuckLogged)
            {
                std::ostringstream ids;
                for (auto const& [questId, when] : repick.givenUp)
                {
                    if (ids.tellp())
                        ids << ", ";
                    ids << questId;
                }
                LOG_INFO("module.overseer",
                         "overseer: '{}' has given up on every quest it can act on "
                         "(ours: {}), so it has nothing to work and will fall back to "
                         "whatever else its strategies choose",
                         name, ids.str().empty() ? std::string("none") : ids.str());
                repick.stuckLogged = true;
            }
        } while (result->NextRow());
    }

    // Give the aim back. The bridge owns this column and will aim again the
    // moment the council decides again; what a supervisor polling on its own
    // cadence cannot do is notice a turn-in the instant it happens, and an aim
    // left standing on a finished quest is an aim that can never land - the
    // traveller would be re-aimed at it every twenty seconds forever while
    // DriveChosenQuest refused it every time.
    //
    // Esc() rather than a bare interpolation because every other write in this
    // file does, not because a character name can carry a quote.
    void ClearAim(std::string const& name)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET drive_quest = 0 WHERE name = '{}'", Esc(name));
    }

    // Put the council's chosen quest in front of the traveller, or say why it
    // could not be. Returns true when the aim is in force - just applied, or
    // already running - and false when the caller should fall back to the
    // leader's own log.
    //
    // EVERY PATH LOGS. This epic's defining bug is an action that reports
    // success while doing nothing, and an aim that silently fails to land is
    // that same bug wearing a new hat. The one deliberately silent path is
    // "the lease already names this quest", because that is the steady state
    // and it is reached three times a minute.
    bool DriveChosenQuest(std::string const& name, Player* bot, PlayerbotAI* botAI,
                          uint32 questId, uint32 working, bool aimChanged)
    {
        // THE BOT MUST HOLD THE QUEST. NewRpgDoQuestAction dispatches only on
        // QUEST_STATUS_INCOMPLETE and QUEST_STATUS_COMPLETE and otherwise calls
        // info.ChangeToIdle(), so aiming at a quest the character does not
        // carry is a silent no-op that idles it on the very next tick. Getting
        // the quest into its log is quest sharing's job (kind='share'); until
        // that has happened this says so and gets out of the way.
        QuestStatus const status = bot->GetQuestStatus(questId);  // Player.h:1492
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
        {
            if (aimChanged)
                LOG_INFO("module.overseer",
                         "overseer: '{}' is aimed at quest {} but does not hold it "
                         "(quest status {}) - aiming would idle it on the next tick, so "
                         "it keeps picking from its own log until the quest is shared",
                         name, questId, static_cast<uint32>(status));
            return false;
        }

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            if (aimChanged)
                LOG_INFO("module.overseer",
                         "overseer: '{}' is aimed at quest {}, which this world has no "
                         "template for - falling back to its own log", name, questId);
            return false;
        }

        // The lease already names this quest, and the aim has not moved since
        // we last looked - leave it alone. Re-issuing ChangeToDoQuest on every
        // poll would reset objectiveIdx, pos and lastReachPOI
        // (NewRpgInfo.h:47-54) every twenty seconds, which is a character that
        // walks toward an objective forever and never arrives.
        //
        // BUT NOT WHEN THE AIM HAS JUST CHANGED, and that distinction is the
        // whole errand. Measured in dev on 2026-08-24: Ymrossi drifted onto
        // quest 233 by itself, was re-asserted onto the council's 3109, handed
        // 3109 in, and its rpg then re-rolled back onto 233. Aiming it at 233
        // for its SECOND errand hit this guard - `working == questId` was true,
        // so this returned silently and nothing happened. The bot sat holding a
        // DoQuest state for 233 whose objective pointer belonged to its own
        // earlier pursuit, and 233 was already COMPLETE, so there was no
        // objective left to walk to and it never advanced to the hand-in.
        //
        // The first errand only worked BECAUSE ChangeToDoQuest was called
        // fresh and reset that pointer. A new errand needs the same reset even
        // when the id happens to match, because the state behind it belongs to
        // a different, self-chosen pursuit. Re-issue once on the change, then
        // go quiet again - which keeps the walks-forever loop this guard was
        // written to prevent.
        if (working == questId && !aimChanged)
            return true;

        if (working == questId)
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' was already on quest {} ({}) by its own choice - "
                     "re-issuing for the new errand so the objective pointer resets",
                     name, questId, quest->GetTitle());
            botAI->rpgInfo.ChangeToDoQuest(questId, quest);  // NewRpgInfo.h:106
            return true;
        }

        if (working)
            LOG_INFO("module.overseer",
                     "overseer: '{}' had drifted onto quest {} - re-asserting chosen "
                     "quest {} ({})", name, working, questId, quest->GetTitle());
        else if (aimChanged)
            LOG_INFO("module.overseer",
                     "overseer: '{}' now working quest {} ({}) - chosen by the council",
                     name, questId, quest->GetTitle());
        else
            LOG_INFO("module.overseer",
                     "overseer: '{}' re-asserting chosen quest {} ({}) - the 30-minute "
                     "RPG_DO_QUEST lease had lapsed", name, questId, quest->GetTitle());

        botAI->rpgInfo.ChangeToDoQuest(questId, quest);  // NewRpgInfo.h:106
        return true;
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

    // Write the queued events. Same shape as FlushChat and for the same two
    // reasons - EscapeString borrows the shared synchronous connection, and one
    // multi-row statement per tick beats one statement per event - with one
    // addition: the batch is already coalesced by key, so the row count here is
    // the number of DISTINCT things that happened, never the number of times
    // they happened.
    void FlushEvents()
    {
        std::map<EventKey, PendingEvent> batch;
        uint64 dropped = 0;
        {
            std::lock_guard<std::mutex> guard(g_eventMutex);
            if (g_eventQueue.empty() && !g_droppedEvents)
                return;
            batch.swap(g_eventQueue);
            dropped = g_droppedEvents;
            g_droppedEvents = 0;
        }

        // Overflow is OURS to bound, so it is counted and said out loud - the
        // same bargain FlushChat strikes with g_droppedLines. Now that
        // module.overseer has an appender of its own, this line will actually
        // be somewhere a person can read it.
        if (dropped)
            LOG_WARN("module.overseer", "overseer: dropped {} events (queue full)", dropped);

        if (batch.empty())
            return;

        std::ostringstream ss;
        ss << "INSERT INTO overseer_event (character_name, character_guid, kind, subject_id, "
              "subject_name, detail, level, map, zone, bucket, occurrences) VALUES ";
        bool first = true;
        for (std::pair<EventKey const, PendingEvent> const& entry : batch)
        {
            EventKey const& key = entry.first;
            PendingEvent const& ev = entry.second;
            if (!first)
                ss << ',';
            first = false;
            ss << "('" << Esc(key.characterName)
               << "'," << ev.characterGuid
               << ",'" << Esc(key.kind)
               << "'," << key.subjectId
               << ",'" << Esc(ev.subjectName)
               << "','" << Esc(ev.detail)
               << "'," << static_cast<uint32>(ev.level)
               << ',' << static_cast<uint32>(ev.mapId)
               << ',' << ev.zoneId
               << ',' << key.bucket
               << ',' << ev.occurrences
               << ')';
        }

        // The de-duplication contract. `occurrences` on the right of the `=` is
        // the value already in the table and VALUES(occurrences) is the count
        // this batch carries, so a row's total survives a flush boundary: sixty
        // failures split as 40 + 20 across two ticks still reads 60.
        //
        // VALUES() in an ON DUPLICATE clause is deprecated as of MySQL 8.0.20 in
        // favour of a row alias. It is used anyway and on purpose: the alias
        // form is a syntax ERROR before 8.0.19, and a statement built into a
        // C++ string is not something anybody will notice has stopped working
        // until the table stops filling. A deprecation notice is a cheaper
        // failure than that.
        ss << " ON DUPLICATE KEY UPDATE "
              "occurrences = occurrences + VALUES(occurrences), "
              "last_seen = CURRENT_TIMESTAMP, "
              "character_guid = VALUES(character_guid), "
              "subject_name = VALUES(subject_name), "
              "detail = VALUES(detail), "
              "level = VALUES(level), "
              "map = VALUES(map), "
              "zone = VALUES(zone)";
        CharacterDatabase.Execute(ss.str().c_str());
    }

    // ------------------------------------------------------- outcome --
    //
    // WHAT A FINISHED ROW IS ALLOWED TO CLAIM (infra#2819).
    //
    // WHAT WAS WRONG. `status = "delivered"` was written the instant
    // PlayerbotAI::HandleCommand accepted the row. HandleCommand accepting a
    // row is not the command acting: the line lands in
    // PlayerbotAI::chatCommands (PlayerbotAI.cpp:1107) and is drained on the
    // bot's NEXT AI tick (PlayerbotAI.cpp:553-588). So the queue's only
    // success status was written before anyone - including this module - knew
    // whether the bot had changed, and it was read as "applied" by two
    // engineers, repeatedly, in writing.
    //
    // THE CASE. `nc -new rpg` was sent to Ugga twice:
    //
    //     4049  01:42:57  Ugga  nc -new rpg  delivered
    //     4184  01:52:57  Ugga  nc -new rpg  delivered
    //     01:57:17  probe: Ugga STILL HAS `new rpg`
    //
    // Both rows read `delivered`. Her strategy did not change either time.
    // Three other characters received the identical command in the identical
    // batch and it worked for all three, both times. And the worldserver log
    // across the whole window contained only `holding` lines - the block
    // below logged the command it HELD and said nothing about the one it
    // SENT. Queue says success, log says nothing, and a deterministic,
    // twice-reproduced, single-character failure is not hard to diagnose, it
    // is unobservable.
    //
    // WHY THIS IS NOT A GUESS AT THE CAUSE. It is not one. The cause is
    // unknown and two hypotheses have already been built and falsified - a
    // delivery race (which predicted self-correction on the next cycle and
    // did not self-correct) and the goal-beneficiary path (nothing re-adds
    // the strategy, and `goals.reconcile` emits DriveQuest, never a
    // StrategyCommand). This changes what the INSTRUMENTS say, so the next
    // occurrence writes a row that is visibly different from the rows that
    // worked, and the next person starts from evidence rather than a third
    // unfalsifiable chain.
    //
    // WHAT IS CHECKED. Only a post-condition this module can actually read.
    // `nc +x` / `nc -x` / `co +x` / `co -x` name a strategy and an engine, and
    // PlayerbotAI::GetStrategies answers off the LIVE engine - the same
    // accessor ProbeStrategies uses (see the note there on why the persisted
    // playerbots_db_store copy is not good enough). Everything else - chat,
    // gm, probe, give, share, `follow`, `summon`, a dot-command - has no
    // post-condition of this shape and keeps `delivered` with its old meaning
    // exactly. NOT redefining `delivered` is what keeps this one change:
    // every row already in the table stays correct, and the worldserver image
    // and the bridge image, which deploy separately, work in either order.
    //
    // WHAT IS DELIBERATELY NOT CHANGED. The per-verb throttle below spaces
    // held commands on a fixed 2s TIMER and nothing verifies the bot has
    // ticked - a timer, not a handshake. That is a real defect and it is a
    // SEPARATE one. It is demonstrably not eating commands across the board
    // (Grug takes `nc +new rpg` and `nc +grind` in one batch every cycle and
    // holds both) and it is not what happens to Ugga. It is left exactly as
    // it is, on purpose.

    // One `+name` or `-name` out of a strategy command, and the answer the
    // engine is expected to give afterwards.
    struct StrategyItem
    {
        std::string name;    // lowercased, sign stripped
        bool combat{false};  // `co` -> the combat engine, `nc` -> non-combat
        bool want{false};    // '+' -> present afterwards, '-' -> absent
        bool before{false};  // what the engine said BEFORE the hand-off
        bool after{false};   // ...and at the verdict
    };

    // A command handed to a bot whose effect has not been observed yet. Held
    // in memory rather than in the row: the row already says `verifying`, and
    // a worldserver that dies here loses the read-back exactly as it loses an
    // in-flight `claimed` row today. The bridge sweeps both (bridge.py
    // _expire_stale_claims), so the failure mode is one that already exists
    // and already has an owner rather than a new one.
    struct StrategyCheck
    {
        uint32 id{0};
        std::string targetName;
        std::string command;
        uint32 waitedMs{0};
        std::vector<StrategyItem> items;
    };

    // World thread only - both DeliverPendingCommands and ResolveStrategyChecks
    // run from OnUpdate - so unguarded, like _lastAim. Bounded by construction:
    // at most COMMANDS_PER_POLL entries are added per poll and every entry is
    // resolved within VERIFY_GRACE_MS.
    std::vector<StrategyCheck> _pendingChecks;

    // `nc -new rpg`, `co +grind,-loot`. Returns false for everything with no
    // post-condition to read: a different verb, `~` (toggle) or `?` (query),
    // an empty or malformed list. False is not a failure - it is this module
    // declining to claim more than it can check, and the row takes the
    // unchanged-in-meaning `delivered`.
    //
    // A strategy whose REGISTERED name differs from the typed one would read
    // as `unchanged`. That is still true rather than wrong - the list really
    // does not contain what was asked for - and the verdict carries the whole
    // live list, so an operator sees which it is in one look.
    static bool ParseStrategyChange(std::string const& command, std::vector<StrategyItem>& out)
    {
        std::string::size_type const space = command.find(' ');
        if (space == std::string::npos)
            return false;

        std::string const verb = LowerName(command.substr(0, space));
        // `d` (the dead engine) is not here on purpose: nothing in this system
        // sends one, and a verb nobody uses is a verb nobody has tested.
        if (verb != "nc" && verb != "co")
            return false;
        bool const combat = (verb == "co");

        std::vector<StrategyItem> items;
        std::string const rest = command.substr(space + 1);
        std::string::size_type at = 0;
        while (at <= rest.size())
        {
            std::string::size_type const comma = rest.find(',', at);
            std::string piece = rest.substr(
                at, comma == std::string::npos ? std::string::npos : comma - at);
            at = (comma == std::string::npos) ? rest.size() + 1 : comma + 1;

            std::string::size_type const first = piece.find_first_not_of(" \t");
            if (first == std::string::npos)
                return false;
            std::string::size_type const last = piece.find_last_not_of(" \t");
            piece = piece.substr(first, last - first + 1);

            if (piece[0] != '+' && piece[0] != '-')
                return false;
            std::string const name = LowerName(piece.substr(1));
            if (name.empty())
                return false;

            StrategyItem item;
            item.name = name;
            item.combat = combat;
            item.want = (piece[0] == '+');
            items.push_back(item);
        }

        if (items.empty())
            return false;
        out = items;
        return true;
    }

    // Is `item.name` in the bot's live list for its engine?
    //
    // The engine is chosen by an if/else over the two enumerators rather than
    // by holding one in a variable, so mod-playerbots' enum TYPE is never
    // named here. This file only compiles on push and an unqualified nested
    // name has already cost a build; the call shape below is character for
    // character the one ProbeStrategies already compiles with.
    static bool StrategyPresent(PlayerbotAI* botAI, StrategyItem const& item)
    {
        auto contains = [&item](auto const& live)
        {
            for (std::string const& name : live)
                if (LowerName(name) == item.name)
                    return true;
            return false;
        };

        if (item.combat)
            return contains(botAI->GetStrategies(BOT_STATE_COMBAT));
        return contains(botAI->GetStrategies(BOT_STATE_NON_COMBAT));
    }

    // Read the engines back and end every check that can be ended.
    //
    // Resolves EARLY - as soon as the post-condition holds - so the usual
    // cost of verifying is one extra poll, not VERIFY_GRACE_MS. Only a check
    // that is still not satisfied when the window closes becomes `unchanged`,
    // which is the row this whole section exists to be able to write.
    void ResolveStrategyChecks(uint32 elapsedMs)
    {
        std::vector<StrategyCheck> stillWaiting;
        stillWaiting.reserve(_pendingChecks.size());

        for (StrategyCheck& check : _pendingChecks)
        {
            check.waitedMs += elapsedMs;

            Player* bot = ObjectAccessor::FindPlayerByName(check.targetName);
            PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            if (!botAI)
            {
                // Said out loud rather than dropped. A character that logged
                // out mid-check is a perfectly good explanation, and an
                // explanation is the thing that was missing.
                LOG_WARN("module.overseer",
                         "overseer: command {} ('{}' for '{}') cannot be verified - the "
                         "character is no longer in the world",
                         check.id, check.command, check.targetName);
                CharacterDatabase.Execute(
                    "UPDATE overseer_command SET status = 'error', detail = '{}' "
                    "WHERE id = {} AND status = 'verifying' AND claimed_by = '{}'",
                    "left the world before the change could be read back", check.id,
                    g_runToken);
                continue;
            }

            bool holds = true;
            for (StrategyItem& item : check.items)
            {
                item.after = StrategyPresent(botAI, item);
                if (item.after != item.want)
                    holds = false;
            }

            if (!holds && check.waitedMs < VERIFY_GRACE_MS)
            {
                stillWaiting.push_back(check);
                continue;
            }

            // EVERY VERDICT WRITES `result`, so no row ends without saying
            // what it was judged on. The live lists ride along whole: the
            // point of this issue is that diagnosing the Ugga case required a
            // separate probe minutes later, and a verdict that names only its
            // own conclusion would have kept that round trip.
            std::ostringstream o;
            o << "{\"outcome\":" << J(holds ? "applied" : "unchanged")
              << ",\"command\":" << J(check.command)
              << ",\"waited_ms\":" << check.waitedMs
              << ",\"items\":[";
            bool first = true;
            for (StrategyItem const& item : check.items)
            {
                if (!first)
                    o << ",";
                first = false;
                o << "{\"strategy\":" << J(item.name)
                  << ",\"engine\":" << J(item.combat ? "combat" : "non_combat")
                  << ",\"sign\":" << J(item.want ? "+" : "-")
                  << ",\"before\":" << (item.before ? "true" : "false")
                  << ",\"after\":" << (item.after ? "true" : "false")
                  << ",\"holds\":" << (item.after == item.want ? "true" : "false") << "}";
            }
            o << "],\"live\":" << ProbeStrategies(bot) << "}";

            char const* status = holds ? "applied" : "unchanged";
            char const* detail =
                holds ? "" : "the bot accepted it and its live strategy list did not change";

            if (holds)
                LOG_INFO("module.overseer",
                         "overseer: command {} ('{}' for '{}') applied - read back off the "
                         "live engine after {}ms",
                         check.id, check.command, check.targetName, check.waitedMs);
            else
                // WARN, not INFO. This is the line whose absence made the
                // original failure invisible, and INFO is where it would be
                // lost again.
                LOG_WARN("module.overseer",
                         "overseer: command {} ('{}' for '{}') CHANGED NOTHING - accepted by "
                         "the bot, and its live strategy list is unchanged after {}ms; "
                         "see overseer_command.result for the lists",
                         check.id, check.command, check.targetName, check.waitedMs);

            CharacterDatabase.Execute(
                "UPDATE overseer_command SET status = '{}', detail = '{}', result = '{}' "
                "WHERE id = {} AND status = 'verifying' AND claimed_by = '{}'",
                status, detail, EscLong(o.str()), check.id, g_runToken);
        }

        _pendingChecks.swap(stillWaiting);
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
    void DeliverPendingCommands(uint32 sincePollMs)
    {
        // Before anything new goes out: end the checks from earlier polls that
        // can now be ended. First, so a `verifying` row never outlives its
        // answer by a whole poll.
        ResolveStrategyChecks(sincePollMs);

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

            // Bot orders only. 'chat', 'gm', 'probe', 'give' and 'share' do
            // not go through PlayerbotAI::HandleCommand and share no trigger,
            // so nothing they do can be overwritten by the row after them.
            if (kind != "chat" && kind != "gm" && kind != "probe" && kind != "give"
                && kind != "share")
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
            // with every appearance of a fresh one. kind='give' and
            // kind='share' fill the same column, for the same reason: the row
            // must carry ITS OWN outcome.
            std::string rowResult;

            Player* player = ObjectAccessor::FindPlayerByName(targetName);
            if (!player)
                detail = "target not online";
            else if (kind == "chat")
                detail = DoChat(player, channel, command, targetArg, status);
            else if (kind == "gm")
                detail = DoGmCommand(player, command, status);
            else if (kind == "probe")
                detail = DoProbe(player, command, status, rowResult);
            else if (kind == "give")
                detail = DoGive(player, targetArg, command, status, rowResult);
            else if (kind == "share")
                detail = DoShare(player, targetArg, command, status, rowResult);
            else if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
            {
                // READ THE ENGINE FIRST. `before` is only meaningful taken on
                // this side of the hand-off, and it is what separates "the
                // command did nothing" from "there was nothing to do".
                StrategyCheck check;
                bool const checkable = ParseStrategyChange(command, check.items);
                if (checkable)
                {
                    for (StrategyItem& item : check.items)
                        item.before = StrategyPresent(botAI, item);
                }

                botAI->HandleCommand(CHAT_MSG_WHISPER, command, player);

                // THE HAND-OFF IS LOGGED. Until this line the module logged
                // only the command it HELD, so the log for the Ugga window
                // contained `holding` lines and no trace whatsoever of the
                // two commands that actually went out (infra#2819).
                LOG_INFO("module.overseer",
                         "overseer: handed command {} ('{}') to '{}'; {}",
                         id, command, targetName,
                         checkable ? "reading the live strategy list back before it counts"
                                   : "no post-condition to read back");

                if (checkable)
                {
                    check.id = id;
                    check.targetName = targetName;
                    check.command = command;
                    _pendingChecks.push_back(check);
                    // NOT a success status. A whispered command has not acted
                    // yet - it is drained on the bot's next AI tick - so the
                    // row stays in flight until ResolveStrategyChecks has
                    // looked. This is the single line the issue is about.
                    status = "verifying";
                }
                else
                    status = "delivered";
            }
            else
                detail = "target has no bot AI (selfbot not enabled?)";

            // Conditional on the row still being OURS. If the bridge gave up
            // waiting and already ended this row, writing over it would
            // resurrect a command the sender has been told nothing came of,
            // and the two sides would disagree about what happened.
            // `result` is written as its own statement shape rather than always
            // being included, so a row that produces no payload does not have
            // NULL written over whatever a reader might already have taken
            // from it.
            if (rowResult.empty())
                CharacterDatabase.Execute(
                    "UPDATE overseer_command SET status = '{}', detail = '{}' "
                    "WHERE id = {} AND status = 'claimed' AND claimed_by = '{}'",
                    status, detail, id, g_runToken);
            else
                CharacterDatabase.Execute(
                    "UPDATE overseer_command SET status = '{}', detail = '{}', result = '{}' "
                    "WHERE id = {} AND status = 'claimed' AND claimed_by = '{}'",
                    status, detail, EscLong(rowResult), id, g_runToken);
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

    // ---------------------------------------------------------------- give --
    //
    // Move ONE item from one living character's bags into another's.
    //
    // WHY THIS IS A MODULE JOB. A party rolls on everything, so loot lands on
    // whoever won the roll rather than on whoever can use it - the priest ends
    // up carrying a two-handed axe she can never equip while the warrior wants
    // it. That is the normal state of a party, not an accident.
    //
    // mod-playerbots cannot do it. Its only item-transfer chat command is
    // `t <Hitem:id:>` and its target is hardcoded to the bot's master
    // (TradeAction.cpp:26-38). Even pointed at the right master it cannot
    // COMPLETE: TradeStatusAction::CheckTrade (TradeStatusAction.cpp:165-200)
    // takes a bot<->bot branch whenever the master is not a real player, and
    // PlayerbotAI::IsRealPlayer (PlayerbotAI.cpp:4389-4395) is
    // `player && !GET_PLAYERBOT_AI(player)` - so a SELFBOT is not a real
    // player. That branch refuses to accept unless the trader's own side
    // already holds an item, so a one-way gift leaves the trade window open
    // forever, while TradeAction::Execute has already returned true
    // (TradeAction.cpp:77) and the queue row reads `delivered`. Reports
    // success, does nothing - the recurring bug this module exists to stop.
    //
    // WHY THE TRANSACTION. The move is three writes that must all land or none
    // of them: the giver's character_inventory row goes away, item_instance
    // changes owner, and the receiver gets a character_inventory row. Half of
    // that applied is a duplicated or a vanished item. SendMailAction.cpp:
    // 166-175 is the shape being copied (BeginTransaction, then
    // DeleteFromInventoryDB + SaveToDB, then CommitTransaction);
    // GiveItemAction.cpp:79-96 does the same move with NO transaction at all,
    // which is why it is not the model.
    //
    // ORDERING, and why it is not arbitrary:
    //   Player::RemoveItem is explicit that it "does not actually change the
    //   item" (PlayerStorage.cpp:3006-3007), so after MoveItemFromInventory
    //   the item is still ITEM_UNCHANGED and Item::SaveToDB would hit its
    //   `case ITEM_UNCHANGED: break` and write NOTHING (Item.cpp:409-410).
    //   Hence the explicit SetState(ITEM_CHANGED) before the save.
    //   SaveToDB then ends with SetState(ITEM_UNCHANGED) (Item.cpp:413), which
    //   is why it must run BEFORE the store rather than after:
    //   MoveItemToInventory is what puts the item into the RECEIVER's update
    //   queue, and clearing that afterwards would leave an item owned by the
    //   receiver that is in nobody's bags.
    //
    // The receiver's character_inventory row is written here, in the same
    // transaction, instead of being left to the receiver's next periodic save.
    // PlayerSaveInterval is 900000 - fifteen minutes in which a worldserver
    // crash would leave the item owned by the receiver and in no container at
    // all, which is the losing half of exactly the corruption the transaction
    // is for.

    struct GiveSpec
    {
        bool valid = false;
        bool byGuid = false;
        uint32 key = 0;
    };

    // `guid:494263` or `entry:4562`.
    //
    // GUID IS THE PREFERRED FORM and the one to use for a specific item. An
    // entry names an item TYPE: a character holding two of them, or a stack,
    // or one worn and one spare, gives entry two right answers and the command
    // would have to pick. item_instance.guid names exactly one row and is what
    // an operator already reads out of the database when deciding what to
    // move. `entry` is kept for the convenient case of "the only one they
    // have", and the result JSON always reports the guid that actually moved,
    // so an entry-addressed move is still auditable after the fact.
    static GiveSpec ParseGiveSpec(std::string const& command)
    {
        GiveSpec spec;
        std::string::size_type const colon = command.find(':');
        if (colon == std::string::npos)
            return spec;

        std::string const what = command.substr(0, colon);
        std::string const value = command.substr(colon + 1);
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            return spec;

        if (what == "guid")
            spec.byGuid = true;
        else if (what != "entry")
            return spec;

        spec.key = static_cast<uint32>(std::strtoul(value.c_str(), nullptr, 10));
        spec.valid = (spec.key != 0);
        return spec;
    }

    // Worn gear, the backpack and equipped bags - the places a character can
    // actually hand something out of. Deliberately NOT the bank, which
    // Player::GetItemByGuid does search (PlayerStorage.cpp:423-440): a
    // character standing in a field cannot reach their bank, and moving an
    // item out of it from here would be a state change no player could make.
    static Item* FindCarriedItem(Player* who, bool byGuid, uint32 key)
    {
        auto matches = [&](Item* item)
        {
            return byGuid ? (item->GetGUID().GetCounter() == key) : (item->GetEntry() == key);
        };

        // 0..38: equipment, the four bag slots, then the backpack.
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = who->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (matches(item))
                    return item;

        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        {
            Bag* bag = who->GetBagByPos(bagSlot);
            if (!bag)
                continue;
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                if (Item* item = bag->GetItemByPos(i))
                    if (matches(item))
                        return item;
        }
        return nullptr;
    }

    // EVERY exit from here writes `out`, including every refusal. A row that
    // ends 'error' with an empty result is a row nobody can diagnose from
    // outside the worldserver, and this queue has already shipped one action
    // that reported success while doing nothing.
    static char const* DoGive(Player* giver, std::string const& receiverName,
                              std::string const& command, char const*& status,
                              std::string& out)
    {
        auto refuse = [&](char const* reason, char const* detail) -> char const*
        {
            std::ostringstream o;
            o << "{\"outcome\":\"refused\",\"reason\":" << J(reason)
              << ",\"from\":" << J(giver->GetName())
              << ",\"to\":" << J(receiverName)
              << ",\"request\":" << J(command) << "}";
            out = o.str();
            return detail;
        };

        GiveSpec const spec = ParseGiveSpec(command);
        if (!spec.valid)
            return refuse("malformed item spec",
                          "malformed give: want guid:<item_instance.guid> or entry:<id>");

        if (receiverName.empty())
            return refuse("no receiver", "no receiver (put the receiving character in target_arg)");

        Player* receiver = ObjectAccessor::FindPlayerByName(receiverName);
        if (!receiver)
            return refuse("receiver offline", "receiver not online");

        // Not merely pointless: MoveItemFromInventory followed by a store back
        // onto the same character is a real chance to lose the item for no
        // gain at all.
        if (receiver == giver)
            return refuse("same character", "giver and receiver are the same character");

        Item* item = FindCarriedItem(giver, spec.byGuid, spec.key);
        if (!item)
            return refuse("item not found",
                          spec.byGuid ? "no carried item with that guid on the giver"
                                      : "no carried item with that entry on the giver");

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return refuse("no item template", "item has no template");

        // Captured BEFORE anything moves. Player::RemoveItem calls
        // SetSlot(NULL_SLOT) (PlayerStorage.cpp:3072), so the source position
        // is unreadable afterwards, and the Item* itself is not guaranteed to
        // survive a store that merges into an existing stack.
        ObjectGuid const itemGuid = item->GetGUID();
        uint32 const itemEntry = item->GetEntry();
        uint32 const itemCount = item->GetCount();
        uint8 const srcBag = item->GetBagSlot();
        uint8 const srcSlot = item->GetSlot();
        std::string const itemName = proto->Name1;

        auto describe = [&](char const* outcome, char const* reason, int32 storeResult,
                            int32 destSlot)
        {
            std::ostringstream o;
            o << "{\"outcome\":" << J(outcome)
              << ",\"reason\":" << J(reason)
              << ",\"from\":" << J(giver->GetName())
              << ",\"to\":" << J(receiver->GetName())
              << ",\"item_guid\":" << itemGuid.GetCounter()
              << ",\"entry\":" << itemEntry
              << ",\"name\":" << J(itemName)
              << ",\"count\":" << itemCount
              << ",\"src_bag\":" << uint32(srcBag)
              << ",\"src_slot\":" << uint32(srcSlot)
              << ",\"dest_slot\":" << destSlot
              << ",\"store_result\":" << storeResult << "}";
            out = o.str();
        };

        // Soulbound is checked separately from CanBeTraded purely so the
        // refusal can NAME it: it is the one refusal that is permanent, and
        // telling an operator "cannot be handed over" when the real answer is
        // "and never will be" invites them to retry forever.
        if (item->IsSoulBound())
        {
            describe("refused", "item is soulbound to the giver", EQUIP_ERR_OK, -1);
            return "item is soulbound and can never be handed over";
        }
        // Everything else Item::CanBeTraded (Item.cpp:795-820) knows about: a
        // non-empty bag, a bag that is itself equipped, an item currently
        // being looted, a temporary or bind-on-enchant.
        if (!item->CanBeTraded())
        {
            describe("refused", "item cannot be traded (non-empty bag, worn bag, being looted, "
                                "or bound by enchant)", EQUIP_ERR_OK, -1);
            return "item cannot be handed over";
        }

        // Asked BEFORE anything is removed from the giver, so a receiver with
        // no room costs nothing: the giver still has the item and the row says
        // why.
        ItemPosCountVec dest;
        InventoryResult const msg = receiver->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            describe("refused", "receiver cannot store the item", int32(msg), -1);
            // No apostrophe, and that is not a style choice: `detail` is
            // embedded straight into the UPDATE below, so every literal on
            // this path must be free of quote characters.
            return msg == EQUIP_ERR_INVENTORY_FULL ? "receiver bags are full"
                                                   : "receiver cannot store the item";
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 1. Off the giver, in memory and in character_inventory.
        giver->MoveItemFromInventory(srcBag, srcSlot, true);
        item->DeleteFromInventoryDB(trans);

        // 2. Re-owned in item_instance. SetState is required because
        //    RemoveItem left the item ITEM_UNCHANGED and SaveToDB ignores
        //    that state; the default nullptr `forplayer` means no update
        //    queue is touched by it.
        item->SetOwnerGUID(receiver->GetGUID());
        item->SetState(ITEM_CHANGED);
        item->SaveToDB(trans);

        // 3. Into the receiver's bags. in_characterInventoryDB = true because
        //    the character_inventory row is written just below rather than
        //    being deferred to the receiver's next save.
        receiver->MoveItemToInventory(dest, item, true, true);

        // 4. The placement, durably. Re-found by guid rather than reusing
        //    `item`: a store that merges into an existing stack consumes the
        //    source item (PlayerStorage.cpp:2786-2800), and in that case there
        //    is no new row to write - the surviving stack already has one.
        int32 destSlot = -1;
        if (Item* stored = receiver->GetItemByGuid(itemGuid))
        {
            destSlot = int32(stored->GetSlot());
            Bag* container = stored->GetContainer();
            trans->Append(
                "REPLACE INTO character_inventory (guid, bag, slot, item) VALUES ({}, {}, {}, {})",
                receiver->GetGUID().GetRawValue(),
                container ? container->GetGUID().GetCounter() : 0,
                uint32(destSlot),
                itemGuid.GetCounter());
        }

        CharacterDatabase.CommitTransaction(trans);

        LOG_INFO("module.overseer", "overseer: gave item {} (entry {}) from '{}' to '{}'",
                 itemGuid.GetCounter(), itemEntry, giver->GetName(), receiver->GetName());

        describe("moved", "", EQUIP_ERR_OK, destSlot);
        status = "delivered";
        return "";
    }

    // --------------------------------------------------------------- share --
    //
    // Put a quest one family member is carrying into another member's log.
    //
    // WHY THIS IS A MODULE JOB. Five characters quest together holding five
    // different quest logs, so the same fight pays two of them nothing - Og
    // 17 turn-ins against Grog and Ugga on 3, standing three yards apart
    // killing the same mobs. The only way five people get paid for one kill
    // is for all five to hold the quest.
    //
    // mod-playerbots cannot do it for these bots. Both of its paths need a
    // master, and these are masterless by design:
    //   * `share quest <link>` returns false at ShareQuestAction.cpp:16 -
    //     `if (!GetMaster()) return false;`
    //   * `auto share quest` is only wired into the `maintenance` strategy,
    //     which AiFactory never adds (commented out at AiFactory.cpp:628 and
    //     657), and even when forced on it does nothing: in an ALL-BOT party
    //     `partyNeedsQuest` is never set (ShareQuestAction.cpp:60-97 only sets
    //     it for a member with no PlayerbotAI), so HandlePushQuestToParty is
    //     never called and no divider is ever written.
    //
    // WHY NO DIVIDER AND NO PACKET, WHICH IS THE LOAD-BEARING PART.
    // AcceptQuestShareAction::Execute takes `master = GetMaster()`
    // (AcceptQuestAction.cpp:104) and dereferences it unconditionally at
    // AcceptQuestAction.cpp:139:
    //
    //     if (!bot->GetDivider().IsEmpty())
    //         master->SendPushToPartyResponse(bot, QUEST_PARTY_MSG_ACCEPT_QUEST);
    //
    // That is a worldserver segfault for a masterless bot. It is unreachable
    // today only because nothing ever sets a roster bot's divider AND feeds it
    // the packet. Any implementation calling HandlePushQuestToParty and then
    // HandleMasterIncomingPacket creates exactly that pair. So this calls the
    // core API directly - Player::AddQuestAndCheckCompletion - and never
    // touches SetDivider or CMSG_PUSHQUESTTOPARTY at all. The crash stays
    // unreachable, and the outcome is synchronous and can be reported.
    //
    // AddQuestAndCheckCompletion rather than AddQuest: it fires
    // OnPlayerQuestAccept and auto-completes an instantly-satisfiable quest,
    // and it is explicitly null-questGiver-safe (core PlayerQuest.cpp:568-569,
    // `if (!questGiver) return;`), so passing the holder is safe.
    //
    // THE ELIGIBILITY CHECKS ARE NOT POLITENESS. They are the same set the
    // core applies in WorldSession::HandlePushQuestToParty (core
    // QuestHandler.cpp:529-603), minus the ones that exist only to send a chat
    // response. Skipping them is how a level 10 priest ends up holding a level
    // 20 quest she can never finish, which is worse than the lopsidedness this
    // is fixing. Every one of them is verified present in the pinned core:
    //
    //   Player::CanShareQuest              Player.h:1561
    //   Player::SatisfyQuestStatus         Player.h:1477
    //   Player::SatisfyQuestLog            Player.h:1472
    //   Player::CanTakeQuest               Player.h:1456
    //   Player::CanAddQuest                Player.h:1457
    //   Player::AddQuestAndCheckCompletion Player.h:1462
    //   Player::GetQuestStatus             Player.h:1492
    //   Player::GetQuestRewardStatus       Player.h:1491
    //   Player::GetGroup                   Player.h:2520
    //   WorldObject::IsInMap               Object.h:542
    //   Group::IsMember                    Group.h:238
    //
    // EVERY EXIT WRITES `out`, refusals included, and `status` becomes
    // 'delivered' ONLY after the quest has been read back out of the taker's
    // log. This queue has already shipped an action that reported success
    // while doing nothing; a share that reported 'delivered' because the call
    // returned would be the same bug wearing a different hat.

    // `quest:1234`. Deliberately the same shape as the give spec so an
    // operator reading the queue does not have to learn two grammars.
    static uint32 ParseShareSpec(std::string const& command)
    {
        std::string::size_type const colon = command.find(':');
        if (colon == std::string::npos)
            return 0;
        if (command.substr(0, colon) != "quest")
            return 0;
        std::string const value = command.substr(colon + 1);
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            return 0;
        return static_cast<uint32>(std::strtoul(value.c_str(), nullptr, 10));
    }

    static char const* DoShare(Player* holder, std::string const& takerName,
                               std::string const& command, char const*& status,
                               std::string& out)
    {
        uint32 const questId = ParseShareSpec(command);

        // Filled by every path, so no row ever ends without saying what it
        // decided. `reason` is the machine-readable half - the bridge counts
        // these - and the returned `detail` is the human half.
        auto describe = [&](char const* outcome, char const* reason, int32 takerStatus)
        {
            std::ostringstream o;
            o << "{\"outcome\":" << J(outcome)
              << ",\"reason\":" << J(reason)
              << ",\"from\":" << J(holder->GetName())
              << ",\"to\":" << J(takerName)
              << ",\"quest_id\":" << questId
              << ",\"taker_status\":" << takerStatus
              << ",\"request\":" << J(command) << "}";
            out = o.str();
        };

        if (!questId)
        {
            describe("refused", "malformed request", -1);
            return "malformed share: want quest:<id>";
        }
        if (takerName.empty())
        {
            describe("refused", "no taker", -1);
            return "no taker (put the receiving character in target_arg)";
        }

        Player* taker = ObjectAccessor::FindPlayerByName(takerName);
        if (!taker)
        {
            describe("refused", "taker offline", -1);
            return "taker not online";
        }
        if (taker == holder)
        {
            describe("refused", "same character", -1);
            return "holder and taker are the same character";
        }

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            describe("refused", "no such quest", -1);
            return "no quest template with that id";
        }

        // CanShareQuest is BOTH halves of "may this move": the quest must
        // carry QUEST_FLAGS_SHARABLE, and the holder must actually have it in
        // m_QuestStatus (core PlayerQuest.cpp:1517-1536). A row naming a
        // holder who turned the quest in last week fails here rather than
        // silently sharing something nobody is carrying.
        if (!holder->CanShareQuest(questId))
        {
            describe("refused", "holder cannot share it (not held, or not flagged sharable)",
                     int32(taker->GetQuestStatus(questId)));
            return "holder is not carrying that quest or it is not sharable";
        }

        // Same party and same map, which is what the core itself requires
        // (`!player->IsInMap(_player)` skips a member outright). Not a
        // politeness check: sharing into a character on another continent is
        // how a quest log fills with work nobody can reach, which is exactly
        // Bork's Coldridge Valley problem in reverse.
        Group* group = holder->GetGroup();
        if (!group || !group->IsMember(taker->GetGUID()))
        {
            describe("refused", "not in the same party", int32(taker->GetQuestStatus(questId)));
            return "holder and taker are not in the same party";
        }
        if (!taker->IsInMap(holder))
        {
            describe("refused", "not on the same map", int32(taker->GetQuestStatus(questId)));
            return "holder and taker are not on the same map";
        }

        // Already-rewarded is checked before already-held so the two never
        // report as one another: "she finished this last week" and "it is in
        // her log right now" call for opposite next moves.
        if (taker->GetQuestRewardStatus(questId))
        {
            describe("refused", "taker already turned it in", int32(QUEST_STATUS_REWARDED));
            return "taker has already been rewarded for that quest";
        }
        QuestStatus const before = taker->GetQuestStatus(questId);
        if (before != QUEST_STATUS_NONE)
        {
            describe("refused", "taker already holds it", int32(before));
            return "taker already has that quest";
        }
        if (!taker->SatisfyQuestStatus(quest, false))
        {
            describe("refused", "taker cannot take it in its current state", int32(before));
            return "taker cannot take that quest in its current state";
        }
        if (!taker->SatisfyQuestLog(false))
        {
            describe("refused", "taker quest log is full", int32(before));
            return "taker quest log is full";
        }
        // Level, race, class, prerequisite chain and exclusive group all live
        // behind this one call, which is why the decision layer mirrors those
        // rules rather than the module re-deriving them.
        if (!taker->CanTakeQuest(quest, false))
        {
            describe("refused", "taker is not eligible (level, race, class, prerequisite "
                                "or exclusive group)", int32(before));
            return "taker is not eligible for that quest";
        }
        if (!taker->CanAddQuest(quest, false))
        {
            describe("refused", "no bag space for the quest starting item", int32(before));
            return "taker has no bag space for the quest starting item";
        }

        taker->AddQuestAndCheckCompletion(quest, holder);

        // READ IT BACK. The whole reason this is a module job rather than a
        // chat command is that a chat command reports what it SENT. This
        // reports what the taker's quest log actually says afterwards, and
        // anything other than a real status is an error however cleanly the
        // call returned.
        QuestStatus const after = taker->GetQuestStatus(questId);
        if (after == QUEST_STATUS_NONE)
        {
            describe("error", "the quest did not land in the taker log", int32(after));
            return "the quest did not land in the taker log";
        }

        LOG_INFO("module.overseer", "overseer: shared quest {} ({}) from '{}' to '{}'",
                 questId, quest->GetTitle(), holder->GetName(), taker->GetName());

        describe("shared", "", int32(after));
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
    // What DriveQuests knew about each traveller's aim last time round, so a
    // standing complaint is logged once instead of three times a minute, an
    // aim that never lands can be given up on, and a turn-in of the WRONG
    // quest is distinguishable from a turn-in of the right one. The roster row
    // remains the decision; none of this is read as one. World thread only -
    // DriveQuests runs from OnUpdate - so unguarded. Lost on restart, which
    // only restarts the backstop clock.
    struct AimState
    {
        uint32 questId{0};      // the aim as this loop last saw it
        time_t since{0};        // when that aim was first seen
        uint32 lastWorking{0};  // the quest RPG_DO_QUEST last named
        // THE RE-PICK MEMORY (infra#2801). One concept, kept together and
        // NOT reset when the aim is: releasing an aim says nothing about
        // which quests are reachable, and an earlier revision cleared this
        // wholesale with `state = AimState()`, which handed a just-abandoned
        // quest straight back on the next poll.
        //
        // Upstream keeps its own give-up set in `lowPriorityQuest` and the
        // walk below honours it, but upstream only writes there after
        // reaching the reward POI and sitting for five minutes
        // (NewRpgAction.cpp:622). A leader who cannot reach the POI at all
        // never starts that clock, so for HIS failure upstream's memory stays
        // empty forever and this is the only record there is.
        struct RepickMemory
        {
            uint32 lastPicked{0};  // the quest this loop chose last
            uint8 strikes{0};      // consecutive chooses of it that went nowhere
            float fromX{0.f};      // where it stood when the STREAK began, so
            float fromY{0.f};      // "went nowhere" is measured over the streak
            bool stuckLogged{false};           // whole-log complaint made once
            std::map<uint32, time_t> givenUp;  // quest -> when we gave up
        };
        RepickMemory repick;
    };
    std::map<std::string, AimState> _lastAim;
    uint32 _eventTimer = 0;
    bool _watchLoaded = false;
};

void Addmod_overseerScripts()
{
    new OverseerWorldScript();
    new OverseerChatScript();
    new OverseerEventScript();
}
