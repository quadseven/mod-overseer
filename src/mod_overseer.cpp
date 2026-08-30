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
 *  6. Death record (infra#2912). overseer_event's 'death' kind is a per-hour
 *     COUNT with no room for WHY - it answers "is this still happening", not
 *     "what happened this time". overseer_death is the second half: one
 *     un-coalesced row per death, carrying the killer, the health trend
 *     leading up to it, and what strategy/aim/job was steering the character,
 *     assembled entirely from state already sitting in memory from the other
 *     drives - see the death-context comment ahead of OverseerEventScript.
 *     Deployment note: this is built for whatever realm's worldserver runs
 *     it (namespace-agnostic - a database-level concern) and is NOT wired
 *     into the live `wow` family's deployment by this change; see this
 *     table's own migration for the full argument.
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
#include "Corpse.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "GameGraveyard.h"
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
#include "Creature.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldSession.h"
#include "TradeData.h"
#include "Opcodes.h"
#include "WorldPacket.h"

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

// How often a character sent to an NPC is pointed at it again (infra#2783).
// RPG_WANDER_NPC self-expires to IDLE after statusWanderNpcDuration, which is
// FIVE minutes (NewRpgAction.h:65, checked at NewRpgAction.cpp:278) - six times
// shorter than the thirty-minute quest lease. A walk across two zones takes
// longer than the lease it is walking under, so the aim is not a one-shot: it
// is renewed here, and a poll slower than the lease would mean the traveller
// spent part of every five minutes wandering off on its own.
constexpr uint32 TRAVEL_POLL_MS = 15000;

// How often the engagement-safety drive checks the roster for a character
// that is both unaccompanied and unaimed (infra#2925/#2891 - watched live:
// Grug revived alone after a group wipe in Burning Steppes, with no group, no
// quest, and no travel aim pointing him anywhere, and walked straight into a
// `??`-conned dragonkin). Faster than the quest/travel polls on purpose: the
// failure this drive exists to stop is a WALK, not a multi-minute errand, and
// a character that revives alone can be in front of something lethal within
// seconds. Still a poll, not a hook on the AI tick - see DriveEngagementSafety
// for why that distinction matters on a worldserver that has already
// segfaulted twice today.
constexpr uint32 ENGAGEMENT_POLL_MS = 8000;

// HOW LONG DEAD BEFORE THIS DRIVE STOPS WAITING FOR THE NORMAL PATH.
// Corpse-run for a corpse a few yards away is seconds; mod-playerbots' own
// "died too many times, get yourself unstuck" escape hatch
// (ReviveFromCorpseAction/FindCorpseAction, dCount >= 5, calling
// RandomPlayerbotMgr::Revive -> RandomTeleportGrindForLevel -> RandomTeleport)
// is the thing this drive exists to back up - see DriveStuckRevival below for
// why that escape hatch never reaches a grouped non-leader. 45s is long
// enough that a working corpse-run has already succeeded, short enough that a
// genuinely stuck character is not left dead for minutes.
constexpr int64 STUCK_REVIVAL_DEAD_SECONDS = 45;

// WHEN THE NEAREST GRAVEYARD IS ITSELF THE THING KILLING THEM.
// Resurrecting at the closest graveyard is right almost everywhere, and it is
// what this drive did first. Measured on wow-dev 2026-08-28 it doubled the
// death rate: 4.94 deaths/character/10min before, 10.03 after, over a 34-minute
// window (173 deaths). The family were dying at Burning Steppes graveyard 1469
// (-7923.56, -1353.23), which has four level 57-58 Flamescale Wyrmkin spawned
// 31 yards away - inside the aggro radius of a level 15-19 character. Every
// resurrection handed them straight back to the same mob, so a FASTER revive
// meant a FASTER death. The run carried its own control group: Grug, the only
// one this drive skipped at the time, died 22 times while the four it revived
// died 35-39 each.
//
// So a graveyard that a character keeps dying at is not a rescue, and the
// module already has the evidence to know which is which - it writes every
// death to overseer_death with a position and a timestamp. Repeated deaths in
// one small area mean the local graveyard is not working; go home instead.
// Deliberately measured in the module's OWN telemetry rather than by zone or
// creature id: nothing here is specific to Burning Steppes or to one mob, and
// a trap built somewhere else reads identically.
constexpr uint64 STUCK_REVIVAL_TRAP_DEATHS = 3;
constexpr uint32 STUCK_REVIVAL_TRAP_MINUTES = 15;
constexpr float STUCK_REVIVAL_TRAP_RADIUS = 100.0f;

// The level difference at which AzerothCore's own client-facing con-color
// math would show a target as `??` - unknown, maximum danger - rather than a
// number. This is the SAME signal the game already computes for the
// nameplate/tooltip frame; it is not a threshold invented for this drive.
// Ten is the standard WoW figure for that boundary (elite or not), and using
// it here means "would this read as `??` to a human looking at it" rather
// than a number picked to fit one incident.
constexpr uint32 CON_COLOR_UNKNOWN_LEVEL_DIFF = 10;

// How close counts as arrived. INTERACTION_DISTANCE is 5.0 yards and is what
// the game uses to decide whether a player may talk to an NPC at all; this is
// deliberately looser, because arrival is measured against the SPAWN POINT out
// of the creature table while the creature itself patrols away from it. Too
// tight and the errand never reads as finished; too loose and it reads as
// finished from across the room.
constexpr float TRAVEL_ARRIVED_YARDS = 12.0f;

// The same question for an aimed POSITION, and it needs a different answer.
// Everything above reasons about a creature: the slack exists because the
// spawn point is not where the creature is standing. A position does not
// patrol. Nothing about it justifies twelve yards, and one thing forbids it -
// the object most often standing on an aimed position is an instance
// areatrigger, and those are SMALLER than the tolerance meant to reach them:
//
//     areatrigger 78   (-11208.5, 1685.34, 25.7612)   radius 7   DeadMines
//
// Arriving within 12 yards of a 7 yard trigger is arriving OUTSIDE it. The
// errand then completes as a SUCCESS, the quest drive resumes, and the
// character walks away from a door it was touching. Measured live on the dev
// world, and it reads as a clean run in the log, which is why it survived so
// long:
//
//     15:41:43  'Ugga' sent to 'at:0:-11208.5,1685.34,25.76' - creature 0 at 2391 yards
//     15:47:28  'Ugga' reached 'at:0:-11208.5,1685.34,25.76' - errand done, releasing
//     15:48:13  'Ugga' is off its travel errand - the quest drive picks up again
//
// The walk was never the problem. 2391 yards were covered without incident,
// and the aim was the trigger's own coordinate to the decimal. Six minutes
// after "arriving" she was over a thousand yards away and gathering again.
//
// Five yards, because it must be strictly inside the smallest trigger this is
// aimed at (7) while staying reachable by a path that ends on a doorstep.
// INTERACTION_DISTANCE is the same 5.0 and is the game's own answer to "close
// enough to act on a thing", which is exactly the question here.
constexpr float TRAVEL_ARRIVED_POSITION_YARDS = 5.0f;

// How long a character may be sent somewhere before the errand is given up on.
// The primary release is ARRIVING; this is the backstop for a target that
// cannot be reached at all - the far side of an ocean, inside an instance, a
// spawn that no longer exists. Longer than two RPG leases so an aim is never
// released merely because the lease lapsed, which is what renewal is for.
constexpr time_t TRAVEL_BACKSTOP_SECONDS = 20 * 60;

// How much nearer a character must get before the backstop above accepts that
// the errand is working and starts its clock again. Compared against the
// CLOSEST the character has ever been on this errand, not against the previous
// poll, which is what makes a small number safe here: the best distance only
// ratchets downward, so a bot circling or jammed against scenery cannot keep
// beating it, while a bot actually walking beats it every poll. Wide enough to
// clear the jitter of a bot that has stopped, narrow enough to be nothing next
// to a real journey.
constexpr float TRAVEL_PROGRESS_YARDS = 10.0f;

// How long travel keeps the wheel after an errand ends (PR #2840 review).
// OnUpdate runs DriveQuests BEFORE DriveTravel, so without a grace the poll
// that releases an arrived errand can be followed immediately by a
// ChangeToDoQuest that wipes the WanderNpc state the bot is still standing in -
// and upstream wants npcStayTime = 8 seconds at the NPC (NewRpgAction.h:99)
// before it counts as having been there. Longer than one QUEST_POLL_MS plus
// that dwell, so the last poll of the errand and the first poll of the quest
// drive can never be the same poll. See TravelHoldsTheWheel.
constexpr time_t TRAVEL_HANDBACK_SECONDS = 45;

// How often the unlearn drive looks for a profession the roster has asked a
// character to give up (infra#2757).
//
// WHY IT IS NOT THE TRAVEL POLL, WHICH IT SITS NEXT TO. Unlearning needs no
// journey - it is a spellbook action - and it is the PREREQUISITE for the
// journey being worth making: a character standing at the tailoring trainer
// with both primary slots full learns nothing. Tying it to travel would
// deadlock the pair, since the slot would not free until the character
// arrived and arriving would achieve nothing until the slot freed.
//
// WHY THIRTY SECONDS AND NOT FASTER. This is a rare, deliberate, destructive
// act - a handful of times in the family's whole life. The poll exists to
// pick up a request a human or the bridge has just written, and half a minute
// is well inside how long the walk to the trainer takes anyway.
constexpr uint32 PROFESSION_POLL_MS = 30000;

// How long a dungeon run's heartbeat may go cold before it is considered over.
// The arming drive touches it on every pass while anyone from the roster is
// inside, so a cold heartbeat means nobody has been seen in there.
//
// GENEROUS ON PURPOSE. Closing a run early re-opens the very stranding the run
// record exists to prevent - every drive resumes steering a character who is
// still inside an instance. Closing late costs only a finished row lingering,
// which nothing suffers from. Two minutes is many arming passes.
constexpr uint32 RUN_COLD_SECONDS = 120;

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

// How often the death queue is written (infra#2912). Faster than the event
// flush on purpose: a death is not a repeat-and-coalesce fact like the axe
// error events are, it is the whole reason this table exists, and losing one
// to a worldserver restart between polls is losing the one sample that
// mattered. Not sub-second either - RecordDeath already runs synchronously in
// the death hook with no query of its own, so nothing is gained by writing
// faster than a person could plausibly be watching a Discord channel.
constexpr uint32 DEATH_FLUSH_MS = 3000;

// A permadeath realm's whole population is disposable and the family is five
// characters - a fortnight of one-row-per-death from either can never
// approach the volume EVENT_RETENTION_DAYS was chosen to bound. Kept anyway,
// separately, so the two tables' retention can be tuned independently once
// there is a cohort's worth of rows to look at.
constexpr uint32 DEATH_RETENTION_DAYS = 90;

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

// ------------------------------------------------------------- death context --
//
// infra#2912. Reading a death back from acore_characters is a photograph
// taken up to fifteen minutes late - PlayerSaveInterval is 900000 - and the
// state that would explain it (what was steering the character, how its
// health had been trending, who was actually swinging at it) is gone by the
// time anyone looks. This section exists to answer those questions AT THE
// MOMENT OF DEATH, from state this module already has sitting in memory from
// drives that were already running - never by adding a query to the death
// path itself. RecordDeath, below, does no database work of any kind.
//
// THREE SMALL CACHES, EACH FED BY A DRIVE THAT WAS ALREADY POLLING.
//
//   g_hpHistory   - last known (health, max_health) and the last tick a
//                   roster character was seen at full health. Refreshed every
//                   SNAPSHOT_MS by WriteSnapshot, which already walks every
//                   online player for the live map.
//
//                   "Health at death" is deliberately NOT read off the
//                   Player* inside a death hook. By the time ANY death hook
//                   fires - OnPlayerKilledByCreature, OnPlayerPVPKill, or
//                   OnPlayerJustDied - Unit::setDeathState has already called
//                   SetHealth(0) (Unit.cpp:11414, pinned core
//                   efe123fab543c5faf3c477674ec17a18fd59f09f), from inside
//                   Unit::Kill (Unit.cpp:14165) which runs BEFORE the
//                   killed-by hooks fire (Unit.cpp:14304, :14311) and well
//                   before Player::KillPlayer/OnPlayerJustDied, which does not
//                   run until the victim's own next Player::Update tick
//                   (PlayerUpdates.cpp:324). The live value is always zero by
//                   every vantage point this module has. The last SAMPLED
//                   reading is the only place a pre-death number still
//                   exists, which is also exactly what the epic asked for:
//                   "HP at time of death and time since last full-health
//                   reading" (infra#2912) describes this cache, not the live
//                   Player*.
//
//   g_aimSnapshot - last known job / quest aim / travel target. Refreshed
//                   every QUEST_POLL_MS by DriveQuests, which already loads
//                   all three every pass (LoadJobs, LoadQuestAims,
//                   LoadTravelAims) - RememberAim there is a cache write next
//                   to a read that was happening anyway, not a new one.
//
//   g_pendingKill - who landed the killing blow, if anyone. Captured by
//                   OnPlayerKilledByCreature / OnPlayerPVPKill below at the
//                   instant Unit::Kill names a killer - see the comment on
//                   those two hooks for why a death hook cannot discover this
//                   any other way once it fires.
//
// A cache read here can be up to one poll interval stale (five seconds for
// health, twenty for the aim). That is a WORSE bound than "at the moment it
// happens" and a categorically better one than acore_characters' fifteen
// minutes, and it costs the death path nothing: every number in it was
// already being computed for another reason on the world thread.
struct HpReading
{
    uint32 health = 0;
    uint32 maxHealth = 0;
    time_t lastFullHealthAt = 0;   // 0 = never seen at full since this cache warmed
    time_t sampledAt = 0;
};
std::mutex g_hpHistoryMutex;
std::map<std::string, HpReading> g_hpHistory;  // key: lowercased character name

struct AimSnapshot
{
    std::string job;            // overseer_roster.job as DriveQuests last saw it
    uint32 questAim = 0;        // overseer_roster.drive_quest, 0 = none
    std::string travelTarget;   // overseer_roster.travel_npc, '' = none
};
std::mutex g_aimMutex;
std::map<std::string, AimSnapshot> g_aimSnapshot;  // key: lowercased character name

struct PendingKill
{
    std::string killerType;   // 'creature' | 'player'
    std::string killerName;
    uint32 killerEntry = 0;   // creature template entry; 0 for a player killer
};
std::mutex g_killMutex;
std::map<std::string, PendingKill> g_pendingKill;  // key: lowercased victim name

// Called from WriteSnapshot, world thread only - see that function.
void RememberHealth(std::string const& name, uint32 health, uint32 maxHealth)
{
    std::lock_guard<std::mutex> guard(g_hpHistoryMutex);
    HpReading& r = g_hpHistory[LowerName(name)];
    r.health = health;
    r.maxHealth = maxHealth;
    r.sampledAt = std::time(nullptr);
    if (maxHealth > 0 && health >= maxHealth)
        r.lastFullHealthAt = r.sampledAt;
}

// Called from DriveQuests, world thread only - see that function.
void RememberAim(std::string const& name, std::string const& job, uint32 questAim,
                 std::string const& travelTarget)
{
    std::lock_guard<std::mutex> guard(g_aimMutex);
    AimSnapshot& a = g_aimSnapshot[LowerName(name)];
    a.job = job;
    a.questAim = questAim;
    a.travelTarget = travelTarget;
}

// Called from the two kill hooks below, whatever thread Unit::Kill happens to
// be running the victim's death on - map-update, same as every other event
// hook in this file (see the file header). Memory only, same discipline as
// RecordEvent: no database work, no resolving anything not already handed in.
void RememberKiller(Player* killed, std::string const& killerType,
                    std::string const& killerName, uint32 killerEntry)
{
    if (!killed || !OnRoster(killed->GetName()))
        return;
    std::lock_guard<std::mutex> guard(g_killMutex);
    PendingKill& k = g_pendingKill[LowerName(killed->GetName())];
    k.killerType = killerType;
    k.killerName = killerName;
    k.killerEntry = killerEntry;
}

// One recorded death, queued for the world thread to write. NOT a repeat of
// overseer_event's 'death' kind: that row is an hourly-bucketed COUNT with no
// room for a killer, a health trend, or an aim - answering "is this still
// happening" - and this table exists because the epic's actual question is
// "what happened THIS time" (infra#2912). The two are deliberately
// independent; OnPlayerJustDied below writes to both.
struct PendingDeath
{
    std::string characterName;
    uint32 characterGuid = 0;
    uint8 level = 0;
    uint16 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.f, y = 0.f, z = 0.f;

    std::string killerType;    // 'creature' | 'player' | 'environment'
    std::string killerName;
    uint32 killerEntry = 0;

    uint32 healthAtDeath = 0;           // last SAMPLED reading - see cache comment above
    uint32 maxHealthAtDeath = 0;
    uint32 secondsSinceFullHealth = 0;  // 0 if never sampled at full since cache warmed

    std::string job;
    uint32 questAim = 0;
    std::string travelTarget;

    uint8 grouped = 0;
    uint8 groupSize = 0;
    std::string groupLeader;
};
std::mutex g_deathMutex;
std::vector<PendingDeath> g_deathQueue;
uint64 g_droppedDeaths = 0;

// A death is never coalesced, unlike g_eventQueue. Every keyed queue in this
// file exists because the SAME fact repeating is not new information; a
// death is never the same fact twice, and this whole table exists to keep
// each one. Same shape as g_chatQueue for the identical reason - see its own
// comment - bounded so a runaway hook can never grow this without limit.
constexpr size_t MAX_DEATH_QUEUE = 200;

// The one place a death's full context is captured. Runs on whatever thread
// OnPlayerJustDied fires on - a map-update thread, per the file header - so
// it does NO database work: it reads three in-memory caches and the
// still-valid Player* (KillPlayer has not yet destroyed anything; the object
// is merely dead), and pushes a struct under a mutex. FlushDeaths, called
// only from OnUpdate on the world thread, is the only code that touches
// MySQL for this table.
void RecordDeath(Player* player)
{
    if (!player)
        return;
    if (!OnRoster(player->GetName()))
        return;

    std::string const lower = LowerName(player->GetName());
    std::time_t const now = std::time(nullptr);

    PendingDeath d;
    d.characterName = player->GetName();
    d.characterGuid = player->GetGUID().GetCounter();
    d.level = player->GetLevel();
    d.mapId = static_cast<uint16>(player->GetMapId());
    d.zoneId = player->GetZoneId();
    d.x = player->GetPositionX();
    d.y = player->GetPositionY();
    d.z = player->GetPositionZ();

    if (Group* group = player->GetGroup())
    {
        d.grouped = 1;
        d.groupSize = static_cast<uint8>(group->GetMembersCount());
        if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
            d.groupLeader = leader->GetName();
    }

    {
        std::lock_guard<std::mutex> guard(g_hpHistoryMutex);
        auto it = g_hpHistory.find(lower);
        if (it != g_hpHistory.end())
        {
            d.healthAtDeath = it->second.health;
            d.maxHealthAtDeath = it->second.maxHealth;
            d.secondsSinceFullHealth = it->second.lastFullHealthAt
                ? static_cast<uint32>(now - it->second.lastFullHealthAt)
                : 0;
        }
    }

    {
        std::lock_guard<std::mutex> guard(g_aimMutex);
        auto it = g_aimSnapshot.find(lower);
        if (it != g_aimSnapshot.end())
        {
            d.job = it->second.job;
            d.questAim = it->second.questAim;
            d.travelTarget = it->second.travelTarget;
        }
    }

    // The killer, if this death arrived through one of the two kill hooks
    // below - CONSUMED, not copied: a stale entry left behind by a PREVIOUS
    // death on this same character must never attach itself to this one, so
    // it is erased whether or not it is used. Absent means the death did not
    // route through Unit::Kill with a non-null killer at all - fall damage,
    // drowning, fatigue, lava, a GM command - and 'environment' is itself the
    // honest answer to "what killed them" for that whole class of death,
    // rather than a blank the reporting layer has to interpret.
    {
        std::lock_guard<std::mutex> guard(g_killMutex);
        auto it = g_pendingKill.find(lower);
        if (it != g_pendingKill.end())
        {
            d.killerType = it->second.killerType;
            d.killerName = it->second.killerName;
            d.killerEntry = it->second.killerEntry;
            g_pendingKill.erase(it);
        }
    }
    if (d.killerType.empty())
        d.killerType = "environment";

    std::lock_guard<std::mutex> guard(g_deathMutex);
    if (g_deathQueue.size() >= MAX_DEATH_QUEUE)
    {
        ++g_droppedDeaths;
        return;
    }
    g_deathQueue.push_back(std::move(d));
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
 *   OnPlayerPVPKill               PlayerScript.h:252   Unit.cpp:14304
 *   OnPlayerKilledByCreature      PlayerScript.h:264   Unit.cpp:14311
 *
 * THE TWO KILL HOOKS EXIST FOR ONE FACT ONLY: WHO. OnPlayerJustDied's own
 * signature is `(Player* player)` - no killer, verified against the pinned
 * core the same way as everything else here - because by the time it fires
 * (Player::KillPlayer, called from Player::Update on the VICTIM's own next
 * tick after death, PlayerUpdates.cpp:324) Unit::CombatStop has already run
 * and cleared whatever the victim was fighting. OnPlayerPVPKill and
 * OnPlayerKilledByCreature fire earlier, from the KILLER's side of
 * Unit::Kill (Unit.cpp:13986), while `killer` is still a live local variable
 * naming exactly who landed the blow - a Player* or a Creature*, handed to
 * the hook directly, never inferred. infra#2912 needed that name and this is
 * the only vantage point in the pinned core that has it. Both are pure
 * capture: they touch no database and drive nothing, they only remember the
 * name (RememberKiller, in the death-context section above this class) for
 * OnPlayerJustDied to pick up moments later when it assembles the death row.
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
        PLAYERHOOK_ON_PVP_KILL,
        PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE,
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

    // Who landed the killing blow, captured while `killer` is still a live
    // local variable in Unit::Kill (Unit.cpp:14304) - see the long comment
    // above this class for why OnPlayerJustDied itself cannot get this. Pure
    // capture, no database work: RememberKiller only ever touches memory.
    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (!killer || !killed)
            return;
        RememberKiller(killed, "player", killer->GetName(), 0);
    }

    // Same capture, the creature-killed-a-player side (Unit.cpp:14311).
    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!killer || !killed)
            return;
        RememberKiller(killed, "creature", killer->GetName(), killer->GetEntry());
    }

    // Deaths carry no subject, so every death in an hour lands on one row with
    // a count. That is deliberate: "Grug died nine times between 02:00 and
    // 03:00" is the observation worth having, and nine rows saying "died" with
    // nothing to tell them apart is not nine times more information.
    //
    // RecordDeath (infra#2912) writes a SEPARATE, un-coalesced row to
    // overseer_death with the context that answers WHY, not just that it
    // happened again - see the death-context section above this class for the
    // whole design. Both calls run in the same hook on purpose: one fact,
    // recorded twice at two different resolutions, is simpler to reason about
    // than two hooks that could someday disagree about when a death occurred.
    void OnPlayerJustDied(Player* player) override
    {
        if (!player)
            return;
        RecordEvent(player, "death", 0, "", "");
        RecordDeath(player);
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
        _travelTimer += diff;
        _professionTimer += diff;
        _eventTimer += diff;
        _engagementTimer += diff;
        _deathTimer += diff;

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
        if (_travelTimer >= TRAVEL_POLL_MS)
        {
            _travelTimer = 0;
            DriveTravel();
        }
        // AFTER DriveQuests and DriveTravel, so an aim either of them just
        // asserted or released this same tick is what this drive sees too -
        // there is no reason for this to be looking at a tick-stale answer to
        // "does this character have an aim" when the two drives that OWN that
        // answer already ran. See DriveEngagementSafety for the rest of the
        // reasoning; this does not touch either drive above it.
        if (_engagementTimer >= ENGAGEMENT_POLL_MS)
        {
            _engagementTimer = 0;
            DriveEngagementSafety();
            // Same cadence, deliberately - see DriveStuckRevival's own WHY
            // block. Reuses this timer rather than adding a new one; there is
            // no reason this needs a different poll interval.
            DriveStuckRevival();
            // Same cadence again, and after the revival drive on purpose: a
            // character resurrected on this tick is alive by the time this one
            // asks, so a party that just recovered from a wipe inside the
            // instance is re-armed on the same pass rather than the next.
            DriveDungeonClear();
        }
        // AFTER DriveTravel, on purpose. Both can run in the same tick, and
        // when they do the order that helps is travel first: a character that
        // has just arrived gets its learn attempt on this tick, discovers the
        // slot is still full, says so, and the unlearn that clears it runs
        // moments later - so the NEXT travel poll finds a free slot waiting.
        // The other order costs nothing either; it just takes one more poll.
        if (_professionTimer >= PROFESSION_POLL_MS)
        {
            _professionTimer = 0;
            DriveProfessions();
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
        if (_deathTimer >= DEATH_FLUSH_MS)
        {
            _deathTimer = 0;
            FlushDeaths();
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
            // Swept on created_at, unlike overseer_event: a death is never
            // updated after the fact (see PendingDeath's own comment on why
            // it is never coalesced), so there is no last_seen for a still-
            // happening problem to protect - only a first and only occurrence.
            CharacterDatabase.Execute(
                "DELETE FROM overseer_death WHERE created_at < NOW() - INTERVAL {} DAY",
                DEATH_RETENTION_DAYS);
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

        // Runs LAST on purpose: it hands out the leader that the block above
        // has just finished correcting.
        KeepRosterFollowing(group, present);
    }

    // Give the followers somebody to follow (infra#2818).
    //
    // WHY `follow` HAS NEVER MOVED ANYBODY. It is on all five and it is not
    // broken. It resolves through a formation, and every formation resolves
    // through the MASTER. FormationValue's default is ChaosFormation
    // (Formations.cpp:506-507); ChaosFormation is a MoveAheadFormation and does
    // not override GetTargetName, so it inherits the base's `return ""`
    // (Formations.h:23), which sends FollowAction::Execute down the location
    // branch (FollowActions.cpp:218) rather than the named-target one. That
    // branch calls GetLocation(), which opens
    //
    //     Player* master = GetMaster();
    //     if (!ValidateTargetContext(master, bot))
    //         return Formation::NullLocation;
    //
    // (Formations.cpp:50-54, and again inside ChaosFormation::
    // GetLocationInternal at Formations.cpp:140-143). No master is a null
    // location, and Execute returns false (FollowActions.cpp:219-220). Nobody
    // has ever followed anybody. What looked like cohesion was the whole party
    // standing still: measured live, max pairwise spread 935 yards and rising,
    // with no restoring force of any kind.
    //
    // WHY THE FAMILY CANNOT ACQUIRE ONE BY ITSELF. UpdateAIGroupMaster() runs
    // unconditionally from UpdateAI every tick (PlayerbotAI.cpp:397) and
    // rechecks at PlayerbotAI.cpp:437 - but it only ever assigns what
    // FindNewMaster() hands back, and FindNewMaster returns the group leader
    // only when the leader is NOT a bot or IS a selfbot
    // (PlayerbotAI.cpp:4420), a member on the same test
    // (PlayerbotAI.cpp:4431), and otherwise nullptr (PlayerbotAI.cpp:4450).
    // Five bots, none of them a selfbot while nobody is logged in, so it
    // returns nullptr and the `if (newMaster)` guard at PlayerbotAI.cpp:440
    // assigns nothing AND clears nothing.
    //
    // WHY AN EXPLICIT MASTER STICKS. That same nullptr is what makes this hold:
    // the recheck fires every tick for a bot master, calls FindNewMaster, gets
    // nullptr, and leaves `master` exactly as it found it. The assignment below
    // is made once per poll and is not overwritten in between.
    //
    // THE OBSERVER EFFECT THAT HID ALL OF THIS. With SelfBotLevel = 3 a human
    // logging in as the leader IS a selfbot, IsSelfBot(groupLeader) goes true,
    // and every follower is handed him with `+follow` on the spot
    // (PlayerbotAI.cpp:440-448). Every in-game verification passed for exactly
    // as long as somebody was watching, and the bug was live the instant the
    // client closed. Nothing about this may be checked with anyone logged in.
    //
    // `+follow` IS GRANTED WITH THE MASTER, BECAUSE UPSTREAM TREATS THEM AS ONE
    // ACT. Its own assignment sets the master and adds the strategy in the same
    // breath (PlayerbotAI.cpp:443, then :448). Doing only the first half leaves
    // a bot holding a master it has no reason to walk toward, and this poll
    // would log "now follows" for a character that cannot - a log line that
    // reads as success while the feature is inert, which is the exact failure
    // infra#2819 exists to stamp out. So the strategy is checked every poll,
    // not only when the master changes, and its absence is said out loud.
    //
    // NO ResetStrategies(), AND NOT FOR THE REASON IT FIRST APPEARS. Upstream
    // calls it alongside its own assignment (PlayerbotAI.cpp:444). The obvious
    // worry - that a reset hands followers `new rpg` back at relevance 3.0-11.0
    // against follow's 1.0 and reproduces the 937-yard scatter of infra#2812 -
    // is WRONG, and it is worth knowing why, because it is the difference
    // between a nuisance and a re-run of that scatter: `new rpg` is added only
    // for a bot that is ungrouped or is the group leader (AiFactory.cpp:602),
    // itself behind an IsRandomBot gate (AiFactory.cpp:591). A grouped follower
    // passes neither. Nor would a reset strip `follow`: it is in the default
    // non-combat set for every non-battleground bot (AiFactory.cpp:584), and
    // ResetStrategies rebuilds from exactly that (PlayerbotAI.cpp:1872-1874).
    //
    // The real reason to leave it alone is smaller and duller: a reset discards
    // every OTHER strategy the bridge has applied and rebuilds from class
    // defaults, which is churn this pass has no need of. Only the master was
    // missing, so only the master is set - and the one strategy that must
    // accompany it is added, never removed.
    //
    // WHAT UPSTREAM DOES DO ON LOGOUT. When the person playing the leader logs
    // out, RandomPlayerbotMgr::OnPlayerLogout clears the followers' master
    // (RandomPlayerbotMgr.cpp:2515) AND calls ResetStrategies on each of them
    // (RandomPlayerbotMgr.cpp:2518) - every single time the client closes,
    // which is the precise moment this feature exists to cover. By the two
    // citations above that leaves `follow` present and `new rpg` absent, so the
    // next poll re-points them at the leader and the family keeps following.
    // Other bridge-applied strategy state is lost until the bridge's next cycle.
    //
    // NO DANGLING MASTER, AND NOT BY LUCK. `master` is a raw Player*; held
    // across a logout it is a use-after-free on the next FollowAction tick,
    // which dereferences it (FollowActions.cpp:104). Upstream already clears
    // it: WorldSession::LogoutPlayer calls OnPlayerbotLogout for any player
    // (WorldSession.cpp:721 - ahead of the `redirecting` guard that gates the
    // other logout hook at WorldSession.cpp:850-857), which reaches
    // RandomPlayerbotMgr::OnPlayerLogout (Playerbots.cpp:457), which clears the
    // master of every bot in its PlayerBotMap that pointed at the departing
    // player (RandomPlayerbotMgr.cpp:2509-2515). These five ARE in that map:
    // KeepRosterOnline logs them in with sRandomPlayerbotMgr.AddPlayerBot and a
    // masterAccountId of 0, which routes the login callback to
    // RandomPlayerbotMgr::instance() (PlayerbotMgr.cpp:186), OnBotLoginOperation
    // resolves the same holder (PlayerbotOperations.h:499), and OnBotLogin
    // inserts them (PlayerbotMgr.cpp:468). Bot logouts take the same road:
    // LogoutPlayerBot calls botWorldSessionPtr->LogoutPlayer(true)
    // (PlayerbotMgr.cpp:408). This module therefore keeps no Player* of its own
    // between polls - the guarantee covers PlayerbotAI::master and nothing else.
    //
    // NO CHANGE TO COMMAND DELIVERY. Commands are delivered through the
    // character's own session with the bot itself as the speaker, and
    // PlayerbotSecurity::CheckLevelFor short-circuits on `from == bot`
    // (PlayerbotSecurity.cpp:178) before the master is consulted at all. A bot
    // that now has a master is no more and no less commandable than before.
    void KeepRosterFollowing(Group* group, std::vector<Player*> const& present)
    {
        if (!group)
            return;

        // The leader the GROUP actually has, not the one the roster prefers.
        // Those differ for one poll every time leadership drifts, and a family
        // following the character that is about to stop leading is a party
        // split in two for thirty seconds.
        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (!leader)
            return;

        // THE LEADER FOLLOWS NOBODY. RandomPlayerbotMgr::OnPlayerLogin hands a
        // master to every bot grouped with an arriving character when its own
        // master is null or is a bot (RandomPlayerbotMgr.cpp:2582-2586), and
        // that includes the leader. A leader following one of his own followers
        // is a cohesion loop with no fixed point: the party converges on
        // nothing and drifts as a clump. A human driving this character is a
        // selfbot, whose master is himself, so HasGameClientMaster() is true
        // and he is left alone.
        if (PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader))
        {
            if (leaderAI->GetMaster() && !leaderAI->HasGameClientMaster())
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' leads, so he follows nobody", leader->GetName());
                leaderAI->SetMaster(nullptr);
            }

            // AND THE LEADER MUST ACTUALLY CARRY `new rpg`, BECAUSE NOTHING
            // ELSE IN THE WORLD EVER GRANTS IT TO THIS FAMILY.
            //
            // This file states the invariant in three places - "exactly one
            // character carries `new rpg`, and it is the group leader" - and
            // then says, correctly, "Nothing below hands `new rpg` to
            // anybody". The invariant was ASSUMED to be established by
            // upstream. It is not, and cannot be: AiFactory.cpp:615 is the
            // ONLY site that ever adds "new rpg", and it sits inside
            // `if ((sRandomPlayerbotMgr.IsRandomBot(player)) && ...)` opened
            // at AiFactory.cpp:591. The family are roster characters on real
            // accounts, not random bots, so that gate is false for all five
            // forever - INCLUDING the leader. Zero characters carry it, not
            // one, and the invariant this epic depends on has never once held.
            //
            // The failure is silent and looks exactly like success, which is
            // why it survived so long: `rpgInfo.ChangeToDoQuest()` sets the
            // STATUS and logs "now working quest N", but only the `new rpg`
            // STRATEGY runs NewRpgAction, the thing that reads that status and
            // walks the character to the objective. Status set, nothing
            // executing it, a confident log line, and five characters standing
            // in a field (infra#2795 / mod-overseer#29).
            //
            // MEASURED, not reasoned: the family stood motionless at
            // (-8950, -132.5) in Elwynn for over an hour holding actionable
            // quests - several already COMPLETE and needing only the walk back
            // to a giver - across a full worldserver restart and a fresh quest
            // aim. One `nc +new rpg` to the leader and they were moving within
            // fifteen seconds, the other four following him.
            //
            // Granted like `follow` below and for the same reason: checked
            // every poll rather than once, because a strategy that goes
            // missing under a leader that is otherwise correct is precisely
            // the silently-inert case. WARN rather than INFO because reaching
            // here means the character could not have been questing.
            //
            // A HUMAN AT THE KEYBOARD IS LEFT ALONE, same test the master
            // assignment above uses: a selfbot's own strategy list is the
            // player's business.
            if (!leaderAI->HasGameClientMaster() &&
                !leaderAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' leads but did not carry `new rpg` - granting it, "
                         "because a quest status nothing executes is a character standing "
                         "in a field", leader->GetName());
                leaderAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);
            }
        }

        for (Player* p : present)
        {
            if (p == leader)
                continue;
            // `present` is the roster; a member left out because the party was
            // full is not in this group and must not be pointed at its leader.
            if (p->GetGroup() != group)
                continue;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(p);
            if (!botAI)
                continue;   // somebody is sitting at this one; nothing to set

            // A HUMAN AT THE KEYBOARD WINS, and owns this character outright -
            // master and strategies both. HasGameClientMaster() is upstream's
            // own test for "the master is a real player or a selfbot" -
            // `return IsRealPlayer(master) || IsSelfBot(master);`
            // (PlayerbotAI.cpp:4459, declared public at PlayerbotAI.h:544). It
            // is null-safe both ways: IsRealPlayer null-checks
            // (PlayerbotAI.cpp:4394) and IsSelfBot goes through
            // GET_PLAYERBOT_AI (PlayerbotAI.cpp:4400). Without this, the poll
            // would take the family back off the person playing every thirty
            // seconds, forever.
            if (botAI->HasGameClientMaster())
                continue;

            // SetMaster is public - PlayerbotAI.h:571, inside the `public:`
            // block opened at PlayerbotAI.h:386 - so this needs no patch to the
            // pinned tree. Assigned only when it is actually wrong: saying "now
            // follows" every thirty seconds would stop the line being evidence
            // of anything.
            if (botAI->GetMaster() != leader)
            {
                botAI->SetMaster(leader);
                LOG_INFO("module.overseer", "overseer: '{}' now follows '{}'",
                         p->GetName(), leader->GetName());
            }

            // CHECKED EVERY POLL, NOT ONLY WHEN THE MASTER CHANGES. A follow
            // strategy that goes missing under a master that is already correct
            // would otherwise never be noticed, and that is precisely the
            // silently-inert case: a master assigned, nothing following it, and
            // a log that said "now follows" once and looked fine ever after.
            //
            // WARN, not INFO, and granted rather than assumed. `follow` is an
            // AiFactory default for every non-battleground bot
            // (AiFactory.cpp:584), so its absence means something took it off.
            // Adding it silently would paper over that; refusing to add it
            // would leave the master pointing at nobody. So: add it, and say
            // that it had to be added. HasStrategy is PlayerbotAI.h:416 and
            // ChangeStrategy PlayerbotAI.h:407, both public; the "+name" form
            // and the BOT_STATE_NON_COMBAT state are upstream's own pairing at
            // PlayerbotAI.cpp:448.
            if (!botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' had a master but no follow strategy - granting "
                         "it, because a master nobody walks toward is not cohesion",
                         p->GetName());
                botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
            }
        }
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
    // THE DDL AND THIS READER SHIP IN DIFFERENT IMAGES. The coupling is
    // infra#2846 - "module code and SQL move together" - which pinned the two
    // dev images to one build after #2824's migration silently never applied.
    // That fixed the DEPLOYMENT half. This is the CODE half: the reader has to
    // survive the pair coming apart anyway, because a pin is a discipline and
    // this is a guarantee.
    //
    // An earlier version of this comment claimed the opposite: that a column
    // and its reader are carried by one image and therefore cannot disagree,
    // and that mod-overseer applies its own SQL at worldserver startup. It was
    // false in both halves, it was the stated reason the query below needed no
    // degrade, and the family stopped questing because of it.
    //
    // WHERE EACH HALF ACTUALLY LIVES. The DDL is in this module's
    // data/sql/characters/base/, which reaches a container only through the
    // DB-IMPORT image: production/oke/manifests/wow/30-db-import.yaml:117 says
    // it outright - "only db-import gets `COPY data data`, so the worldserver
    // has no SQL" - and that is why the worldserver is started with
    // AC_PLAYERBOTS_UPDATES_ENABLE_DATABASES=0 (50-worldserver.yaml:407). The
    // migrations are applied by the `db-upgrade` initContainer, which runs the
    // DB-IMPORT image (50-worldserver.yaml:148). The reader - this file - is
    // compiled into the WORLDSERVER image (50-worldserver.yaml:172). Two
    // digests, pinned independently in that one manifest and bumped
    // independently. Bumping the worldserver alone is a routine one-line
    // change, and it puts new module code in front of an old schema. That is
    // not hypothetical: it is what the dev world was running when this was
    // found.
    //
    // WHAT MYSQL DOES ABOUT IT. A SELECT naming a column the table does not
    // have fails WHOLE - error 1054, no partial rows - and
    // CharacterDatabase.Query hands that back as a null QueryResult,
    // indistinguishable from "no rows matched". So every `if (!result) return;`
    // in this file reads a missing column as "there is nothing to do", which is
    // the right answer ONLY when the column in question is the entire reason
    // the query is being run.
    //
    // SO EACH LATE-ADDED COLUMN IS READ ON ITS OWN. LoadQuestAims and
    // LoadTravelAims below each select one column and each return an EMPTY map
    // when the read comes back null. Every caller already handles an empty map,
    // because that is also what "nobody is aimed" looks like - which is why
    // there is no 1054 test anywhere here and does not need to be: the error
    // case and the empty case want the same answer. A missing `travel_npc`
    // costs the errands. A missing `drive_quest` costs the council's aim and
    // leaves the leader's own-log fallback and the repick memory running. What
    // neither can do any more is take the whole drive off the air.
    //
    // The roster query itself is left holding `name`/`enabled` (the CREATE
    // TABLE, 2026_08_23_00) and `lead` (2026_08_23_01) - all older than this
    // drive and read unguarded by KeepRosterGrouped as well (:1148). If those
    // are missing there is no roster feature at all, and no behaviour to
    // degrade to.

    // Every enabled character with a standing council aim, name -> quest id.
    // Absent means 0, "no opinion", the sentinel the column itself defines
    // (2026_08_24_00_overseer_roster_drive_quest.sql) - so a row filtered out
    // here, a row that was never written, and a column that does not exist are
    // all the same thing to the caller, deliberately.
    std::map<std::string, uint32> LoadQuestAims()
    {
        std::map<std::string, uint32> aims;
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, drive_quest FROM overseer_roster "
            "WHERE enabled = 1 AND drive_quest <> 0");
        if (!result)
            return aims;  // nobody aimed, or no such column - same answer
        do
        {
            Field* fields = result->Fetch();
            aims[fields[0].Get<std::string>()] = fields[1].Get<uint32>();
        } while (result->NextRow());
        return aims;
    }

    // The character behind a name, ONLY if it is safe to steer this tick.
    //
    // FOUND AND STEERABLE ARE DIFFERENT THINGS, and this module learned the
    // difference from a segfault. ObjectAccessor::FindPlayerByName answers
    // "is there a Player object with this name" - not "is that object in the
    // world and finished being set up". Between those two states sits exactly
    // the window infra#2663's POV streaming opens several times an hour: a
    // real client logs in as a family character, the altbot holding that name
    // is evicted, and for a moment the name resolves to a Player that is
    // mid-teardown or mid-login with a PlayerbotAI that is being destroyed.
    // Three drives then did this, unguarded:
    //
    //     Player* bot = ObjectAccessor::FindPlayerByName(name);
    //     if (!bot) continue;
    //     PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    //     if (!botAI) continue;
    //     ... botAI->rpgInfo.ChangeToDoQuest(...)
    //
    // and a non-null pointer to a half-destroyed AI passes both checks.
    //
    // THE EVIDENCE. worldserver exited 139 (SIGSEGV) with its log cut off
    // mid-line, part-way through DriveQuests' own message:
    //
    //     15:14:59 overseer: 'Grog' re-asserting chosen quest 418 ...
    //     15:14:59 overseer: 'Grug' now working quest 5 ...
    //     15:14:59 overseer: 'Ugga' re-asserting chosen quest 418 (Thelsamar
    //              Blood Sausages) - the 30-minute            <- ends here
    //
    // Two entries in the same loop had already been steered; the third died on
    // the call after the log. Thirty-nine seconds earlier the stream agent had
    // logged `selfbot already attached for Og` - a real client taking over a
    // family character. Six such exits in twenty hours, all while that feature
    // was in use.
    //
    // This is a correlation and a missing guard, not a backtrace, so the guard
    // is written to be correct regardless of whether that race is the whole
    // story: nothing here should ever have been steering a character that is
    // not in the world with a live session.
    //
    // IsInWorld() is the check this file already makes before touching a
    // watcher (:507) and a snapshot subject (:4338). The drives simply never
    // adopted it.
    static PlayerbotAI* SteerableAI(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->GetSession())
            return nullptr;
        return GET_PLAYERBOT_AI(bot);
    }

    // Every enabled character with an outstanding errand, name -> target.
    // Absent means '', "stay with the family"
    // (2026_08_25_00_overseer_roster_travel_npc.sql).
    //
    // ONE READER, TWO CALLERS, AND NO SHARED STATE. DriveQuests needs this to
    // run the arbitration and DriveTravel needs it to run the errands. They are
    // on SEPARATE timers - QUEST_POLL_MS and TRAVEL_POLL_MS - so there is no
    // tick they reliably share and no single snapshot that could serve both
    // without being stale for one of them. Sharing the QUERY instead of a
    // cached result keeps each drive reading the table as it stands when it
    // runs, and keeps DriveTravel from having to know that DriveQuests exists -
    // which TravelHoldsTheWheel forbids, for the reason set out there.
    std::map<std::string, std::string> LoadTravelAims()
    {
        std::map<std::string, std::string> aims;
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, travel_npc FROM overseer_roster "
            "WHERE enabled = 1 AND travel_npc <> ''");
        if (!result)
            return aims;  // no errands, or no such column - same answer
        do
        {
            Field* fields = result->Fetch();
            aims[fields[0].Get<std::string>()] = fields[1].Get<std::string>();
        } while (result->NextRow());
        return aims;
    }

    // Every enabled character's job mode (infra#2834), name -> mode. Absent
    // means the schema's own default, 'quest'
    // (2026_08_26_01_overseer_roster_job.sql) - so a row nobody has touched, a
    // row explicitly set to 'quest', and a `job` column that does not exist
    // yet are all the same thing to DriveQuests, deliberately: read-on-its-own,
    // same discipline as LoadQuestAims and LoadTravelAims and for the same
    // reason (the DDL and this reader ship in different images).
    //
    // ONLY 'quest' IS EXCLUDED FROM THE QUERY rather than every mode fetched
    // and filtered here: DriveQuests only ever needs to know "should I stand
    // down", so the map holds exactly the characters whose mode is something
    // other than quest, and absence from it already means "drive as normal" -
    // no separate default branch to keep in sync with the schema's DEFAULT.
    std::map<std::string, std::string> LoadJobs()
    {
        std::map<std::string, std::string> jobs;
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, job FROM overseer_roster "
            "WHERE enabled = 1 AND job <> '' AND job <> 'quest'");
        if (!result)
            return jobs;  // everybody questing, or no such column - same answer
        do
        {
            Field* fields = result->Fetch();
            jobs[fields[0].Get<std::string>()] = fields[1].Get<std::string>();
        } while (result->NextRow());
        return jobs;
    }

    // IS A RUN IN PROGRESS FOR THIS CHARACTER, AND THEREFORE IS THIS DRIVE
    // ALLOWED TO STEER IT AT ALL?
    //
    // WHY THIS IS ONE FUNCTION AND NOT A CHECK PER DRIVE. This module runs
    // several independent periodic drives - quests, travel, professions,
    // engagement safety, dungeon arming, revival - and every one of them writes
    // to the same characters. There was no arbitration, only gates added one at
    // a time as each collision was discovered in production. The quest drive
    // overwriting a dungeon run was not a bug in the quest drive; it was the
    // design working as built.
    //
    // Six independent model reviews of this architecture all returned the same
    // verdict and the same remedy: the party and the RUN should be the unit of
    // control, not the character and the tick, and exactly one component may
    // steer while a run is active (see the epic). This predicate is the first
    // slice of that: a single named concept the drives agree on, replacing
    // literal `GetMap()->IsDungeon()` checks copy-pasted into whichever drive
    // last collided with something.
    //
    // BEING ON AN INSTANCE MAP IS THE FACT, deliberately, rather than "holds the
    // dungeon-clear strategy". A character that entered before the arming drive's
    // next poll would otherwise be steered by everything else in that window,
    // which is the race this closes. The strategy is a consequence of being
    // inside; the map is the cause.
    //
    // Members read at the pinned core:
    //   GetMap     Object.h:631     Map* GetMap() const
    //   IsDungeon  Map.h:298        bool IsDungeon() const
    static bool InDungeonRun(Player* bot)
    {
        if (!bot)
            return false;

        // GEOGRAPHY IS NECESSARY AND NOT SUFFICIENT. Being on an instance map
        // is what makes a run POSSIBLE; an open run row is what makes one
        // REAL. Slice 1 asked only the first question, and three independent
        // reviews named the same defect: "a character can be in a dungeon
        // without an active run, and this code will still stand down all
        // drives even though no run exists to arbitrate."
        //
        // That mattered because a character inside with no run would have had
        // every drive stand down on it and nothing pick it back up - the same
        // stranding shape this drive family has produced four times. Ownership
        // inferred from position produces a state nobody owns.
        Map* map = bot->GetMap();
        if (!map || !map->IsDungeon())
            return false;

        return ActiveRunOnMap(bot->GetMapId());
    }

    // Is a run open on this map? Cached for the length of a tick because every
    // steering drive asks the same question about the same handful of
    // characters, and the answer cannot change between two drives in one tick.
    static bool ActiveRunOnMap(uint32 mapId)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT 1 FROM overseer_dungeon_run WHERE state = 'active' AND map_id = {} LIMIT 1",
            mapId);
        return result != nullptr;
    }

    // Open a run, or keep the open one alive. Called by the arming drive, which
    // is the component that already notices a character is inside.
    //
    // ONE RUN PER MAP, not per character: the five of them share an instance and
    // a run is the thing they share. A second character walking in joins the
    // run that exists rather than starting a rival one.
    static void OpenOrTouchRun(std::string const& leaderName, uint32 mapId)
    {
        // ONE STATEMENT, BECAUSE TWO WOULD BE A RACE. The first draft asked
        // "is there an active run?" and inserted if not. A review caught it at
        // once: two characters stepping through the portal in the same tick
        // both see no run and both insert, and the one-run-per-map invariant
        // this whole slice rests on is gone. A SELECT-then-INSERT is not a
        // claim, it is a hope.
        //
        // The table carries a generated `active_map` column - the map id while
        // active, NULL once ended - under a unique key. NULLs do not collide,
        // so any number of finished runs may share a map while at most one live
        // run ever can. This insert is therefore atomic in both directions: the
        // winner opens the run, the loser touches its heartbeat, and neither
        // needs to know which it was.
        CharacterDatabase.Execute(
            "INSERT INTO overseer_dungeon_run (leader_name, map_id, state) "
            "VALUES ('{}', {}, 'active') "
            "ON DUPLICATE KEY UPDATE last_progress_at = NOW()",
            Esc(leaderName), mapId);

        // SAYING WHICH IT WAS, WITHOUT ASKING THE INSERT. The statement above
        // is deliberately atomic, which means it cannot report whether it
        // opened a run or touched one - and losing the open from the log would
        // be a real loss, because a run that begins silently is exactly as hard
        // to reason about as one that ends silently.
        //
        // So it is read back rather than inferred: a row whose heartbeat has
        // never moved off its own start time is one nobody had touched before
        // this call. That is a query, but only on the arming path, which
        // already only runs for a character that is inside and unarmed.
        if (QueryResult opened = CharacterDatabase.Query(
                "SELECT id FROM overseer_dungeon_run WHERE state = 'active' AND map_id = {} "
                "AND last_progress_at = started_at LIMIT 1", mapId))
        {
            LOG_INFO("module.overseer",
                     "overseer: opened dungeon run {} on map {} - '{}' is the first of "
                     "the roster inside, so the run now owns whoever is in there",
                     opened->Fetch()[0].Get<uint32>(), mapId, leaderName);
        }
    }

    // Close any run whose map no longer holds a single roster character.
    //
    // SAID OUT LOUD, ALWAYS. A run that ends silently is indistinguishable from
    // a run that is still open and doing nothing, and telling those two apart is
    // the whole reason this table exists.
    void CloseAbandonedRuns()
    {
        // CLOSED ON A COLD HEARTBEAT, NOT ON AN EMPTY ROOM.
        //
        // The first draft asked FindPlayerByName for each roster member and
        // closed the run when none answered. Review caught the hole: that
        // lookup finds players who are ONLINE, not players who are on the map.
        // A character that walked in and then logged out is still in the
        // instance, the lookup returns nothing, the run closes as abandoned,
        // and when they come back a second run opens on a map that already had
        // one - breaking the exact invariant this table exists to hold.
        //
        // The suggested remedy was to enumerate Map::GetPlayers() instead, but
        // reaching a Map* means already having a player on it, which is the
        // same question being asked. So the answer is not a better census.
        //
        // It is the heartbeat, which is already here. The arming drive touches
        // last_progress_at on every pass while anyone from the roster is inside.
        // A run whose heartbeat has gone cold is therefore one nobody has been
        // seen in for a while - which covers an empty instance, a logged-out
        // character, and a server that stopped ticking, without needing to tell
        // those three apart.
        //
        // The grace period is generous on purpose. Closing early re-opens the
        // stranding this slice exists to prevent; closing late costs only that
        // a finished run lingers as a row, which nothing suffers from.
        QueryResult runs = CharacterDatabase.Query(
            "SELECT id, map_id, TIMESTAMPDIFF(SECOND, last_progress_at, NOW()) "
            "FROM overseer_dungeon_run WHERE state = 'active' "
            "AND last_progress_at < NOW() - INTERVAL {} SECOND",
            RUN_COLD_SECONDS);
        if (!runs)
            return;

        do
        {
            Field* f = runs->Fetch();
            uint32 const runId = f[0].Get<uint32>();
            uint32 const mapId = f[1].Get<uint32>();
            uint32 const coldFor = f[2].Get<uint32>();

            // THE COLD CONDITION IS REPEATED IN THE UPDATE, DELIBERATELY.
            //
            // Selecting cold runs and then closing them by id alone is the same
            // mistake as the SELECT-then-INSERT this file already fixed one
            // function above, and it was made again here in the fix for that
            // one: between the SELECT and this write, the arming drive can
            // touch last_progress_at, and an id-only UPDATE would close a run
            // that had just come back to life. That is a premature close, which
            // is precisely the stranding this whole slice exists to prevent -
            // every drive resumes steering a character who is still inside.
            //
            // Carrying the predicate into the write makes the close atomic with
            // the decision to close. A run refreshed in that window simply is
            // not matched, and the next pass will judge it fresh.
            CharacterDatabase.Execute(
                "UPDATE overseer_dungeon_run SET state = 'ended', ended_at = NOW(), "
                "ended_reason = 'heartbeat cold - nobody from the roster seen on the map' "
                "WHERE id = {} AND state = 'active' "
                "AND last_progress_at < NOW() - INTERVAL {} SECOND",
                runId, RUN_COLD_SECONDS);

            LOG_INFO("module.overseer",
                     "overseer: closed dungeon run {} on map {} - its heartbeat had been "
                     "cold for {}s, so nobody from the roster has been inside",
                     runId, mapId, coldFor);
        } while (runs->NextRow());
    }

    void DriveQuests()
    {
        // Read first, and each on its own, so that neither aim column can stop
        // this drive from running - see above.
        std::map<std::string, uint32> const aims = LoadQuestAims();
        std::map<std::string, std::string> const travelAims = LoadTravelAims();
        // infra#2834: everybody NOT holding job 'quest'. Checked first in the
        // loop below, before any quest aim or travel arbitration - a
        // character told to farm or rest should not have a quest re-asserted
        // out from under it just because nothing farm-specific exists yet to
        // replace the assertion with. See LoadJobs above for what absence
        // from this map means.
        std::map<std::string, std::string> const jobs = LoadJobs();

        QueryResult result = CharacterDatabase.Query(
            // EVERY enabled member, not only the leader (infra#2801, "quest
            // together"). Handing a quest in is reachable only through the rpg
            // strategy, so a follower that cannot be aimed cannot ever turn
            // anything in - which is why the four hoarded completed quests
            // while the leader handed his own in. `lead` still comes back
            // because the FALLBACK below stays leader-only: an unaimed
            // follower carrying `new rpg` is the 937-yard scatter, and the aim
            // is the only thing holding the party to one destination.
            //
            // Neither AIM column is here. `drive_quest` and `travel_npc` are
            // read by their own guarded loaders above, so that a schema older
            // than either one costs that aim and not this whole drive
            // (infra#2846).
            "SELECT name, `lead` FROM overseer_roster "
            "WHERE enabled = 1");
        // A roster query that comes back empty means there is no roster. That
        // IS nothing to do, and it is the one case where returning is right.
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            std::string const name = fields[0].Get<std::string>();
            bool const isLead = fields[1].Get<uint8>() != 0;

            // THE JOB GATE (infra#2834). A character whose schedule says
            // something other than 'quest' gets no quest aim asserted and no
            // fallback to its own log - it is stood down from this drive
            // entirely, the same shape as the travel hand-off just below.
            // Deliberately does NOT touch strategies or rpgInfo: nothing yet
            // exists to positively drive farm/dungeon/grind/etc (see
            // jobs.py's IMPLEMENTED), so the honest behaviour is "stop
            // questing", not a mode this file cannot yet make good on.
            auto const jobIt = jobs.find(name);

            // Absent from a map is the column's own "no opinion" value, which
            // is also what a column the schema does not have degrades to. Every
            // decision below is written against those sentinels already, so
            // nothing downstream has to know which of the two it is looking at.
            auto const aimIt = aims.find(name);
            uint32 const aim = aimIt == aims.end() ? 0u : aimIt->second;
            auto const travelIt = travelAims.find(name);
            std::string const travelTarget =
                travelIt == travelAims.end() ? std::string() : travelIt->second;

            // infra#2912: the aim cache RecordDeath reads, kept fresh here
            // rather than queried fresh in the death path - see the
            // death-context comment above OverseerEventScript for why. Written
            // for EVERY roster row this loop sees, including one the job gate
            // is about to stand down, because a death while farming still
            // deserves its job recorded accurately.
            RememberAim(name, jobIt == jobs.end() ? "quest" : jobIt->second, aim,
                       travelTarget);

            // THE JOB GATE (infra#2834). A character whose schedule says
            // something other than 'quest' gets no quest aim asserted and no
            // fallback to its own log - it is stood down from this drive
            // entirely, the same shape as the travel hand-off just below.
            // Deliberately does NOT touch strategies or rpgInfo: nothing yet
            // exists to positively drive farm/dungeon/grind/etc (see
            // jobs.py's IMPLEMENTED), so the honest behaviour is "stop
            // questing", not a mode this file cannot yet make good on.
            if (jobIt != jobs.end())
            {
                LOG_DEBUG("module.overseer",
                          "overseer: '{}' job is '{}', not quest - the quest "
                          "drive stands down for it", name, jobIt->second);
                continue;
            }

            // SteerableAI, not a bare lookup: a name can resolve to a
            // Player that is mid-login or mid-teardown, and its AI pointer is
            // non-null right up until it is freed. See SteerableAI above.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            PlayerbotAI* botAI = SteerableAI(bot);
            if (!botAI)
                continue;

            // THE DUNGEON GATE. Same shape as the job gate above and the
            // travel hand-off below: something else is steering, so this drive
            // takes its hands off the wheel.
            //
            // WHY IT HAS TO EXIST. A quest aim inside an instance is not merely
            // useless, it is actively destructive. Measured live in Deadmines
            // with the whole party inside, at full health, nobody in combat:
            //
            //     22:47:06  'Ugga' now working quest 14 (The People's Militia)
            //     22:47:26  'Ugga' now working quest 22 (Goretusk Liver Pie)
            //     22:47:46  'Ugga' now working quest 14 (The People's Militia)
            //     22:48:06  'Ugga' now working quest 22 (Goretusk Liver Pie)
            //     22:48:26  'Ugga' now working quest 14 (The People's Militia)
            //
            // Two OUTDOOR Westfall quests, alternating every twenty seconds,
            // for half an hour. Neither objective exists on this map, so
            // neither aim can ever be satisfied and neither can ever be
            // abandoned - and each re-assertion overwrites the rpgInfo the
            // dungeon run needs. The party did not move, did not fight, and did
            // not gain a single point of XP in thirty minutes while this ran.
            //
            // A dungeon is the one place where "carry on questing" is knowably
            // wrong: quest POIs live on the outdoor map, and a character that
            // walked into an instance did so because something else decided it
            // should be there.
            //
            // DELIBERATELY MAP-BASED, NOT STRATEGY-BASED. Gating on "has the
            // dungeon clear strategy" would mean a character that entered
            // before the arming drive's next poll still gets a quest aim
            // written over it in that window, which is exactly the race this is
            // here to close. Being on an instance map is the fact; the strategy
            // is a consequence of it.
            //
            // Members read at the pinned core, cited so the guard in
            // tests/test_quest_aim.py can hold them to it:
            //   GetMap     Object.h:631     Map* GetMap() const
            //   IsDungeon  Map.h:298        bool IsDungeon() const
            //   GetMapId   Position.h:281   uint32 GetMapId() const
            if (InDungeonRun(bot))
            {
                LOG_DEBUG("module.overseer",
                          "overseer: '{}' is in a dungeon run - the quest drive "
                          "stands down", name);
                continue;
            }

            // The aim as this loop last saw it, so a standing complaint is made
            // once rather than three times a minute forever. Nothing here is
            // read as a decision: the roster row remains the only real state.
            AimState& state = _lastAim[name];

            // ---------------------------------------------- arbitration --
            //
            // WHO STEERS THIS CHARACTER (PR #2840 review). This drive and
            // DriveTravel both write botAI->rpgInfo, one every twenty seconds
            // and the other every fifteen, and left alone they take turns: a
            // travel aim makes the status something other than RPG_DO_QUEST,
            // so this drive re-issues ChangeToDoQuest; that makes it something
            // other than RPG_WANDER_NPC, so DriveTravel re-issues
            // ChangeToWanderNpc; each write resets the other errand's movement
            // state and the character goes nowhere while looking busy. The dev
            // roster carries a drive_quest on the leader, so this is the
            // ordinary case and not a corner.
            //
            // TRAVEL WINS WHILE AN ERRAND IS OUTSTANDING - and NOT because a
            // travel errand is "more explicit". `drive_quest` is exactly as
            // explicit: the same council decides it and the same bridge writes
            // it. The asymmetry that decides this is that travel is BOUNDED and
            // this drive is not. An errand ends on arrival, and at the latest
            // after TRAVEL_BACKSTOP_SECONDS; a quest aim is renewed by the
            // bridge for as long as the council keeps deciding, and when no aim
            // is set at all this drive still picks from the character's own log
            // forever. So "quest wins" starves travel with no bound - and then
            // the errand's own backstop releases it as "sent over twenty
            // minutes ago and never arrived" when it was never tried, which is
            // this epic's one unforgivable bug: a mechanism reporting something
            // it did not observe. "Travel wins" costs the quest aim a bounded
            // pause and makes no false statement.
            //
            // ONE DECISION IN ONE PLACE. TravelHoldsTheWheel is the whole of
            // the arbitration and it is asked here and nowhere else. DriveTravel
            // does not know this drive exists and must not learn: two functions
            // each deferring to the other is how the next oscillation gets
            // built.
            if (TravelHoldsTheWheel(name, travelTarget, botAI))
            {
                if (!state.travelHeld)
                {
                    state.travelHeld = std::time(nullptr);
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is on a travel errand ({}) - the quest "
                             "drive stands down until it lands", name,
                             travelTarget.empty() ? std::string("just released")
                                                  : travelTarget);
                }
                continue;
            }

            if (state.travelHeld)
            {
                // THE HAND-BACK. Time spent stood down is not time the aim
                // spent failing, so the backstop clock is carried forward over
                // the errand instead of running through it. Without this the
                // arbitration would rebuild the very bug this PR fixes on the
                // travel side: a quest aim held across a long errand released
                // as unreachable on the first poll after the hand-back, having
                // been given no chance to land.
                time_t const handedBack = std::time(nullptr);
                if (state.since)
                    state.since += handedBack - state.travelHeld;
                state.travelHeld = 0;
                LOG_INFO("module.overseer",
                         "overseer: '{}' is off its travel errand - the quest drive "
                         "picks up again", name);
            }

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


    // ---------------------------------------------------------------- travel --
    //
    // THE ONE VERB THE WHOLE EPIC WAS WAITING ON (infra#2783). Before this the
    // family could walk to a quest objective and nowhere else: no trainer, no
    // vendor, no repair, no bank, no guild-charter petitioner, no tabard
    // designer. Not because those NPCs were disallowed - mod-playerbots'
    // allowed-target list at PossibleRpgTargetsValue.cpp:23-46 is one
    // unconditional block and contains all of them - but because the only way
    // into RPG_WANDER_NPC took no argument (NewRpgInfo.h:104), so the bot always
    // chose its own NPC. One missing parameter blocked #2757, #2829, #2830,
    // #2831 and the `town run` and `train` modes of #2834 at once.
    //
    // WHAT IS DELIBERATELY NOT HERE: the transaction. Arriving is all this does.
    // Upstream's only interaction on arrival is its existing quest-giver branch
    // (NewRpgAction.cpp:398-399), so a character aimed at a trainer walks there
    // and stands in front of it, and learns nothing. Training, buying, repairing
    // and signing a charter are each their own issue - and each of them was
    // blocked on being unable to get there at all.

    // The keywords the roster may name, and the NPC flag each one matches.
    // DUPLICATED, ON PURPOSE AND UNAVOIDABLY, in
    // production/scripts/wow-overseer/travel.py: a compiled module and a Python
    // process share no schema. tests/test_travel_npc.py compares the two tables
    // in BOTH directions, because a keyword one side accepts and the other
    // silently ignores is the written-and-unread failure of #2776 wearing a new
    // hat.
    struct TravelRole
    {
        char const* keyword;
        uint32 npcFlag;
    };
    static std::vector<TravelRole> const& TravelRoles()
    {
        static std::vector<TravelRole> const roles = {
            {"trainer",            UNIT_NPC_FLAG_TRAINER},
            {"class trainer",      UNIT_NPC_FLAG_TRAINER_CLASS},
            {"profession trainer", UNIT_NPC_FLAG_TRAINER_PROFESSION},
            {"vendor",             UNIT_NPC_FLAG_VENDOR},
            {"repair",             UNIT_NPC_FLAG_REPAIR},
            {"banker",             UNIT_NPC_FLAG_BANKER},
            {"guild banker",       UNIT_NPC_FLAG_GUILD_BANKER},
            {"auctioneer",         UNIT_NPC_FLAG_AUCTIONEER},
            {"petitioner",         UNIT_NPC_FLAG_PETITIONER},
            {"tabard designer",    UNIT_NPC_FLAG_TABARDDESIGNER},
            {"innkeeper",          UNIT_NPC_FLAG_INNKEEPER},
            {"flight master",      UNIT_NPC_FLAG_FLIGHTMASTER},
            {"stable master",      UNIT_NPC_FLAG_STABLEMASTER},
        };
        return roles;
    }

    // Every flag any keyword can ask for, so the index can drop the ~99% of
    // spawns that are nothing anybody can be sent to.
    static uint32 TravelAnyFlag()
    {
        uint32 mask = 0;
        for (TravelRole const& role : TravelRoles())
            mask |= role.npcFlag;
        return mask;
    }

    // WHY AN INDEX AND NOT A QUERY, and why it is built here rather than in
    // Python. Three reasons, in order of how much they matter:
    //
    //   1. NEAREST DEPENDS ON WHERE THE CHARACTER IS STANDING, which changes as
    //      he walks and is known only inside the worldserver. A target chosen
    //      out of band is a target chosen for where he was, not where he is.
    //   2. NO SCHEMA GUESS. sObjectMgr already holds every creature spawn, and
    //      this reads it through exactly the members mod-playerbots itself
    //      reads at the pinned SHA - creatureData.id, .mapid, .posX/.posY/.posZ
    //      (TravelMgr.cpp:4648-4661) - so nothing here depends on a column name
    //      in acore_world.creature being what we remember it being. The C++
    //      only compiles on a push to main; a guessed member name costs the
    //      whole team forty-five minutes to find out about.
    //   3. IT IS A LOOP OVER A QUARTER OF A MILLION SPAWNS. Doing that per
    //      character per poll, on the world thread, to answer a question whose
    //      answer never changes, would be a real cost for no gain. Built once,
    //      lazily, on the first errand rather than at startup - a world where
    //      nobody is ever sent anywhere pays nothing.
    void BuildTravelIndex()
    {
        if (_travelIndexBuilt)
            return;
        _travelIndexBuilt = true;

        uint32 const wanted = TravelAnyFlag();
        for (auto const& itr : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& data = itr.second;
            CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(data.id);
            if (!creatureTemplate)
                continue;

            // THE SPAWN'S OWN FLAGS WIN WHERE IT HAS THEM. `creature.npcflag`
            // is a per-spawn override and 0 means "use the template", which is
            // the overwhelmingly common case (CreatureData.h:383 and :200). A
            // guard that read only the template would index a spawn that is
            // deliberately NOT a vendor as one, and send somebody to it.
            uint32 const npcFlags = data.npcflag ? data.npcflag : creatureTemplate->npcflag;
            if (!(npcFlags & wanted))
                continue;

            TravelSpawn spawn;
            spawn.entry = data.id;
            spawn.mapId = data.mapid;
            spawn.x = data.posX;
            spawn.y = data.posY;
            spawn.z = data.posZ;
            spawn.npcFlags = npcFlags;
            _travelSpawns.push_back(spawn);
        }

        LOG_INFO("module.overseer",
                 "overseer: travel index built - {} spawns the family can be sent to",
                 static_cast<uint32>(_travelSpawns.size()));
    }

    // Turn a roster value into a specific spawn near this character.
    //
    // SAME MAP ONLY, and that is a refusal rather than a limitation to fix
    // later. MoveFarTo paths through PathGenerator, and there is no navmesh
    // across an ocean or into an instance - the same reason patch 0003 keeps
    // upstream's MapId check while removing its zone check. A cross-continent
    // errand needs a boat, and a character that accepted one would fail far
    // more confusingly than one that refuses.
    // `wantSkill` narrows a TRAINER role to trainers that can actually start
    // this character in that primary profession (infra#2757). 0 means "no
    // opinion", which is every errand that is not a profession errand and is
    // byte for byte the behaviour this function has always had.
    bool ResolveTravelTarget(Player* bot, std::string const& target,
                             uint32& outEntry, WorldPosition& outPos,
                             uint32 wantSkill = 0)
    {
        // A PLACE, NOT A CREATURE: `at:<map>:<x>,<y>,<z>`. Answered before the
        // NPC index is even built, because no spawn is involved - the aim names
        // ground. This is what lets a party reach an instance portal, which is
        // an areatrigger and so has no entry to name; see the mod-playerbots
        // patch that lets an aimed wander carry a position with no entry.
        //
        // THE MAP IS PART OF THE AIM, and refusing when it does not match is
        // what ENDS the errand rather than a special case that has to know
        // about portals. Walk onto a portal and the trigger changes your map
        // mid-walk; on the next poll this returns false, and the caller's
        // existing release path clears the aim exactly as it would for a spawn
        // that is no longer there. Arriving and being teleported away are the
        // same observable event from here, and both mean the errand is over.
        if (target.rfind("at:", 0) == 0)
        {
            // Parsed with the stream the file already includes rather than
            // sscanf, which would need a header this translation unit does not
            // pull in - and the separators are checked rather than assumed, so
            // a malformed aim is refused instead of silently reaching a
            // half-parsed coordinate.
            std::istringstream in(target.substr(3));
            uint32 m = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            char c1 = 0, c2 = 0, c3 = 0;
            if (!(in >> m >> c1 >> x >> c2 >> y >> c3 >> z) ||
                c1 != ':' || c2 != ',' || c3 != ',')
                return false;
            if (!bot || bot->GetMapId() != m)
                return false;
            outEntry = 0;  // deliberately: the walk is the whole errand
            outPos = WorldPosition(m, x, y, z);
            return true;
        }

        // A PORTAL: `trigger:<areatrigger id>`. The same walk as `at:` above,
        // except the module knows on arrival that the destination is a doorway
        // and can knock on it.
        //
        // WHY THIS IS NOT JUST `at:` WITH THE PORTAL'S COORDINATES. It was,
        // and it got the family to the Deadmines portal and no further:
        // measured on the dev world, four of them stood 5, 7, 10 and 10 yards
        // from a trigger of radius 7 and nothing happened. Nothing was wrong
        // with the walk. An areatrigger fires from CMSG_AREATRIGGER, which the
        // game CLIENT sends when it notices it has touched one
        // (WorldSession::HandleAreaTriggerOpcode, MiscHandler.cpp:691, the only
        // caller of the teleport at :807). The server never sweeps player
        // positions against triggers, so a character with no client walks over
        // a portal and the portal never hears about it. Walking in "like a
        // player" turns out to depend on a part of the player that a bot
        // does not have.
        //
        // Upstream does simulate that packet - AreaTriggerAction.cpp builds one
        // and hands it to the same handler - but only on two paths, neither of
        // which a roster character is ever on: a HUMAN master walking through
        // first (PlayerbotAI.cpp:160 registers it as a master-incoming packet),
        // and the old TravelNode graph, which the NewRpg walk this module
        // steers does not use.
        //
        // The id is taken rather than coordinates because the trigger table is
        // then the single source of both WHERE to walk and WHAT to knock on,
        // and the two cannot drift apart.
        if (target.rfind("trigger:", 0) == 0)
        {
            std::istringstream in(target.substr(8));
            uint32 id = 0;
            if (!(in >> id) || !id)
                return false;
            AreaTrigger const* at = sObjectMgr->GetAreaTrigger(id);
            if (!at)
                return false;
            // Same reasoning as the map check above: once through, the trigger
            // is on a map this character has left, this refuses, and the
            // caller's existing release path ends the errand.
            if (!bot || bot->GetMapId() != at->map)
                return false;
            outEntry = 0;  // a doorway is not a creature
            outPos = WorldPosition(at->map, at->x, at->y, at->z);
            return true;
        }

        BuildTravelIndex();

        uint32 wantedEntry = 0;
        uint32 wantedFlag = 0;
        bool const numeric = !target.empty() &&
                             target.find_first_not_of("0123456789") == std::string::npos;
        if (numeric)
        {
            wantedEntry = static_cast<uint32>(std::strtoul(target.c_str(), nullptr, 10));
            if (!wantedEntry)
                return false;
        }
        else
        {
            for (TravelRole const& role : TravelRoles())
            {
                if (target == role.keyword)
                {
                    wantedFlag = role.npcFlag;
                    break;
                }
            }
            if (!wantedFlag)
                return false;
        }

        // ONLY A TRAINER ROLE IS NARROWED, and the restraint is deliberate. A
        // learn errand paired with, say, a vendor aim is somebody sending this
        // character somewhere else on purpose; silently finding no vendor that
        // teaches tailoring and reporting "no such spawn" would be a worse
        // answer than simply going to the vendor. A bare creature entry is left
        // alone for the same reason: naming one NPC is naming one NPC.
        bool const narrowToSkill =
            wantSkill && !wantedEntry &&
            (wantedFlag & (UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_TRAINER_PROFESSION));

        uint32 const mapId = bot->GetMapId();
        float bestDist = 0.f;
        bool found = false;
        for (TravelSpawn const& spawn : _travelSpawns)
        {
            if (spawn.mapId != mapId)
                continue;
            if (wantedEntry ? spawn.entry != wantedEntry : !(spawn.npcFlags & wantedFlag))
                continue;
            if (narrowToSkill && !TrainerStartedSkills(spawn.entry).count(wantSkill))
                continue;

            float const dist = bot->GetDistance2d(spawn.x, spawn.y);
            if (!found || dist < bestDist)
            {
                found = true;
                bestDist = dist;
                outEntry = spawn.entry;
                outPos = WorldPosition(spawn.mapId, spawn.x, spawn.y, spawn.z);
            }
        }
        return found;
    }

    // ----------------------------------------------------------- professions --
    //
    // THE TRANSACTION HALF (infra#2757). infra#2783 delivered TRAVEL: a
    // character can be aimed at a profession trainer and it walks there. Its
    // own note said what it was not - "it does not train, buy, repair or sign
    // anything" - and that missing verb is this section.
    //
    // WHAT WAS ALREADY DONE, AND IT IS MOST OF IT. The DECISION has existed and
    // been correct for days. professions.py holds the family's assignment with
    // the reasoning beside each row, professions.plan() opens one trade at a
    // time in a deliberate order, and `overseer_trade` had been carrying the
    // answer since 2026-08-26 02:15 -
    //
    //     Og  unlearn  alchemy   (171)  planned
    //     Og  learn    tailoring (197)  planned
    //
    // - two rows nothing in the worldserver could see, because `overseer_trade`
    // is a Python table and this module reads `overseer_roster`. Nothing here
    // decides anything. It executes a decision that was already made, and it
    // refuses to execute one the roster does not also declare.
    //
    // THE THREE THINGS THAT STOPPED IT, AND WHERE EACH IS ANSWERED.
    //
    //   1. A FOLLOWER CANNOT RUN AN ERRAND. Only `new rpg` reaches
    //      NewRpgWanderNpcAction, the family carry it on the leader alone, and
    //      giving a follower both `new rpg` and `follow` is the 937-yard
    //      scatter of infra#2812. NOT ANSWERED HERE, AND DELIBERATELY NOT: the
    //      answer is that an errand MOVES THE LEADERSHIP, so the character with
    //      the errand becomes the one traveller and the other four follow it.
    //      That is a decision about who leads, it is made in Python
    //      (bridge._head_now), and this module already implements it - the
    //      `lead` column moves, KeepRosterGrouped promotes, KeepRosterFollowing
    //      re-points the other four. The invariant this epic depends on is
    //      untouched: exactly one character carries `new rpg`, and it is the
    //      group leader. Nothing below hands `new rpg` to anybody.
    //
    //   2. NOBODY TRAINED ON ARRIVAL. TrainOnArrival is that verb, and it is
    //      built on the core's own Trainer::TeachSpell rather than on a second
    //      copy of it - see the note there.
    //
    //   3. BOTH PRIMARY SLOTS ARE FULL. UnlearnProfession is that verb, behind
    //      a price the requester has to have agreed to in advance.
    //
    // AND A FOURTH NOBODY HAD NAMED, WHICH WOULD HAVE MADE THE OTHER THREE
    // POINTLESS. See the guard in TrainRoster: the level-up sweep steals a
    // primary profession out of thin air the moment a slot is free, which is
    // where the alchemy all five hold actually came from.

    // What the roster says about one character's trades.
    struct ProfessionPlan
    {
        // The primary professions this character is MEANT to end up with. This
        // is the permission: nothing is learned that is not in here, and
        // nothing in here is ever unlearned. See the column comment in
        // 2026_08_26_00_overseer_roster_professions.sql.
        std::set<uint32> wanted;
        uint32 learnSkill = 0;
        uint32 unlearnSkill = 0;
        uint32 unlearnMax = 0;
    };

    // The DBC's own word for a skill line, for the logs. Read rather than
    // tabulated: a table of names in this file would be a third spelling of
    // something goals.SKILL_IDS and SkillLineStore already agree about, and the
    // only thing a third spelling can add is a way to disagree.
    static char const* SkillName(uint32 skill)
    {
        if (SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill))
            if (line->name[LOCALE_enUS])
                return line->name[LOCALE_enUS];
        return "an unnamed skill";
    }

    // Which primary profession, if any, this trainer spell would START somebody
    // in. 0 for everything else - a recipe, a rank-up, a class spell, a
    // secondary skill.
    //
    // TWO SHAPES, AND trainer_spell CONTAINS BOTH. A row may name the
    // profession spell itself, or a wrapper whose SPELL_EFFECT_LEARN_SPELL
    // effect names it. Trainer::GetSpellState walks the second shape for
    // exactly this reason (Trainer.cpp:176-188), so both are asked here, in the
    // same order and through the same members.
    //
    // IsPrimaryProfessionSkill is the core's own test - one lookup, one
    // category check (SpellMgr.cpp:38-48) - and it is what keeps cooking,
    // fishing and first aid out of this. Those cost no slot, all five already
    // hold them, and an errand for one would be an errand for nothing.
    static uint32 SkillStartedBySpell(uint32 spellId)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info)
            return 0;

        auto primaryOf = [](uint32 candidate) -> uint32
        {
            if (!candidate)
                return 0;
            SpellLearnSkillNode const* node = sSpellMgr->GetSpellLearnSkill(candidate);
            if (!node)
                return 0;
            return IsPrimaryProfessionSkill(node->skill) ? node->skill : 0;
        };

        if (uint32 const direct = primaryOf(spellId))
            return direct;

        for (SpellEffectInfo const& effect : info->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;
            if (uint32 const taught = primaryOf(effect.TriggerSpell))
                return taught;
        }
        return 0;
    }

    // Every primary profession this trainer ENTRY can start somebody in.
    //
    // THIS IS WHAT MAKES THE ERRAND LAND SOMEWHERE USEFUL, and its absence was
    // a blocker nobody had written down. `travel_npc` = 'profession trainer'
    // resolves to the nearest spawn carrying UNIT_NPC_FLAG_TRAINER_PROFESSION,
    // and that flag is worn by cooking instructors and fishing trainers as
    // cheerfully as by tailors. Aim a mage at it in Elwynn and he walks
    // confidently to a cook. Arriving would be logged, the errand would be
    // released as done, and he would learn nothing - travel that reports
    // success and delivers nothing, which is this epic's signature failure
    // wearing yet another hat.
    //
    // Keyed by ENTRY and not by spawn: the answer is a property of the
    // template, and every spawn of a trainer gives the same one.
    std::set<uint32> const& TrainerStartedSkills(uint32 entry)
    {
        auto const it = _trainerSkills.find(entry);
        if (it != _trainerSkills.end())
            return it->second;

        std::set<uint32>& skills = _trainerSkills[entry];
        if (Trainer::Trainer* trainer = sObjectMgr->GetTrainer(entry))
            for (Trainer::Spell const& spell : trainer->GetSpells())
                if (uint32 const skill = SkillStartedBySpell(spell.SpellId))
                    skills.insert(skill);
        return skills;
    }

    // The spell THIS trainer would sell THIS character to start THIS skill, or
    // 0 if there is no such spell or it is not currently available to them.
    //
    // CanTeachSpell is asked here rather than trusted later because it is the
    // one place the refusals are legible: it folds "already known", "too low a
    // level", "wrong race or class" and - the one that matters most here - "no
    // free primary profession slot" into a single answer (Trainer.cpp:134-150).
    static uint32 TrainerSpellForSkill(Trainer::Trainer* trainer, Player* bot, uint32 skill)
    {
        for (Trainer::Spell const& spell : trainer->GetSpells())
        {
            if (SkillStartedBySpell(spell.SpellId) != skill)
                continue;
            if (!trainer->CanTeachSpell(bot, &spell))
                continue;
            return spell.SpellId;
        }
        return 0;
    }

    // Every enabled character the roster has an OPINION about, name -> plan.
    //
    // READ ON ITS OWN, like LoadQuestAims and LoadTravelAims and for the same
    // reason: these four columns ship in the DB-IMPORT image and this reader
    // ships in the WORLDSERVER image, the two are pinned and bumped
    // independently, and a SELECT naming a column that does not exist fails
    // whole - error 1054, handed back as a null QueryResult that is
    // indistinguishable from "no rows matched". An empty map is the correct
    // degrade: no opinions, so nothing is learned and nothing is unlearned,
    // which is exactly the behaviour this module had yesterday.
    //
    // `professions <> ''` IS THE GATE, IN THE WHERE CLAUSE. A character with no
    // declared end state can produce no plan at all, so a stray `learn_skill`
    // on a row nobody has decided about is not merely refused later - it is
    // never even loaded.
    std::map<std::string, ProfessionPlan> LoadProfessionPlans()
    {
        std::map<std::string, ProfessionPlan> plans;
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, professions, learn_skill, unlearn_skill, unlearn_max "
            "FROM overseer_roster WHERE enabled = 1 AND professions <> ''");
        if (!result)
            return plans;  // nobody assigned, or no such columns - same answer

        do
        {
            Field* fields = result->Fetch();
            ProfessionPlan plan;

            // Comma-separated skill ids. Anything unparseable is 0 and is
            // dropped, so a malformed column narrows the permission rather than
            // widening it - which is the only direction a parse error may fail
            // in when the value being parsed is a permission.
            std::string const wanted = fields[1].Get<std::string>();
            for (size_t start = 0; start <= wanted.size();)
            {
                size_t const comma = wanted.find(',', start);
                size_t const len = comma == std::string::npos ? std::string::npos : comma - start;
                if (uint32 const id = static_cast<uint32>(
                        std::strtoul(wanted.substr(start, len).c_str(), nullptr, 10)))
                    plan.wanted.insert(id);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }

            plan.learnSkill = fields[2].Get<uint16>();
            plan.unlearnSkill = fields[3].Get<uint16>();
            plan.unlearnMax = fields[4].Get<uint16>();
            plans[fields[0].Get<std::string>()] = plan;
        } while (result->NextRow());
        return plans;
    }

    void ClearLearnAim(std::string const& name)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET learn_skill = 0 WHERE name = '{}'", Esc(name));
    }

    // The price dies with the request, for the same reason ClearTravelAim
    // erases the clock: a leftover `unlearn_max` would be inherited by the NEXT
    // request against the same character, and the next one might be for a skill
    // worth a great deal more than this one was.
    void ClearUnlearnRequest(std::string const& name)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET unlearn_skill = 0, unlearn_max = 0 WHERE name = '{}'",
            Esc(name));
        _unlearnRefused.erase(name);
    }

    // Give up a profession, on purpose, out loud, and never as a side effect.
    //
    // WHY THIS IS ITS OWN VERB AND NOT A STEP INSIDE "LEARN". Making room is
    // the obvious thing to fold into learning - the character needs a slot, so
    // free one - and folding it in is precisely how a family loses Grug's 41
    // points of herbalism to a line of code nobody read. Destroying work is not
    // a detail of acquiring work. It is asked for separately, priced
    // separately, logged separately, and recorded as its own event.
    //
    // WHAT IT ACTUALLY DOES IS ONE LINE, AND IT IS THE CLIENT'S OWN LINE.
    // Unlearning a profession in 3.3.5 is a spellbook action, not a trainer
    // one: the client sends CMSG_UNLEARN_SKILL and
    // WorldSession::HandleUnlearnSkillOpcode does exactly this, behind exactly
    // this guard (SkillHandler.cpp:91-100). Player::SetSkill with a zero value
    // then clears the skill fields and calls removeSpell on every ability
    // hanging off the line (Player.cpp:5523-5539), and removeSpell hands the
    // primary profession point back (Player.cpp:3561-3567) - which is the slot
    // this whole errand exists to open. So no part of this is a bespoke
    // mechanism; it is the same code path a person clicking the button walks.
    void UnlearnProfession(std::string const& name, Player* bot, ProfessionPlan const& plan)
    {
        uint32 const skill = plan.unlearnSkill;
        char const* const skillName = SkillName(skill);

        // THE THREE REFUSALS THAT CAN NEVER BECOME RIGHT clear the request, so
        // that a bad row is said once rather than every thirty seconds forever.
        // A log line repeated two thousand times a day is not a louder warning,
        // it is a quieter one.
        if (!IsPrimaryProfessionSkill(skill))
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' was asked to give up skill {} ({}), which is not a "
                     "primary profession - only those cost a slot, so there is nothing "
                     "here to make room. Dropping the request", name, skill, skillName);
            ClearUnlearnRequest(name);
            return;
        }

        // THE GATE. The roster's declared end state outranks the instruction,
        // always and in this direction only. Ugga is assigned herbalism and
        // alchemy and already holds both; this is what makes a stray request
        // against her harmless without anybody having remembered her.
        if (plan.wanted.count(skill))
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' was asked to give up {} ({}), which the roster also "
                     "says it should END UP with. The declared end state wins - dropping "
                     "the request rather than the profession", name, skillName, skill);
            ClearUnlearnRequest(name);
            return;
        }

        if (!bot->HasSkill(skill))
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' was asked to give up {} ({}) and does not have it - "
                     "already true, so the request is done", name, skillName, skill);
            ClearUnlearnRequest(name);
            return;
        }

        // THE PRICE, AND THE ONE REFUSAL THAT IS LEFT STANDING. A requester
        // whose picture of the world is stale should get a refusal it can
        // answer by raising the price, not one that erases the request and
        // makes the disagreement disappear. Said once per (character, skill)
        // because it is re-evaluated every poll and would otherwise be the
        // loudest line in the log while being the least urgent.
        uint16 const value = bot->GetPureSkillValue(skill);
        if (value > plan.unlearnMax)
        {
            auto const said = _unlearnRefused.find(name);
            if (said == _unlearnRefused.end() || said->second != skill)
            {
                _unlearnRefused[name] = skill;
                LOG_WARN("module.overseer",
                         "overseer: REFUSING to take {} ({}) off '{}'. It stands at {} and "
                         "the request only agreed to destroy {}. Nothing has been lost. "
                         "Raise unlearn_max to {} if that loss is really intended",
                         skillName, skill, name, static_cast<uint32>(value),
                         plan.unlearnMax, static_cast<uint32>(value));
            }
            return;
        }

        LOG_WARN("module.overseer",
                 "overseer: '{}' is GIVING UP {} ({}) at {}/{} - deliberately, because the "
                 "roster asked for it and agreed to the cost. This destroys those {} points "
                 "and every recipe hanging off them, and there is no undo",
                 name, skillName, skill, static_cast<uint32>(value),
                 static_cast<uint32>(bot->GetPureMaxSkillValue(skill)),
                 static_cast<uint32>(value));

        bot->SetSkill(static_cast<uint16>(skill), 0, 0, 0);  // Player.h:2111

        // READ BACK, NOT ASSUMED. "delivered is not worked" is the rule this
        // whole epic was built out of, and a verb that logs its own success
        // without looking is the thing that rule exists to stop.
        if (bot->HasSkill(skill))
        {
            LOG_ERROR("module.overseer",
                      "overseer: '{}' still has {} ({}) after SetSkill cleared it - the "
                      "unlearn did not take. Leaving the request standing", name,
                      skillName, skill);
            return;
        }

        LOG_INFO("module.overseer",
                 "overseer: '{}' no longer has {} ({}) and now holds {} free primary "
                 "profession slot(s)", name, skillName, skill,
                 bot->GetFreePrimaryProfessionPoints());
        RecordEvent(bot, "unlearn", skill, skillName,
                    "gave up this profession to make room for the one the family assigned");
        ClearUnlearnRequest(name);
    }

    // Learn the assigned trade from the trainer this character is standing at.
    //
    // Returns TRUE when the errand is finished with - learned, already true, or
    // impossible for a reason that will not change - and FALSE when the
    // character should stay put and let the next poll try again. A false is not
    // an error path: the commonest one is "the slot is still full", which the
    // unlearn drive clears a few seconds later.
    //
    // WHY Trainer::TeachSpell AND NOT A COPY OF IT. mod-playerbots has a copy -
    // TrainerAction::Iterate reimplements the loop and calls CastSpell or
    // learnSpell itself (TrainerAction.cpp:92-137) - and this module could have
    // a third. It must not, for two reasons. The first is that the copy is
    // wrong for this job: TrainerAction learns EVERYTHING the trainer offers,
    // which at a profession trainer means whatever primary it happens to reach
    // first, and this errand is about ONE named trade. The second is that
    // TeachSpell is where the money is taken, the free-slot rule is enforced
    // and the client is told (Trainer.cpp:81-118). Reimplementing it is how a
    // profession appears in `character_skills` without a trainer having been
    // paid or visited, which is infra#2782 and is the thing professions.py
    // refuses to do by design.
    //
    // NO SELECTION PROBLEM, WHICH professions.py LISTED AS A BLOCKER. That note
    // was about TrainerAction, which reads the MASTER's selected unit and so
    // could never be driven for a family that has one (TrainerAction.cpp:75-82).
    // Going under it to the core's own Trainer object makes selection
    // irrelevant: the creature is passed in, because this module already knows
    // which one it sent the character to.
    bool TrainOnArrival(std::string const& name, Player* bot, uint32 entry,
                        ProfessionPlan const& plan)
    {
        uint32 const skill = plan.learnSkill;
        if (!skill)
            return true;   // arriving WAS the whole errand

        char const* const skillName = SkillName(skill);

        if (!plan.wanted.count(skill))
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' was sent to learn {} ({}), which the roster does not "
                     "say it should hold. Refusing and dropping the errand - the declared "
                     "end state is the only permission there is", name, skillName, skill);
            ClearLearnAim(name);
            return true;
        }

        if (bot->HasSkill(skill))
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' already has {} ({}) - nothing to learn", name,
                     skillName, skill);
            ClearLearnAim(name);
            return true;
        }

        // BOTH SLOTS STILL FULL. Not an error and not a refusal - it is the
        // errand arriving in the wrong order, and the unlearn drive runs on its
        // own poll. Staying put is the right answer: walking away from the
        // trainer and coming back is a five-minute round trip for a condition
        // that clears in thirty seconds.
        if (!bot->GetFreePrimaryProfessionPoints())
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' is at the trainer for {} ({}) but holds two primary "
                     "professions already, so there is no slot to put it in. Waiting here "
                     "for the unlearn", name, skillName, skill);
            return false;
        }

        // Resolved now rather than carried: DriveTravel measures arrival
        // against the recorded SPAWN POINT, which is a position and not a
        // creature. TRAVEL_ARRIVED_YARDS is the radius this module already
        // calls "there", so it is the radius the creature is looked for in
        // rather than a second, differently-argued number.
        Creature* npc = bot->FindNearestCreature(entry, TRAVEL_ARRIVED_YARDS);
        if (!npc || !npc->IsAlive())
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' is standing on the spawn point of creature {} and it "
                     "is not there - despawned, phased or dead. Cannot learn {} here yet",
                     name, entry, skillName);
            return false;
        }

        Trainer::Trainer* trainer = sObjectMgr->GetTrainer(entry);
        if (!trainer || !trainer->IsTrainerValidForPlayer(bot))
        {
            LOG_WARN("module.overseer",
                     "overseer: creature {} ('{}') will not train '{}' - it is either not a "
                     "trainer at all or not one this character may use. Dropping the errand",
                     entry, npc->GetName(), name);
            ClearLearnAim(name);
            return true;
        }

        uint32 const spellId = TrainerSpellForSkill(trainer, bot, skill);
        if (!spellId)
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' reached '{}' (creature {}) and it cannot teach {} ({}) "
                     "to this character. Dropping the errand so a fresh one can pick a "
                     "trainer that can", name, npc->GetName(), entry, skillName, skill);
            ClearLearnAim(name);
            return true;
        }

        LOG_INFO("module.overseer",
                 "overseer: '{}' is buying {} ({}) from '{}' (creature {}, spell {})",
                 name, skillName, skill, npc->GetName(), entry, spellId);

        trainer->TeachSpell(npc, bot, spellId);  // Trainer.h:73

        // THE READ-BACK, and it is the whole point of the exercise. TeachSpell
        // returns void and reports its failures to the CLIENT - a packet no bot
        // has anybody to show it to (Trainer.cpp:88-108). So the only way to
        // know whether a character learned a trade is to ask the character. A
        // `delivered` here would mean nothing at all.
        if (!bot->HasSkill(skill))
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' was not taught {} ({}) by '{}'. TeachSpell reports its "
                     "reason only to a client, so the likely ones are money - it holds {} "
                     "copper - or a slot that closed between the check and the sale. "
                     "Leaving the errand standing for the next poll",
                     name, skillName, skill, npc->GetName(), bot->GetMoney());
            return false;
        }

        LOG_INFO("module.overseer",
                 "overseer: '{}' LEARNED {} ({}) at {}/{} from '{}' - taught by a trainer it "
                 "was sent to, not granted out of thin air",
                 name, skillName, skill,
                 static_cast<uint32>(bot->GetPureSkillValue(skill)),
                 static_cast<uint32>(bot->GetPureMaxSkillValue(skill)), npc->GetName());
        RecordEvent(bot, "learn", skill, skillName,
                    "learned this profession from a trainer it was sent to");
        ClearLearnAim(name);
        return true;
    }

    // The unlearn drive. Its own poll, because unlearning needs no NPC and no
    // journey: it is the prerequisite that makes the journey worth taking, and
    // making it wait for one would deadlock the pair - the character cannot
    // learn until a slot is free, and the slot would not free until it arrived.
    void DriveProfessions()
    {
        std::map<std::string, ProfessionPlan> const plans = LoadProfessionPlans();
        for (auto const& [name, plan] : plans)
        {
            if (!plan.unlearnSkill)
                continue;

            // SteerableAI for the same reason every other drive uses it: a name
            // resolves to a Player that is mid-login or mid-teardown several
            // times an hour while POV streaming runs, and this one writes to
            // the character's skills.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(bot))
                continue;

            // The same stand-down every steering drive now shares. Giving up a
            // trade is a decision about what a character does in the WORLD, and
            // there is no trainer inside an instance to make it good on - so
            // this belongs to the run, not to the profession plan, exactly as
            // the quest aim does.
            if (InDungeonRun(bot))
            {
                LOG_DEBUG("module.overseer",
                          "overseer: '{}' is in a dungeon run - the profession "
                          "drive stands down", name);
                continue;
            }

            UnlearnProfession(name, bot, plan);
        }
    }

    // Only `new rpg` walks a character to an NPC: it owns the `wander npc
    // status` trigger (NewRpgStrategy.cpp), and NewRpgWanderNpcAction is
    // reachable through nothing else. The family carry it on the LEADER alone,
    // because five characters each free-roaming is the 937-yard scatter that
    // taking it off the followers cured - so a follower cannot be sent
    // anywhere, and travels by following. HasStrategy is PlayerbotAI.h:416.
    //
    // Named once because two callers need the same answer: DriveTravel, which
    // refuses such an errand out loud, and the arbitration below, which must
    // NOT stand the quest drive down for an errand that will never move anyone.
    bool CanBeSentToNpc(PlayerbotAI* botAI)
    {
        return botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);
    }

    // THE ARBITRATION between this drive and DriveQuests, in full. DriveQuests
    // is the only caller and carries the argument for the direction; what is
    // here is the two ways travel holds the wheel.
    bool TravelHoldsTheWheel(std::string const& name, std::string const& travelTarget,
                             PlayerbotAI* botAI)
    {
        // 1. AN ERRAND IS OUTSTANDING. Not merely "the column is set": an aim
        //    on a character that cannot act on it moves nobody, and standing
        //    the quest drive down for one would quietly take an unaimable
        //    follower out of the family's questing until somebody noticed the
        //    column. DriveTravel leaves such a row set on purpose and says so.
        if (!travelTarget.empty() && CanBeSentToNpc(botAI))
            return true;

        // 2. THE ERRAND HAS JUST ENDED, and the hand-back must not land on the
        //    tick that ended it. DriveQuests runs BEFORE DriveTravel inside one
        //    OnUpdate, so without this the poll that releases an arrived errand
        //    is followed by a ChangeToDoQuest that wipes the WanderNpc state the
        //    character is still standing in - before upstream has finished the
        //    eight seconds at the NPC that count as having got there
        //    (npcStayTime, NewRpgAction.h:99). That is the oscillation above in
        //    miniature: it happens once and then stops, which makes it harder
        //    to see rather than less real.
        //
        //    A grace keyed by name and swept when it lapses is the shape the
        //    repick memory's give-up set already uses (infra#2801): a refusal
        //    that outlives its reason is its own bug.
        auto const it = _travelHandback.find(name);
        if (it == _travelHandback.end())
            return false;
        if (std::time(nullptr) - it->second < TRAVEL_HANDBACK_SECONDS)
            return true;
        _travelHandback.erase(it);
        return false;
    }

    // KNOCK ON THE DOOR THE CHARACTER HAS WALKED TO. Returns true ONLY if it
    // actually went through, because that is the only thing the caller may read
    // as the errand being finished.
    //
    // THIS IS NOT A TELEPORT THE MODULE PERFORMS, and the difference is the
    // whole reason it is allowed to exist. It sends the packet a game client
    // would have sent on touching the trigger, to the handler that would have
    // received it - the same two lines upstream's own AreaTriggerAction.cpp
    // uses. That handler re-checks IsInAreaTriggerRadius (MiscHandler.cpp:716)
    // and refuses a character not genuinely standing in the trigger, so nothing
    // here can move a character that has not walked to the door on its own legs.
    // The check is the server's, not ours, which is what makes it trustworthy.
    bool StepThroughAreaTrigger(std::string const& name, Player* bot,
                                std::string const& target)
    {
        if (target.rfind("trigger:", 0) != 0)
            return false;

        std::istringstream in(target.substr(8));
        uint32 id = 0;
        if (!(in >> id) || !id)
            return false;

        // A trigger with no teleport attached is a place, not a doorway. There
        // is nothing to step through and the walk was the whole errand, so this
        // says so rather than claiming a crossing that did not happen.
        if (!sObjectMgr->GetAreaTriggerTeleport(id))
            return false;

        uint32 const before = bot->GetMapId();

        WorldPacket packet(CMSG_AREATRIGGER);
        packet << id;
        packet.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(packet);

        if (bot->GetMapId() == before)
            // Refused, and the ordinary reason is the last few yards. The
            // handler's own IsInAreaTriggerRadius is the authority and it is
            // stricter than arrival: a `trigger:` aim carries no creature, so
            // it arrives on TRAVEL_ARRIVED_POSITION_YARDS (5) against the
            // Deadmines portal's radius of 7 - inside it, but a trigger may be
            // tighter still, and a path may stop short of where it aimed. Not
            // an error, and NOT the end of the errand. The caller keeps walking
            // and knocks again next poll.
            return false;

        LOG_INFO("module.overseer",
                 "overseer: '{}' walked into area trigger {} and is now on map {}",
                 name, id, static_cast<uint32>(bot->GetMapId()));
        return true;
    }

    // Give the errand back. THE ONE TERMINAL PATH - every release in DriveTravel
    // goes through here, which is why it does three things and not one.
    //
    // Esc() rather than a bare interpolation for the same reason every other
    // write in this file uses it: the name came out of a table a person edits.
    void ClearTravelAim(std::string const& name)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET travel_npc = '' WHERE name = '{}'", Esc(name));
        // THE CLOCK DIES WITH THE ERRAND (PR #2840 review). `since` is the
        // twenty-minute backstop, and a state entry outliving its errand is
        // inherited by the NEXT errand at the same target - which is then
        // released as unreachable on its first poll, having walked nowhere.
        _travelState.erase(name);
        // And the quest drive keeps its hands off for a moment: see
        // TravelHoldsTheWheel, case 2.
        _travelHandback[name] = std::time(nullptr);
    }

    // An errand can also end without this loop touching it: the bridge owns the
    // column and clears it when it re-aims the family. Such a row simply stops
    // coming back from the query, so nothing INSIDE the loop can see it go, and
    // its state entry would sit there with a running clock waiting to poison the
    // next errand. Comparing the map against the rows that did come back is the
    // only place that absence is visible.
    void PruneTravelState(std::set<std::string> const& stillAimed)
    {
        for (auto it = _travelState.begin(); it != _travelState.end();)
        {
            if (stillAimed.count(it->first))
            {
                ++it;
                continue;
            }
            // The character may be mid-walk under an aim nobody is renewing any
            // more, so this is a release like any other and takes the same grace.
            _travelHandback[it->first] = std::time(nullptr);
            it = _travelState.erase(it);
        }
    }

    void DriveTravel()
    {
        // THE SAME READ THE QUEST DRIVE'S ARBITRATION USES (infra#2846), so the
        // two can never be looking at different answers to "who is on an
        // errand". The WHERE clause that used to live here lives in the loader:
        // an empty target never reaches this loop, exactly as before.
        std::map<std::string, std::string> const aims = LoadTravelAims();
        // Read once for the whole sweep rather than per character: the errands
        // and the trades are the same handful of rows, and this is the read
        // that decides both WHERE a character is sent and what it does when it
        // gets there. An empty map - nobody assigned, or the columns are not
        // deployed yet - degrades this loop to exactly what it did before:
        // travel, and no transaction.
        std::map<std::string, ProfessionPlan> const plans = LoadProfessionPlans();
        if (aims.empty())
        {
            // No errands anywhere, so nothing this loop remembers is still
            // true. A schema with no `travel_npc` lands here too, which is the
            // correct degrade for this drive and always was: no column, no
            // errands, nobody sent anywhere.
            PruneTravelState(std::set<std::string>());
            return;
        }

        std::set<std::string> stillAimed;

        for (auto const& [name, target] : aims)
        {
            // Recorded before any refusal below, because the ROW still exists:
            // the prune is about rows that vanished, not about characters this
            // loop declined to move.
            stillAimed.insert(name);

            // SteerableAI, not a bare lookup: a name can resolve to a
            // Player that is mid-login or mid-teardown, and its AI pointer is
            // non-null right up until it is freed. See SteerableAI above.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            PlayerbotAI* botAI = SteerableAI(bot);
            if (!botAI)
                continue;

            auto const planIt = plans.find(name);
            ProfessionPlan const* const plan =
                planIt == plans.end() ? nullptr : &planIt->second;
            uint32 const wantSkill = plan ? plan->learnSkill : 0;

            TravelState& state = _travelState[name];
            if (state.target != target || state.learn != wantSkill)
            {
                state.target = target;
                state.learn = wantSkill;
                state.since = std::time(nullptr);
                state.entry = 0;
                state.pinned = false;
                state.arrived = false;
                state.closest = 0.f;
            }

            // AN AIM ON A CHARACTER THAT CANNOT ACT ON IT IS NOT AN AIM, and
            // this is the one failure that would look exactly like success from
            // the outside: the column is set, the module read it, and nothing
            // moves. Only `new rpg` runs NewRpgWanderNpcAction - it is the
            // strategy that owns the `wander npc status` trigger
            // (NewRpgStrategy.cpp) - and the family deliberately carry it on the
            // LEADER ALONE, because five characters each free-roaming is the
            // 937-yard scatter that taking it off the followers cured. So a
            // follower cannot be sent anywhere; it arrives by following. Say so
            // once per errand rather than renewing an aim nothing will read.
            // CanBeSentToNpc is the shared predicate the arbitration also
            // asks, so this refusal and that decision can never disagree.
            if (!CanBeSentToNpc(botAI))
            {
                if (!state.arrived)
                {
                    state.arrived = true;  // "said already", not "got there"
                    LOG_INFO("module.overseer",
                             "overseer: '{}' was sent to '{}' but does not carry `new rpg` - "
                             "nothing walks it anywhere. Followers travel by following the "
                             "leader; aim the leader instead", name, target);
                }
                continue;
            }

            // THE RESOLVED SPAWN IS PINNED FOR THE LIFE OF THE ERRAND (PR
            // #2840 review). ResolveTravelTarget picks the nearest spawn of the
            // role FROM WHERE THE CHARACTER NOW STANDS, so walking toward one
            // trainer can bring a different one nearer and hand back a new entry
            // mid-walk - which re-issues the aim, resets `lastReach` and
            // `startT`, and prints "sent to" a second time. An errand means one
            // NPC, so it is chosen once. A map change drops the pin, because a
            // spawn on a map the character has left is not reachable from here
            // and ResolveTravelTarget would refuse it anyway.
            uint32 entry = 0;
            WorldPosition pos;
            if (state.pinned && state.mapId == bot->GetMapId())
            {
                entry = state.entry;
                pos = WorldPosition(state.mapId, state.x, state.y, state.z);
            }
            else if (!ResolveTravelTarget(bot, target, entry, pos, wantSkill))
            {
                // Said unconditionally: the errand is released on this line and
                // its state erased with it, so there is no second poll of this
                // errand to repeat it.
                LOG_INFO("module.overseer",
                         "overseer: '{}' was sent to '{}' and there is no such spawn on "
                         "map {} - releasing the errand", name, target,
                         static_cast<uint32>(bot->GetMapId()));
                ClearTravelAim(name);
                continue;
            }

            // ARRIVAL IS THE PRIMARY RELEASE, measured against the spawn point
            // rather than read out of the bot's own state. Upstream clears
            // `npcOrGo` eight seconds after reaching an NPC (npcStayTime,
            // NewRpgAction.h:99), so the bot's state stops naming the target
            // almost immediately and cannot be asked "did you get there".
            // Distance can be, and it is the thing the errand actually means.
            float const distance =
                bot->GetDistance2d(pos.GetPositionX(), pos.GetPositionY());
            // `entry` is zero for an `at:<map>:<x>,<y>,<z>` aim and non-zero
            // for a creature, which is the whole distinction the two
            // tolerances are about - see TRAVEL_ARRIVED_POSITION_YARDS.
            float const arriveWithin =
                entry ? TRAVEL_ARRIVED_YARDS : TRAVEL_ARRIVED_POSITION_YARDS;
            if (distance <= arriveWithin)
            {
                // A DOORWAY IS ANSWERED FIRST, because for a `trigger:` aim
                // going through IS the errand and everything below is about
                // errands that end where the character is standing.
                bool const doorway = target.rfind("trigger:", 0) == 0;
                if (doorway && StepThroughAreaTrigger(name, bot, target))
                {
                    ClearTravelAim(name);
                    continue;
                }

                // ARRIVING IS NO LONGER THE WHOLE ERRAND (infra#2757). Where
                // the roster has asked this character to learn a trade, the
                // errand is finished when it has been LEARNED - or when it is
                // certain it cannot be here. TrainOnArrival answers exactly
                // that question and nothing else; a character with no learn
                // aim gets `true` on its first line and this reads as it
                // always did.
                if (doorway)
                {
                    // INSIDE ARRIVAL RANGE BUT NOT THROUGH THE DOOR, which for
                    // a portal is the normal last step rather than a fault: the
                    // arrival radius is wider than the trigger's. Deliberately
                    // NOT released and deliberately not `continue`, so the walk
                    // below carries the character the last few yards in. The
                    // backstop still bounds it, and now bounds the right thing:
                    // a character that has stopped getting nearer to a door it
                    // cannot open is exactly what it is for.
                }
                else if (plan && !TrainOnArrival(name, bot, entry, *plan))
                {
                    // NOT RELEASED, AND DELIBERATELY NOT `continue`. Falling
                    // through reaches the re-issue guard below, which renews
                    // the WANDER_NPC lease - so a character waiting for its
                    // slot to free keeps standing at the trainer instead of
                    // going IDLE when upstream's five-minute clock lapses
                    // (statusWanderNpcDuration, NewRpgAction.h:65). The
                    // twenty-minute backstop immediately below is what stops
                    // this being forever, and it is reached on this path.
                    //
                    // SAID ONCE PER ERRAND, not once per poll. `arrived` is
                    // already this state's "said already" flag and it is reset
                    // whenever the errand changes, so it carries exactly the
                    // meaning wanted here. TrainOnArrival says WHY it is not
                    // finished on its own line; repeating that the character is
                    // standing still every fifteen seconds would bury it.
                    if (!state.arrived)
                    {
                        state.arrived = true;
                        LOG_INFO("module.overseer",
                                 "overseer: '{}' has reached '{}' (creature {}) and is "
                                 "holding position there - the errand is not finished yet",
                                 name, target, entry);
                    }
                }
                else
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' reached '{}' (creature {}) - errand done, "
                             "releasing", name, target, entry);
                    ClearTravelAim(name);
                    continue;
                }
            }

            // PROGRESS RESTARTS THE BACKSTOP'S CLOCK (#63). The backstop below
            // exists to catch a character STANDING STILL, and it used to
            // approximate that as "taking a while" - which is a different thing,
            // and wrong in the one case that matters most. Measured on the dev
            // world: a character aimed at the Deadmines portal from Elwynn was
            // released 586 yards short, having walked 2347 of 2933 yards at 112
            // yards a minute, about five minutes from arriving. It was released
            // as UNREACHABLE while it was visibly reaching it, and the log said
            // so in those words. Nothing was stuck; the journey was simply
            // longer than a constant that had only ever been asked about
            // trainers in the same city.
            //
            // So ask the question the backstop is actually for. `closest` only
            // ever ratchets DOWNWARD, so beating it means the character has got
            // nearer than it has ever been on this errand - which no bot jammed
            // against scenery, circling, or standing in a field can keep doing,
            // and which a walking bot does on every poll. A target that truly
            // cannot be reached still gets released: the character closes to
            // whatever range it can manage, stops improving, and the clock then
            // runs out undisturbed. The bound is on being stuck, where it
            // belongs, rather than on distance.
            if (!state.closest || distance < state.closest - TRAVEL_PROGRESS_YARDS)
            {
                state.closest = distance;
                state.since = std::time(nullptr);
            }

            // BACKSTOP. An errand that can never land must not pin a character
            // to it forever - that is a character standing still, which is the
            // state this whole epic exists to stop mistaking for a working one.
            // Read with the progress check above: "since" now means since the
            // character last got nearer, not since the errand was issued.
            if (state.since && std::time(nullptr) - state.since > TRAVEL_BACKSTOP_SECONDS)
            {
                LOG_INFO("module.overseer",
                         // "releasing the errand as unreachable" is kept on ONE
                         // source line on purpose: infra's guard test greps this
                         // function for the phrase to prove every release path
                         // says why it fired, and a phrase split across two
                         // string literals is invisible to it.
                         "overseer: '{}' was sent to '{}' and has not got any nearer than "
                         "{} yards for {} minutes - releasing the errand as unreachable",
                         name, target, static_cast<uint32>(state.closest),
                         static_cast<uint32>(TRAVEL_BACKSTOP_SECONDS / 60));
                ClearTravelAim(name);
                continue;
            }

            // THE RE-ISSUE GUARD, and the #2799 lesson in the shape this status
            // needs it. Three cases have to come apart, and only one of them
            // wants a write:
            //
            //   1. ALREADY WALKING TO THIS NPC -> do nothing. The common case,
            //      and the one that breaks loudly if it is got wrong:
            //      ChangeToWanderNpc rebuilds the variant, which resets
            //      `lastReach` (so a character standing at the trainer never
            //      finishes the eight seconds that count as having arrived) and
            //      resets `startT` (so the five-minute lease never expires and
            //      the errand loses its only bound). Restarting the walk on
            //      every poll is also precisely what MoveFarTo's own comment
            //      warns about - a bot that oscillates instead of arriving.
            //   2. A DIFFERENT NPC -> re-issue. `npcEntry` names the target, so
            //      a changed aim is simply a changed entry.
            //   3. THE AIM WAS DROPPED and the errand is still outstanding ->
            //      re-issue the same target deliberately. Upstream drops it by
            //      letting the five-minute lease lapse to IDLE, at which point
            //      the status is no longer RPG_WANDER_NPC and the test below
            //      falls through to the write.
            //
            // WHAT MAKES CASE 3 TELLABLE AT ALL. For the quest aim, "the target
            // is empty" meant both "never aimed" and "arrived, and it cleared".
            // Here it cannot: the eight-second consume in
            // NewRpgWanderNpcAction clears `npcOrGo` and NEVER `npcEntry`, and
            // patch 0005 re-resolves `npcEntry` on the next tick. Two
            // alternatives were rejected. Reading `lastReach` cannot answer it,
            // because `lastReach` is reset by the very re-issue being decided
            // about. Keeping the answer in module-side state cannot either,
            // because a worldserver restart or a relog loses it and an errand
            // has to survive both. A separate, never-consumed field makes the
            // state SAY which case it is instead of leaving it to be inferred.
            if (botAI->rpgInfo.GetStatus() == RPG_WANDER_NPC)  // NewRpgInfo.h:99
            {
                if (NewRpgInfo::WanderNpc const* wander =
                        std::get_if<NewRpgInfo::WanderNpc>(&botAI->rpgInfo.data))
                {
                    if (wander->npcEntry == entry)
                        continue;
                }
            }

            // patches/mod-playerbots/0005-wander-npc-can-be-aimed.patch adds this
            // overload. The no-argument ChangeToWanderNpc() upstream ships takes
            // no target, which is the entire reason this feature did not exist.
            botAI->rpgInfo.ChangeToWanderNpc(entry, pos);

            // Pinned to what it was actually SENT to, not to what was
            // resolved: a resolve that never reached ChangeToWanderNpc is not
            // this errand's target. The same test gates the log, so "sent to" is
            // printed once per errand rather than once per poll.
            if (!state.pinned || state.entry != entry)
            {
                state.pinned = true;
                state.entry = entry;
                // The spawn is on the character's own map by construction:
                // ResolveTravelTarget refuses every other one.
                state.mapId = bot->GetMapId();
                state.x = pos.GetPositionX();
                state.y = pos.GetPositionY();
                state.z = pos.GetPositionZ();
                LOG_INFO("module.overseer",
                         "overseer: '{}' sent to '{}' - creature {} at {:.0f} yards",
                         name, target, entry,
                         bot->GetDistance2d(pos.GetPositionX(), pos.GetPositionY()));
            }
        }

        PruneTravelState(stillAimed);
    }

    // ---------------------------------------------------------- engagement --
    //
    // WHAT WAS WATCHED. The family wiped as a group in Burning Steppes
    // (level 45-55) against a named elite and its trash - a group's own bad
    // call, and out of scope here. What happened minutes later was not a
    // group decision at all: Grug revived alone, with no group, no quest aim
    // and no travel aim pointing him anywhere, and walked straight at a
    // `??`-conned dragonkin and died again. His combat engine still carried
    // `pull`, `tank` and `tank assist`, correctly - he is the family's
    // protection warrior and those are exactly the strategies that job needs
    // WHILE GROUPED AND TANKING CONTENT THE GROUP CHOSE. Nothing turned them
    // off when the group and the choosing both stopped being true.
    //
    // THE GATE. A character counts as having a reason to be somewhere when
    // either the council gave it one - `drive_quest` or `travel_npc`, read
    // through the SAME guarded loaders DriveQuests and DriveTravel already
    // use, so a late-added column degrades this drive exactly the way it
    // degrades theirs (infra#2846) rather than in some new way of its own -
    // or it is actually accompanied: grouped, with at least one OTHER roster
    // member in that same group and in the world right now. Not merely
    // holding a Group* - KeepRosterGrouped keeps this family in one party
    // long after a wipe scatters who is actually online, which is precisely
    // the gap that was watched: Grug's own group membership almost certainly
    // survived the wipe intact. Nothing had gone anywhere.
    //
    // A character with neither is stripped of the strategies that go looking
    // for a NEW fight - `pull`, `aoe`, `grind` - through the exact mechanism
    // DeliverPendingCommands already uses to grant them: ChangeStrategy on
    // BOT_STATE_COMBAT, read back with the same StrategyPresent this file
    // already uses to verify a whispered command. This is not a parallel
    // system; it is the same one, run the other direction.
    //
    // WHAT THIS DELIBERATELY DOES NOT TOUCH. `tank` and `tank assist` stay,
    // even here: a bot drawn into a fight it did not choose - its own party's,
    // or something that simply finds it - should still be able to hold aggro
    // off whoever needs it, and stripping those would be gating a REACTION,
    // not an initiation. `co +flee` is not relied on as the fix, either -
    // Grog carried it into yesterday's death and it did not save him, so it
    // is used below as one layer, not the whole answer. The single-traveller
    // arbitration (TravelHoldsTheWheel, DriveQuests, DriveTravel, SteerableAI)
    // is not called here and this drive does not write rpgInfo, `master`, or
    // any travel/quest state - it only ever touches the combat strategy list.
    //
    // THE BACKSTOP. If a character is unaccompanied, unaimed, and ALREADY
    // fighting something the client's own con-color math would show as `??`
    // (CON_COLOR_UNKNOWN_LEVEL_DIFF - the level gap the brief for this fix
    // says to use, not one invented for this incident), `flee` is added on
    // top of whatever strategies it already has. This is additive and
    // reactive on purpose: it is what runs when the strip above was too late,
    // because the bot already had `pull` live before this drive ever saw it
    // unaccompanied - which is exactly Grug's case.
    //
    // WHY A POLL AND NOT A HOOK ON THE AI TICK. A target-selection guard
    // inside mod-playerbots itself (PullAction, GrindingStrategy) would catch
    // this before a single step is taken, which this cannot. It would also
    // add a branch to the tick every bot in the world runs every update, on a
    // worldserver that has SIGSEGV'd twice today (infra#2891) with its cause
    // not yet closed. Everything this function reads is a pointer already
    // proven valid by SteerableAI, and every decision is a level comparison
    // or a strategy-list lookup - cheap, and nothing here can throw or block.
    // That trade - a several-second window instead of zero, in exchange for
    // touching nothing upstream compiles on its own hot path - is the one
    // made here; the precise version stays out of scope for today (see
    // infra#2912, which exists to find out WHY the family keeps drifting
    // somewhere lethal in the first place).
    //
    // OUT OF SCOPE, ALSO ON PURPOSE. This does not refuse an incoming
    // `co +pull` at delivery time. A grant that lands while a character is
    // unaccompanied is undone here on the next poll instead, within
    // ENGAGEMENT_POLL_MS - self-healing rather than refused outright.
    // Gating DeliverPendingCommands too would close that few-second window,
    // but that function's own history (COMMANDS_PER_POLL, the trigger-
    // collision fix above it) is exactly why that 2-second loop is treated as
    // its own hot path in this file, and closing a several-second window is
    // not worth adding risk to it today.
    void DriveEngagementSafety()
    {
        // THE SAME READERS DriveQuests AND DriveTravel USE. Not a fresh query
        // of these columns: one guarded loader per column, so a schema older
        // than either degrades this drive exactly like it degrades theirs -
        // to "nobody has an opinion", never to "nothing runs".
        std::map<std::string, uint32> const questAims = LoadQuestAims();
        std::map<std::string, std::string> const travelAims = LoadTravelAims();

        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1");
        // No roster, nothing to guard - the same "nothing to do" reading
        // every other roster-wide drive in this file gives a null result.
        if (!result)
            return;

        static char const* const INITIATOR_STRATEGIES[] = {"pull", "aoe", "grind"};

        do
        {
            std::string const name = result->Fetch()[0].Get<std::string>();

            // SteerableAI, not a bare lookup - see SteerableAI above and
            // infra#2891: a name can resolve to a Player mid-login or
            // mid-teardown, with a PlayerbotAI that is non-null right up
            // until it is freed.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            PlayerbotAI* botAI = SteerableAI(bot);
            if (!botAI)
                continue;

            bool const aimed = questAims.count(name) != 0 || travelAims.count(name) != 0;

            // Accompanied means another roster member is in the SAME group
            // and actually in the world right now - see the comment above
            // this function for why a bare Group* is not enough.
            bool accompanied = false;
            if (Group* group = bot->GetGroup())
            {
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member && member != bot && member->IsInWorld())
                    {
                        accompanied = true;
                        break;
                    }
                }
            }

            if (aimed || accompanied)
                continue;

            // THE STRIP. Built from what is actually present, so the
            // ChangeStrategy call and the log line both say only what
            // changed - the same discipline ResolveStrategyChecks already
            // applies to a whispered command's own verdict.
            std::string change;
            std::string strippedList;
            for (char const* strategy : INITIATOR_STRATEGIES)
            {
                if (!StrategyPresent(botAI, StrategyItem{strategy, true}))
                    continue;
                if (!change.empty())
                {
                    change += ',';
                    strippedList += ", ";
                }
                change += '-';
                change += strategy;
                strippedList += strategy;
            }

            if (!change.empty())
            {
                // .c_str(): ChangeStrategy is called elsewhere in this file
                // only with string literals, so this is the one call site
                // that has to work whether the parameter type upstream picked
                // is std::string or a bare const char*.
                botAI->ChangeStrategy(change.c_str(), BOT_STATE_COMBAT);
                LOG_WARN("module.overseer",
                         "overseer: '{}' is alone (no live groupmate) and unaimed (no "
                         "quest or travel aim) - stripping {} from its combat engine so "
                         "it stops choosing new fights; tank/tank assist are left alone "
                         "for if something finds it anyway",
                         name, strippedList);
            }

            // THE BACKSTOP. Only reached for a character already judged
            // unaccompanied and unaimed above - a bot legitimately tanking
            // for its grouped family is never evaluated against this at all.
            if (!bot->IsInCombat())
                continue;
            Unit* victim = bot->GetVictim();
            if (!victim)
                continue;

            uint32 const botLevel = uint32(bot->GetLevel());
            uint32 const victimLevel = uint32(victim->GetLevel());
            if (victimLevel < botLevel + CON_COLOR_UNKNOWN_LEVEL_DIFF)
                continue;

            if (StrategyPresent(botAI, StrategyItem{"flee", true}))
                continue;

            botAI->ChangeStrategy("+flee", BOT_STATE_COMBAT);
            // WARN, not INFO: this is the line that fires when the strip
            // above ran too late, and its absence is exactly how yesterday's
            // family death stayed invisible until somebody was watching.
            LOG_WARN("module.overseer",
                     "overseer: '{}' (level {}) is alone, unaimed, and already fighting "
                     "'{}' (level {}) - that will `??` by the con-color math this module "
                     "was told to use ({}+ level gap) - forcing +flee as a backstop; this "
                     "is NOT proven to save a character on its own (see infra#2891/#2912)",
                     name, botLevel, victim->GetName(), victimLevel,
                     CON_COLOR_UNKNOWN_LEVEL_DIFF);
        } while (result->NextRow());
    }

    // ---------------------------------------------------------- stuck dead --
    //
    // WHAT WAS WATCHED. The family found repeatedly dead at the identical
    // spot in Burning Steppes, over and over, sometimes only seconds apart -
    // confirmed live, not guessed: overseer_death timestamps for the same
    // four characters (Ugga, Grog, Bork, Og) clustered at the same
    // coordinates for over an hour, health always observed at ~1 the moment
    // it was checked, positions never once moving between deaths.
    //
    // THE ACTUAL MECHANISM, read from the pinned mod-playerbots source, not
    // guessed. mod-playerbots already has an escape hatch for exactly this -
    // a character that keeps failing to recover its corpse (dCount >= 5,
    // ReviveFromCorpseAction.cpp / FindCorpseAction.cpp) gets routed to
    // RandomPlayerbotMgr::Revive(), which calls RandomTeleportGrindForLevel()
    // -> RandomTeleport() to relocate it away from wherever it keeps dying.
    // RandomTeleport() (RandomPlayerbotMgr.cpp) opens with a silent early
    // return: `if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
    // return;` - a grouped character that is not the party leader gets
    // NOTHING. No log, no fallback, the function simply does not act.
    //
    // KeepRosterGrouped keeps this family in one permanent party (see its own
    // comment elsewhere in this file) specifically so they behave like a
    // party, not five strangers - and that is exactly what makes four of the
    // five structurally ineligible for this rescue path. Grug leads the
    // party, so his own repeated deaths (independently still a live problem)
    // at least have a working escape hatch; Ugga, Grog, Bork and Og do not,
    // and never will while grouped under him. This is a real interaction
    // between two features that individually make sense - "stay grouped as a
    // family" and "don't randomly scatter a bot that's usefully following its
    // group" - and neither side is wrong in isolation.
    //
    // WHY THIS IS FIXED HERE AND NOT IN mod-playerbots. RandomTeleport's
    // leader gate is shared by every grouped bot on the server, not just this
    // roster - loosening it changes behaviour for the whole 500-bot
    // population for a problem that is specific to a family kept
    // permanently grouped by mod-overseer's own design. This drive is the
    // narrower, correctly-scoped fix: it only ever acts on this roster, and
    // only on a character the built-in path has already had a full
    // STUCK_REVIVAL_DEAD_SECONDS to reach and has not.
    //
    // CORRECTION, 2026-08-28: THIS DRIVE USED TO SKIP THE PARTY LEADER, AND
    // THAT WAS WRONG. The original reasoning was that RandomTeleport's leader
    // gate strands only grouped NON-leaders, so a leader still had a working
    // path. Checked against the pinned module source rather than assumed, that
    // is not what happens for this roster. RandomPlayerbotMgr::ProcessBot - the
    // function containing the whole dead -> Revive() -> RandomTeleportGrindForLevel()
    // rescue - is reached only when IsRandomBot(player) is true
    // (RandomPlayerbotMgr.cpp, the `if (!sRandomPlayerbotMgr.IsRandomBot(player))
    // update = false;` guard before the ProcessBot call). IsRandomBot in turn
    // requires the account to be in the configured random-bot account list,
    // which is built from AiPlayerbot.RandomBotAccountPrefix ("rndbot"). This
    // family logs in on its own named accounts - GRUG, UGGA, GROG, BORK, OG -
    // so IsRandomBot is false for every one of them and that rescue path never
    // runs for ANY roster member, leader or not. The leader gate is real but it
    // was never the operative one here. Confirmed by measurement: the skipped
    // leader stayed dead longest of the five rather than being rescued.
    //
    // So this drive now covers the whole enabled roster.
    // ARM THE DUNGEON MODULE, BECAUSE NOTHING ELSE EVER DOES.
    //
    // mod-dungeon-clear is built, pinned, loaded and configured. The worldserver
    // says so at startup:
    //
    //     mod-dungeon-clear: registered DungeonClear contexts
    //     (strategy/action/trigger/value) into all class registries
    //
    // and then it sits there. Registering a strategy is not enabling it. The
    // module waits for `dc on`, and no code path in this module, in
    // mod-playerbots, or anywhere else has ever issued it. The family has been
    // walking into dungeons with ordinary open-world logic - no pull discipline,
    // no room pre-clear, no boss handling - and losing, which reads from the
    // outside as "the bots are bad at dungeons" when the dungeon brain was
    // simply switched off.
    //
    // TWO FACTS SHAPE THIS DRIVE, both learned by hand before it existed.
    //
    // ONE: `dc on` IS REFUSED OUTSIDE AN INSTANCE. Issued at the portal it comes
    // back accepted-and-inert - the command surface reports `delivered`, the
    // result is null, and the strategy list is unchanged. So this cannot be done
    // once at the start of a run; it has to happen after the character is
    // actually inside, which is what IsDungeon() gates.
    //
    // TWO: IT DOES NOT SURVIVE A RESTART. Strategies live in the bot's engine,
    // not in a table, so every worldserver bounce silently disarms the module
    // and nothing re-applies it. That is why this is a DRIVE and not a one-shot:
    // the HasStrategy check makes it idempotent while armed and self-healing
    // after any restart, teardown, or relog.
    //
    // WHY THE CHAT COMMAND AND NOT ChangeStrategy DIRECTLY. `dc on` does more
    // than add two strategies: it seeds the run's pull setting and camp through
    // the module's own ApplyPullSetting. Adding the strategies by hand would
    // produce a half-armed run whose state the module never initialised, which
    // is a worse failure than not arming at all because it looks armed.
    // HandleCommand is the same hand-off DeliverPendingCommands uses.
    //
    // NOT DISARMED ON THE WAY OUT, deliberately. Leaving an instance is not the
    // same as ending a run - a wipe puts the party outside at a graveyard with
    // the run still meaningfully in progress - and the module's own `dc` verbs
    // own that lifecycle. This drive only ever turns the brain ON.
    void DriveDungeonClear()
    {
        // Retire runs nobody is in before considering new ones, so a party that
        // left and came back opens a fresh run rather than inheriting a stale
        // one's heartbeat.
        CloseAbandonedRuns();

        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        do
        {
            std::string const name = result->Fetch()[0].Get<std::string>();

            // SteerableAI, not a bare lookup - a name can resolve to a Player
            // mid-login or mid-teardown with a PlayerbotAI that is non-null
            // right up until it is freed.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            PlayerbotAI* botAI = SteerableAI(bot);
            if (!botAI)
                continue;

            // A corpse cannot start a dungeon run, and the dead strategy set
            // does not carry the dungeon contexts anyway.
            if (!bot->IsAlive())
                continue;

            // The same predicate the other drives stand down on, read the
            // other way round: this is the one drive that acts BECAUSE a run
            // is in progress.
            if (!InDungeonRun(bot))
                continue;

            // Already armed. This is the line that makes the drive idempotent:
            // once `dc on` has taken, every later poll stops here.
            if (botAI->HasStrategy("dungeon clear", BOT_STATE_NON_COMBAT))
                continue;

            // The run is opened BEFORE the module is armed, so that by the
            // time anything is steering, the thing that owns the steering
            // already exists. The other order leaves a window where a character
            // is armed but unowned.
            OpenOrTouchRun(name, bot->GetMapId());

            botAI->HandleCommand(CHAT_MSG_WHISPER, "dc on", bot);

            // SAID ON EVERY ATTEMPT, NOT ONCE. If the command takes, the
            // HasStrategy check above silences this on the next poll and the
            // line appears exactly once per run. If it keeps appearing, the
            // command is being refused for a reason nobody has found yet, and a
            // repeating line is how that becomes visible instead of a dungeon
            // brain that is quietly off.
            LOG_INFO("module.overseer",
                     "overseer: '{}' is inside map {} and the dungeon clear was not "
                     "armed - issuing 'dc on'",
                     name, static_cast<uint32>(bot->GetMapId()));
        } while (result->NextRow());
    }

    void DriveStuckRevival()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        do
        {
            std::string const name = result->Fetch()[0].Get<std::string>();

            // SteerableAI, not a bare lookup - see SteerableAI above and
            // infra#2891: a name can resolve to a Player mid-login or
            // mid-teardown, with a PlayerbotAI that is non-null right up
            // until it is freed.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            PlayerbotAI* botAI = SteerableAI(bot);
            if (!botAI)
                continue;

            if (bot->IsAlive())
                continue;

            // WHERE THE CORPSE IS, AND WHY ASKING THE PLAYER IS NOT ENOUGH.
            // Player::GetCorpse() resolves through the player's CURRENT map, so
            // it answers null for a corpse lying on a different one. That is not
            // an edge case here, it is the normal result of a wipe in an
            // instance: the party releases, the ghosts land at an outdoor
            // graveyard on map 0, and every corpse stays on the instance map.
            //
            // The old code read that null as "ghost with no corpse yet, or
            // already past this" and skipped. For a ghost whose corpse is on
            // another map, neither is true and neither ever becomes true, so it
            // was skipped on every pass, forever. Measured on the dev world:
            // five characters wiped inside a dungeon, released to the graveyard
            // outside, and sat there as ghosts with this drive logging nothing
            // at all - the one path that could have recovered them was the one
            // returning early.
            //
            // So the corpse is read from the table when the map-scoped lookup
            // cannot see it. `corpse.time` is the same ghost time
            // Corpse::GetGhostTime() returns, and the position columns are the
            // same ones the trap check below needs, so the two paths agree by
            // construction rather than by luck.
            Corpse* corpse = bot->GetCorpse();
            int64 ghostTime = 0;
            uint32 corpseMap = 0;
            float corpseX = 0.f;
            float corpseY = 0.f;

            if (corpse)
            {
                ghostTime = corpse->GetGhostTime();
                corpseMap = corpse->GetMapId();
                corpseX = corpse->GetPositionX();
                corpseY = corpse->GetPositionY();
            }
            else if (QueryResult row = CharacterDatabase.Query(
                         "SELECT time, mapId, posX, posY FROM corpse WHERE guid = {}",
                         bot->GetGUID().GetCounter()))
            {
                Field* f = row->Fetch();
                ghostTime = int64(f[0].Get<uint32>());
                corpseMap = f[1].Get<uint16>();
                corpseX = f[2].Get<float>();
                corpseY = f[3].Get<float>();
            }

            // Still nothing: a ghost genuinely without a corpse anywhere. That
            // is the case the original guard was written for and it is left
            // alone - there is no death to time from and nothing to recover to.
            if (!ghostTime)
                continue;

            int64 const deadFor = time(nullptr) - ghostTime;
            if (deadFor < STUCK_REVIVAL_DEAD_SECONDS)
                continue;

            // Has this character been dying over and over in one small area?
            // If so the local graveyard is feeding it back to whatever killed
            // it, and reviving there again is the harm, not the fix - see
            // STUCK_REVIVAL_TRAP_* above for the measurement that established
            // this.
            uint64 recentDeathsHere = 0;
            if (QueryResult trap = CharacterDatabase.Query(
                    "SELECT COUNT(*) FROM overseer_death WHERE character_name = '{}' "
                    "AND created_at >= NOW() - INTERVAL {} MINUTE AND map = {} "
                    "AND POW(pos_x - {}, 2) + POW(pos_y - {}, 2) <= {}",
                    Esc(name), STUCK_REVIVAL_TRAP_MINUTES, corpseMap,
                    corpseX, corpseY,
                    STUCK_REVIVAL_TRAP_RADIUS * STUCK_REVIVAL_TRAP_RADIUS))
                recentDeathsHere = trap->Fetch()[0].Get<uint64>();

            if (recentDeathsHere >= STUCK_REVIVAL_TRAP_DEATHS)
            {
                // Home, not the graveyard. m_homebind* is the character's own
                // bind point - the destination its own Hearthstone would use -
                // so this is the existing in-game escape, reached without the
                // 10-second cast that combat here interrupts every time.
                //
                // The LEADER's bind point when there is one, so the family
                // lands together and mod-overseer's follow logic just works.
                // Sending each to its own bind would scatter this roster
                // across two starting zones (measured: Elwynn and Dun Morogh,
                // ~2000 yards apart) and leave the party split on arrival.
                Player* home = bot;
                if (Group* group = bot->GetGroup())
                    if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
                        home = leader;

                bot->TeleportTo(home->m_homebindMapId, home->m_homebindX,
                                home->m_homebindY, home->m_homebindZ, 0.f);
                bot->ResurrectPlayer(1.0f);
                bot->SpawnCorpseBones();

                LOG_WARN("module.overseer",
                         "overseer: '{}' has died {} times within {}yd in the last {}min - "
                         "the nearest graveyard is inside whatever is killing it, so reviving "
                         "there again would only speed the loop up (measured: doing exactly "
                         "that doubled this roster's death rate). Sent home to '{}'s bind "
                         "point instead",
                         name, recentDeathsHere, uint32(STUCK_REVIVAL_TRAP_RADIUS),
                         STUCK_REVIVAL_TRAP_MINUTES, home->GetName());
                continue;
            }

            // NO GRAVEYARD ON THIS MAP IS NOT A REASON TO WALK AWAY.
            //
            // This used to be `if (!grave) continue;`, and that stranded every
            // character who died INSIDE AN INSTANCE. Measured: `game_graveyard`
            // holds ZERO rows on map 36, because the Deadmines graveyards are
            // outdoor ones, so the lookup comes back empty for a character
            // standing in the instance. A body lay there for 29 minutes while
            // this drive ran the whole time and never once considered it - over
            // the same window the drive logged exactly two revivals, both for a
            // character who had died outside.
            //
            // THE SAME SHAPE AS THE CORPSE BUG THIS DRIVE ALREADY HAD (#79):
            // an early return whose comment reads as a transient case and whose
            // reality is permanent. A map does not grow a graveyard while you
            // wait. Every `continue` on this path has to answer "can this
            // condition change on its own?" - and when it cannot, skipping is
            // stranding.
            //
            // It matters more here than it looks. Upstream's own rescue is
            // gated behind IsRandomBot, which is false for this roster, so
            // without this fallback a death inside an instance has NO recovery
            // path at all: not upstream's, not this one. It also silently
            // freezes dungeon runs, because the party holds position waiting
            // for a member who is never coming back.
            //
            // The bind point is the fallback because it is the one destination
            // that always exists, it is the character's OWN in-game escape, and
            // the trap branch directly above already relies on it working.
            GraveyardStruct const* grave = sGraveyard->GetClosestGraveyard(bot, bot->GetTeamId());
            if (!grave)
            {
                bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX,
                                bot->m_homebindY, bot->m_homebindZ, 0.f);
                bot->ResurrectPlayer(0.5f);
                bot->SpawnCorpseBones();

                LOG_WARN("module.overseer",
                         "overseer: '{}' has been dead for {}s on map {}, which has no "
                         "graveyard of its own - dying inside an instance leaves nothing "
                         "for GetClosestGraveyard to return, so it was sent to its own "
                         "bind point instead of being left where it fell",
                         name, deadFor, static_cast<uint32>(bot->GetMapId()));
                continue;
            }

            // Mirrors exactly what SpiritHealerAction::Execute already does
            // on a successful revive (ResurrectPlayer + SpawnCorpseBones) -
            // this is not a new resurrection mechanism, it is the same one,
            // reached without depending on a path that never runs for this
            // roster at all.
            bot->TeleportTo(grave->Map, grave->x, grave->y, grave->z, 0.f);
            bot->ResurrectPlayer(0.5f);
            bot->SpawnCorpseBones();

            LOG_WARN("module.overseer",
                     "overseer: '{}' has been dead for {}s - mod-playerbots' own "
                     "auto-relocate-after-repeated-deaths (RandomTeleport, "
                     "RandomPlayerbotMgr.cpp) is gated behind IsRandomBot, which is false "
                     "for this roster's named accounts, so it would never reach a graveyard "
                     "on its own; resurrected at the nearest graveyard instead",
                     name, deadFor);
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

            // Same guard as the other two drives: this one goes on to send
            // the character to a trainer, so a half-built Player is just as
            // unsafe here. See SteerableAI above.
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(bot))
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

            // THE FOURTH BLOCKER, AND THE ONE NOBODY HAD NAMED (infra#2757,
            // infra#2782). This is also where the alchemy all five hold came
            // from, and until it was closed the other three fixes were
            // pointless: freeing a slot for tailoring would simply have handed
            // the slot to whatever the sweep below reached first, within sixty
            // seconds, forever.
            //
            // WHAT InitAvailableSpells ACTUALLY DOES. It walks EVERY Tradeskill
            // and Class trainer template in the world and learns every spell
            // CanTeachSpell says yes to, with no NPC, no money and no journey
            // (PlayerbotFactory.cpp:3210-3253). CanTeachSpell refuses a primary
            // profession's first rank on exactly one condition - that the
            // character has no free primary profession point
            // (Trainer.cpp:145-148). So while a slot is open, the first
            // profession in trainer-template order is taken out of thin air.
            // That is why all five hold the SAME profession at 1/75 rather than
            // five different ones: it was never a roll, it was an iteration
            // order. AiPlayerbot.ClassMatchingProfessionChance could not have
            // helped, because InitTradeSkills - the function that reads it -
            // returns at its first line for anyone who is not a random bot
            // (PlayerbotFactory.cpp:2755-2758).
            //
            // WHAT THE GUARD DOES, AND WHAT IT DELIBERATELY DOES NOT. Zeroing
            // the free-slot counter for the duration of the sweep makes
            // CanTeachSpell refuse primary FIRST RANKS and nothing else.
            // Journeyman and higher ranks are not first ranks, recipes are not
            // first ranks, and no class spell is - so every reason this module
            // calls InitAvailableSpells at all still works, and a character
            // that already holds a trade still gets its rank-ups. The only
            // thing that becomes impossible is acquiring a profession without a
            // trainer, which is the thing that was never meant to happen.
            //
            // Restored unconditionally, because a character left holding zero
            // free slots could never learn the trade it is being sent to learn -
            // which would swap one silent blocker for another.
            uint32 const freeProfessionSlots = bot->GetFreePrimaryProfessionPoints();
            if (freeProfessionSlots)
                LOG_INFO("module.overseer",
                         "overseer: '{}' has {} free primary profession slot(s); holding "
                         "them shut for the trainer-table sweep so it cannot take one "
                         "without a trainer", name, freeProfessionSlots);
            bot->SetFreePrimaryProfessions(0);  // Player.h:1796
            factory.InitAvailableSpells();
            bot->SetFreePrimaryProfessions(static_cast<uint16>(freeProfessionSlots));

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

    // Write the queued deaths (infra#2912). Same shape as FlushEvents - one
    // multi-row INSERT per tick, built on the world thread only, because
    // EscapeString borrows the shared synchronous connection with no lock of
    // its own (see the file header) - with one difference: the batch is a
    // VECTOR, not a keyed map, because a death is never coalesced (see
    // PendingDeath's own comment for why).
    //
    // NEVER LET THIS THROW OR BLOCK THE KILL PATH. It cannot: by the time
    // this runs, the death that produced each row has already finished -
    // RecordDeath only ever touches memory, and this function is called from
    // OnUpdate on a timer, never from inside a death hook. If overseer_death
    // does not exist yet on this schema (the DDL and this reader ship in
    // DIFFERENT images - see EachLateColumnIsReadOnceAndGuardedOnItsOwn's
    // reasoning, which applies here to a whole table rather than one column),
    // CharacterDatabase.Execute logs the failure and returns; it does not
    // throw, and nothing upstream of it - a bot's own death - is put at risk
    // either way, because nothing here runs on that path.
    void FlushDeaths()
    {
        std::vector<PendingDeath> batch;
        uint64 dropped = 0;
        {
            std::lock_guard<std::mutex> guard(g_deathMutex);
            if (g_deathQueue.empty() && !g_droppedDeaths)
                return;
            batch.swap(g_deathQueue);
            dropped = g_droppedDeaths;
            g_droppedDeaths = 0;
        }

        if (dropped)
            LOG_WARN("module.overseer", "overseer: dropped {} deaths (queue full)", dropped);

        if (batch.empty())
            return;

        std::ostringstream ss;
        ss << "INSERT INTO overseer_death (character_name, character_guid, level, "
              "map, zone, pos_x, pos_y, pos_z, killer_type, killer_name, killer_entry, "
              "health_at_death, max_health_at_death, seconds_since_full_health, "
              "job, quest_aim, travel_target, grouped, group_size, group_leader) VALUES ";
        bool first = true;
        for (PendingDeath const& d : batch)
        {
            if (!first)
                ss << ',';
            first = false;
            ss << '(' << "'" << Esc(d.characterName) << "'," << d.characterGuid
               << ',' << static_cast<uint32>(d.level)
               << ',' << static_cast<uint32>(d.mapId)
               << ',' << d.zoneId
               << ',' << d.x << ',' << d.y << ',' << d.z
               << ",'" << Esc(d.killerType) << "'"
               << ",'" << Esc(d.killerName) << "'"
               << ',' << d.killerEntry
               << ',' << d.healthAtDeath
               << ',' << d.maxHealthAtDeath
               << ',' << d.secondsSinceFullHealth
               << ",'" << Esc(d.job) << "'"
               << ',' << d.questAim
               << ",'" << Esc(d.travelTarget) << "'"
               << ',' << static_cast<uint32>(d.grouped)
               << ',' << static_cast<uint32>(d.groupSize)
               << ",'" << Esc(d.groupLeader) << "'"
               << ')';
        }
        CharacterDatabase.Execute(ss.str().c_str());

        LOG_INFO("module.overseer",
                 "overseer: recorded {} death(s), most recently '{}' at level {} "
                 "in zone {} (killer: {} '{}')",
                 batch.size(), batch.back().characterName,
                 static_cast<uint32>(batch.back().level), batch.back().zoneId,
                 batch.back().killerType, batch.back().killerName);
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

            // Bot orders only. 'chat', 'gm', 'probe', 'give', 'trade',
            // 'share' and 'job' do not go through PlayerbotAI::HandleCommand
            // and share no
            // trigger, so nothing they do can be overwritten by the row
            // after them.
            if (kind != "chat" && kind != "gm" && kind != "probe" && kind != "give"
                && kind != "trade" && kind != "share" && kind != "job")
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
            else if (kind == "trade")
                detail = DoTrade(player, targetArg, command, status, rowResult);
            else if (kind == "share")
                detail = DoShare(player, targetArg, command, status, rowResult);
            else if (kind == "job")
                detail = DoJob(player, command, status);
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

    // The job-schedule vocabulary (infra#2834). DUPLICATED, ON PURPOSE AND
    // UNAVOIDABLY, in production/scripts/wow-overseer/jobs.py - same
    // situation as TravelRoles above, and for the same reason: a compiled
    // module and a Python process share no schema. tests/test_job_mode.py
    // compares the two keys in both directions.
    //
    // Only 'quest' does anything beyond writing the column - see the job gate
    // in DriveQuests above. Every other name here is still a VALID thing to
    // set: this list is what tells DoJob a name is real, not what tells it
    // a name is built.
    static std::vector<std::string> const& JobModes()
    {
        static std::vector<std::string> const modes = {
            "quest", "farm", "dungeon", "grind", "gear hunt", "craft",
            "town run", "train", "rest", "bank", "reputation", "guild business",
        };
        return modes;
    }

    // Set `player`'s job-schedule mode (infra#2834). `command` is the mode
    // name verbatim - it was already validated by jobs.resolve on the Python
    // side before being written to overseer_command, but that side sent a
    // string over a boundary this module does not trust any string across,
    // so it is checked again here against the same list mod-overseer would
    // otherwise silently accept anything into.
    //
    // A PLAIN COLUMN WRITE, not a strategy change: nothing here touches
    // rpgInfo or ChangeStrategy. Only the quest-drive GATE (DriveQuests, the
    // jobs map read at the top of its loop) reacts to this column today -
    // every mode besides 'quest' means "the quest drive stands down", full
    // stop, until each mode gets its own drive built.
    static char const* DoJob(Player* player, std::string const& command, char const*& status)
    {
        std::string const mode = LowerName(command);
        auto const& modes = JobModes();
        if (std::find(modes.begin(), modes.end(), mode) == modes.end())
            return "unknown job mode";

        CharacterDatabase.DirectExecute(
            "UPDATE overseer_roster SET job = '{}' WHERE name = '{}'",
            mode, player->GetName());

        status = "delivered";
        return "";
    }

    // Run a dot-command through the character's OWN session, so it carries
    // that account's real security level. A non-GM account is refused by the
    // core exactly as if the player had typed it.
    //
    // AND FOR A BOT THAT SECURITY LEVEL IS ALWAYS SEC_PLAYER, WHATEVER THE
    // ACCOUNT SAYS. mod-playerbots builds bot sessions with the level as a
    // hardcoded constructor argument and never reads `account_access`:
    //
    //     new WorldSession(botAccountId, "", 0x0, nullptr, SEC_PLAYER, ...)
    //         -- PlayerbotMgr.cpp:203, and identically at
    //            RandomPlayerbotFactory.cpp:701 and TravelMgr.cpp:3393,
    //            so it is the module's uniform posture, not one path's slip.
    //
    // So GRANTING gmlevel 3 TO A BOT'S ACCOUNT DOES NOTHING - not after a
    // relog, not after a worldserver restart. Two sessions independently
    // assumed otherwise on 2026-08-25, granted it, saw `.additem` refused,
    // and re-granted before reading this constructor. The rows were later
    // deleted precisely because an inert gmlevel 3 sitting in the table is
    // worse than none: the next reader concludes those characters have it.
    //
    // kind='gm' is therefore usable ONLY on a character whose session came
    // from a REAL CLIENT LOGIN. If a dot-command has to run against the
    // family, a human logs in and types it; there is no server-side road.
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

    // --------------------------------------------------------------- trade --
    //
    // Hand ONE item from one living character to another through the core's
    // real trade, so it happens in the world instead of in the database.
    //
    // WHY, WHEN give ALREADY MOVES ITEMS CORRECTLY. It does, and it stays.
    // But give is three database writes: nothing renders, nothing animates,
    // and a camera pointed at either character sees an item appear in a bag
    // with no visible cause. mod-overseer#14 asked for the item to move "in
    // the world, not a DB UPDATE teleporting it". This is that path; give
    // remains the one to reach for when the two are nowhere near each other.
    //
    // WHY THIS IS REACHABLE WHEN THE NOTE ABOVE DoGive SAYS IT IS NOT. That
    // note is about mod-playerbots trade, and every word of it still holds:
    // its chat command targets the bot's master (TradeAction.cpp:26-38), and
    // CheckTrade takes a bot-to-bot branch whenever the master is not a real
    // player (TradeStatusAction.cpp:165-200), which a selfbot is not
    // (PlayerbotAI.cpp:4389-4395). None of that is used here.
    //
    // The CORE's trade is a different machine. Its handlers are public on
    // WorldSession (WorldSession.h:684 opens the opcodes-handlers public
    // section; the trade handlers sit at :893-902), they take Player pointers
    // rather than a client, and the two that matter ignore their packet
    // argument outright (TradeHandler.cpp:237 and :692 both take
    // WorldPacket& /*recvPacket*/). Both characters are real Player objects
    // on one worldserver, so the exchange is a sequence of ordinary calls.
    //
    // WHAT COMPLETES IT. The second accept: HandleAcceptTradeOpcode gates the
    // exchange on his_trade->IsAccepted() (TradeHandler.cpp:340) and only
    // then runs moveItems inside a CharacterDatabase transaction, frees both
    // TradeData, and reports to both sessions. The atomicity is the core's,
    // which is why - unlike DoGive - this function opens no transaction.
    //
    // ORDERING THAT IS NOT OPTIONAL. TradeData::SetItem clears the accepted
    // flag on BOTH sides every time it is called (TradeData.cpp:64-65), so the
    // item goes in BEFORE either accept. Reversed, the window sits open for
    // ever while this function believes it succeeded - the precise failure
    // this module exists to stop reporting as success. It is checked at the
    // end by reading the item's owner back rather than by trusting the calls.
    //
    // WHY EVERY CONDITION IS TESTED HERE AS WELL AS BY THE CORE. Initiate
    // refuses for a dozen reasons and reports each one by sending a status
    // packet to a session, which a module cannot read back. Testing them first
    // is what lets detail NAME the wall that was hit; the core then tests
    // them again and remains the authority.
    static char const* DoTrade(Player* giver, std::string const& receiverName,
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
                          "malformed trade: want guid:<item_instance.guid> or entry:<id>");

        if (receiverName.empty())
            return refuse("no receiver", "no receiver (put the receiving character in target_arg)");

        Player* receiver = ObjectAccessor::FindPlayerByName(receiverName);
        if (!receiver)
            return refuse("receiver offline", "receiver not online");

        if (receiver == giver)
            return refuse("same character", "giver and receiver are the same character");

        WorldSession* giverSession = giver->GetSession();
        WorldSession* receiverSession = receiver->GetSession();
        if (!giverSession || !receiverSession)
            return refuse("no session", "one of the characters has no session");

        // Each of these is a condition HandleInitiateTradeOpcode
        // (TradeHandler.cpp:721-840) tests and answers with a status packet.
        // Named here so the row says which one.
        if (!giver->IsAlive())
            return refuse("giver dead", "giver is dead");
        if (!receiver->IsAlive())
            return refuse("receiver dead", "receiver is dead");
        if (giver->IsInFlight() || receiver->IsInFlight())
            return refuse("in flight", "one of the characters is on a flight path");
        if (giver->HasUnitState(UNIT_STATE_STUNNED) || receiver->HasUnitState(UNIT_STATE_STUNNED))
            return refuse("stunned", "one of the characters is stunned");
        if (giverSession->isLogingOut() || receiverSession->isLogingOut())
            return refuse("logging out", "one of the characters is logging out");
        if (giver->GetTradeData() || receiver->GetTradeData())
            return refuse("already trading", "one of the characters is already in a trade");
        if (giver->GetLevel() < sWorld->getIntConfig(CONFIG_TRADE_LEVEL_REQ))
            return refuse("below trade level", "giver is below the trade level requirement");

        // The one that is normally false for a travelling group rather than
        // rarely false. TRADE_DISTANCE is 11.11 yards (ObjectDefines.h:29).
        if (!giver->IsWithinDistInMap(receiver, TRADE_DISTANCE, false))
            return refuse("too far apart", "characters are too far apart to trade");

        Item* item = FindCarriedItem(giver, spec.byGuid, spec.key);
        if (!item)
            return refuse("item not found",
                          spec.byGuid ? "no carried item with that guid on the giver"
                                      : "no carried item with that entry on the giver");

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return refuse("no item template", "item has no template");

        ObjectGuid const itemGuid = item->GetGUID();
        uint32 const itemEntry = item->GetEntry();
        uint32 const itemCount = item->GetCount();
        std::string const itemName = proto->Name1;

        auto describe = [&](char const* outcome, char const* reason)
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
              << ",\"method\":\"trade\"}";
            out = o.str();
        };

        // Soulbound named separately for the same reason DoGive names it: it
        // is the one refusal that is permanent, and an operator told only
        // "cannot be traded" will retry it for ever.
        if (item->IsSoulBound())
        {
            describe("refused", "item is soulbound to the giver");
            return "item is soulbound and can never be handed over";
        }
        if (!item->CanBeTraded())
        {
            describe("refused", "item cannot be traded (non-empty bag, worn bag, being looted, "
                                "or bound by enchant)");
            return "item cannot be handed over";
        }

        // Asked before anything opens, so a receiver with no room costs a row
        // rather than a hung trade window.
        ItemPosCountVec dest;
        InventoryResult const msg = receiver->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            describe("refused", "receiver cannot store the item");
            return msg == EQUIP_ERR_INVENTORY_FULL ? "receiver bags are full"
                                                   : "receiver cannot store the item";
        }

        // ---- drive the core's own state machine ----------------------------

        WorldPacket initiate(CMSG_INITIATE_TRADE, 8);
        initiate << receiver->GetGUID();
        giverSession->HandleInitiateTradeOpcode(initiate);

        TradeData* giverTrade = giver->GetTradeData();
        if (!giverTrade || !receiver->GetTradeData())
        {
            // Everything it tests was tested above, so reaching here means a
            // condition this function does not know about - a script hook
            // (OnPlayerCanInitTrade), a faction rule, or a spectator state.
            giver->TradeCancel(false);
            describe("refused", "the core refused to open the trade window");
            return "the core refused to open the trade window";
        }

        // Cosmetic, and the reason this is worth doing at all: it is what puts
        // the trade window on screen for anyone watching either character.
        WorldPacket begin(CMSG_BEGIN_TRADE, 0);
        receiverSession->HandleBeginTradeOpcode(begin);

        // BEFORE the accepts. See the ordering note above.
        giverTrade->SetItem(TradeSlots(0), item);

        WorldPacket accept(CMSG_ACCEPT_TRADE, 0);
        giverSession->HandleAcceptTradeOpcode(accept);
        receiverSession->HandleAcceptTradeOpcode(accept);

        // ---- believe nothing; read the item back ---------------------------
        //
        // A completed trade frees both TradeData and leaves the item owned by
        // the receiver. Either half still being true means it did not
        // complete, and a window left open would otherwise be reported as a
        // success by a function that only counted its own calls.
        if (giver->GetTradeData() || receiver->GetTradeData())
        {
            giver->TradeCancel(false);
            describe("refused", "the trade did not complete and was cancelled");
            return "the trade did not complete";
        }

        Item* moved = receiver->GetItemByGuid(itemGuid);
        if (!moved)
        {
            describe("refused", "the trade closed but the receiver does not hold the item");
            return "the trade closed but the item did not move";
        }

        LOG_INFO("module.overseer", "overseer: traded item {} (entry {}) from '{}' to '{}'",
                 itemGuid.GetCounter(), itemEntry, giver->GetName(), receiver->GetName());

        describe("moved", "");
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

            // infra#2912: the health-history cache RecordDeath reads. Gated to
            // the roster, same as RecordEvent - the point is the five named
            // characters, and there is no reason to pay a map-and-mutex touch
            // per tick for the other five hundred. Written here rather than in
            // a hook because there is no per-tick "health changed" hook in the
            // pinned core cheap enough to use on a 500-bot world; sampling
            // alongside a walk that was happening anyway costs nothing extra.
            if (OnRoster(p->GetName()))
                RememberHealth(p->GetName(), p->GetHealth(), p->GetMaxHealth());
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
    uint32 _travelTimer = 0;
    uint32 _professionTimer = 0;
    uint32 _engagementTimer = 0;

    // ---------------------------------------------------------------- travel --
    //
    // One spawn point of one NPC the family can be sent to. Built once, from
    // sObjectMgr, and then only read - see BuildTravelIndex for why it is a
    // cache and not a query.
    struct TravelSpawn
    {
        uint32 entry{0};
        uint32 mapId{0};
        float x{0.f};
        float y{0.f};
        float z{0.f};
        uint32 npcFlags{0};
    };
    std::vector<TravelSpawn> _travelSpawns;
    bool _travelIndexBuilt = false;

    // Which primary professions each trainer ENTRY can start somebody in
    // (infra#2757). Keyed by creature entry, not by spawn: the same trainer is
    // spawned once, but the question "can this one teach tailoring" is about
    // the template, and every spawn of it has the same answer.
    //
    // WHY THIS IS CACHED AND NOT ASKED PER RESOLVE. Answering it walks the
    // trainer's whole spell list and resolves a SpellInfo per entry. Doing that
    // for every profession-trainer spawn on the map, on the world thread, every
    // fifteen seconds, to answer a question whose answer never changes, is the
    // same waste BuildTravelIndex already refused for spawns. Filled lazily by
    // TrainerStartsSkill on the first errand that asks.
    std::map<uint32, std::set<uint32>> _trainerSkills;

    // What DriveTravel knew about each errand last time round, so an aim that
    // can never land is given up on rather than renewed forever, and so
    // "arrived" is announced once instead of every poll. World thread only -
    // DriveTravel runs from OnUpdate - so unguarded, like _lastAim. Lost on
    // restart, which only restarts the backstop clock.
    struct TravelState
    {
        std::string target;   // the column value as this loop last saw it
        // What the errand was FOR, as of the last poll. A changed learn skill
        // is a changed errand even when the target keyword has not moved,
        // because the resolve is narrowed by it - see ResolveTravelTarget.
        // Without this the pinned spawn from the previous trade would be kept
        // and the character would walk to a trainer for a skill it is no
        // longer being sent to learn.
        uint32 learn{0};
        time_t since{0};      // when that target was first seen
        uint32 entry{0};      // the spawn it resolved to
        bool pinned{false};   // ...and which it is NOT resolved away from again
        uint32 mapId{0};      // where that spawn is, so a map change drops the pin
        float x{0.f};
        float y{0.f};
        float z{0.f};
        bool arrived{false};  // announced already
        // The nearest this character has ever been to this errand's target, and
        // 0 while it has not been measured yet. The backstop's memory of whether
        // the walk is going anywhere - see the progress check in DriveTravel.
        float closest{0.f};
    };
    std::map<std::string, TravelState> _travelState;

    // The unlearn this module has already refused for each character, so a
    // standing disagreement about the price is said once rather than twice a
    // minute forever (infra#2757). Name -> the skill id refused. Cleared when
    // the request is cleared, and lost on restart - which costs one repeated
    // log line and no correctness, exactly like _travelHandback above.
    std::map<std::string, uint32> _unlearnRefused;
    // When travel last let go of a character, so the quest drive does not pick
    // it up on the same tick the errand ended. Written by ClearTravelAim and by
    // PruneTravelState - every way an errand can end - and read only by
    // TravelHoldsTheWheel, which sweeps an entry once it lapses. World thread
    // only, like everything else on this loop.
    std::map<std::string, time_t> _travelHandback;
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
        // When the travel drive took the wheel, or 0 while this drive has it.
        // Both a "said it once" flag for the stand-down line and the amount the
        // backstop clock is carried forward on the hand-back.
        time_t travelHeld{0};
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
    uint32 _deathTimer = 0;
    bool _watchLoaded = false;
};

void Addmod_overseerScripts()
{
    new OverseerWorldScript();
    new OverseerChatScript();
    new OverseerEventScript();
}
