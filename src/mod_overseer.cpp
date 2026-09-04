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
 *                     A CONTAINER goes into a free bag slot and is worn, not
 *                     into the inventory (mod-overseer#169), and a receiver
 *                     with no room is offered a bag first when the giver has
 *                     one to spare. A give that cannot succeed is left alone
 *                     for GIVE_REFUSAL_BACKOFF_SECONDS rather than retried on
 *                     every poll of the queue.
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
// The core reporting its own commit, the same call Banner.cpp makes for the
// startup line. It is what lets a realm say which AzerothCore it is running
// without anybody having to declare it (mod-overseer#184).
#include "GitRevision.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "MapMgr.h"
#include "PathGenerator.h"
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
#include "Timer.h"
#include "Trainer.h"
#include "World.h"
#include "WorldSession.h"
#include "TradeData.h"
#include "Opcodes.h"
#include "WorldPacket.h"

// The decisions this module makes that need nothing from the world. Its own
// header so that something other than this translation unit can reach them -
// see overseer_decisions.h for why that was worth a file.
#include "overseer_decisions.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
// std::exception, for the one catch in this file. Pulled in
// transitively by several of the headers above, but a catch that
// compiles only by accident of somebody else's include is a catch
// that stops compiling when they tidy up.
#include <exception>
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

// How often the roster is checked for a character playing with no client
// attached (#131). This used to be the login cadence, and 30s was chosen
// because a login is a query holder plus a world-thread callback. Nothing is
// logged in any more; the pass is one roster SELECT and a handful of name
// lookups, and what it catches is a character playing unobserved, which the
// operator counts by the minute. So it runs at the snapshot cadence.
constexpr uint32 ROSTER_POLL_MS = 5000;

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

// HOW QUICKLY A LIVING CHARACTER BELOW THE WALKABLE WORLD IS BROUGHT BACK.
// The Deadmines exit dropped two characters more than five hundred yards in
// six seconds (#174), so the eight-second revival cadence is too slow for this
// different failure. One check a second over a five-character roster is early
// enough to act while the bad position is still a position, not a death.
constexpr uint32 TERRAIN_RECOVERY_POLL_MS = 1000;

// HOW FAR ABOVE A CHARACTER ANOTHER SURFACE MUST BE before it can mean the
// character is underneath the world rather than walking an ordinary slope.
// The first live Stormwind reading was a 34-36 yard gap, but the trapped party
// later climbed hidden terrain until only part of that gap remained while its
// point of view still showed it inside city geometry. Ten is the same maximum
// single-sample drop GroundHolds permits. The navmesh check, not a larger gap,
// is what distinguishes a real lower interior from terrain below a WMO.
constexpr float TERRAIN_RECOVERY_GAP_YARDS = 10.0f;

// The surface lookup starts this far above the character and searches the
// same distance down. It reaches the measured city floor without asking from
// MAX_HEIGHT, which can answer with an unrelated roof or terrain layer.
constexpr float TERRAIN_RECOVERY_PROBE_YARDS = 60.0f;

// Four short probes answer whether the character's current height belongs to
// the local walkable mesh. Two yards fits inside an ordinary room while still
// requiring the navmesh to find a real polygon rather than a point in place.
constexpr float TERRAIN_RECOVERY_PATH_YARDS = 2.0f;

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

// HOW LONG THE HEALER GETS BEFORE THIS DRIVE STOPS WAITING FOR IT.
// STUCK_REVIVAL_DEAD_SECONDS is an open-world number: out there nothing else
// will ever revive a roster character, so 45s of nobody coming is proof. Inside
// an instance with a run open that is not true. The dungeon module's own
// post-combat rez (DcRezRecovery.cpp, EvaluateImpl) elects a living Priest,
// Paladin, Shaman or Druid and walks it to the corpse, and it gives that walk
// `DungeonClear.PostCombatRezTimeoutSecs` of out-of-combat time (90 by
// default, mod_dungeon_clear.conf.dist:264-274; read at DcRezRecovery.cpp:245).
// Measured on the first tank-driven run: the tank died 155 yards in, the
// dungeon module logged that the healer was coming, and 45s later this drive
// teleported the tank to the graveyard outside and resurrected it there. The
// four followers then released ("DC tank gone") and the run was over, with the
// tank alive in a field and everyone else inside.
//
// So a character dead on a dungeon map with a living resurrector in its party
// gets the dungeon module's whole budget plus a margin before this drive
// concludes nobody is coming. The margin covers what this drive cannot see:
// the module's clock only runs OUT of combat and is reset by a fight, while
// this one is wall time from the death.
constexpr int64 STUCK_REVIVAL_HEALER_SECONDS = 90 + 30;

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

// WHEN THE NEAREST GRAVEYARD IS SOMEBODY ELSE'S CAMP.
// GetClosestGraveyard answers a geometry question. The character needs a
// safety question answered: where can it stand for ten seconds at half
// health. Measured after the first full instance clear: a level 22 follower
// died once on the road through Duskwood, was resurrected by this drive at
// the nearest graveyard, Raven Hill cemetery, and died there twice more
// inside a minute to level 25-30 undead, each time before it could take a
// step. The trap check above (three deaths within 100 yards in fifteen
// minutes) is the right backstop for a loop that has already run; this is
// the check that stops the loop from starting, by asking about the
// destination BEFORE the first revival there.
//
// A graveyard is refused when the ground it stands on is levelled above the
// character (AreaTable's area_level, the same number the client's zone text
// reads), or when hostile spawns above the character's level sit within
// GRAVEYARD_THREAT_RADIUS of it. Spawn data, not the live grid: a graveyard
// two grids away is not loaded, and an unloaded grid reads as "no creatures",
// which is exactly the wrong answer for the question being asked.
// The alternatives are the core's own candidate set for that corpse (the
// graveyards graveyard_zone links to its zone, filtered by team, which is
// what GetClosestGraveyard itself walks at GameGraveyard.cpp:145-160), nearest
// first; when none qualifies the bind point is the destination, because it
// is the one place that always exists and is always the character's own.
constexpr float GRAVEYARD_THREAT_RADIUS = 60.0f;
constexpr std::size_t GRAVEYARD_CANDIDATES = 6;

// Two revivals of the same character at the same graveyard inside this
// window, and the second goes to the bind point instead, unconditionally:
// whatever the scoring above thought of that graveyard, the character has
// now demonstrated it cannot live there.
constexpr int64 GRAVEYARD_REPEAT_SECONDS = 5 * 60;

// THE GRACE AFTER A REVIVAL. A character revived at half health that is
// immediately walked off by `follow` or `new rpg` arrives at its next fight
// at half health. So it is held (`stay`, which upstream honours over
// `follow`) for this long before this module lets anything move it - unless
// hostile spawns sit within REVIVAL_AGGRO_RADIUS of the spot, in which case
// standing still is the worse option and it is left free to move away.
constexpr int64 REVIVAL_HOLD_SECONDS = 20;
constexpr float REVIVAL_AGGRO_RADIUS = 30.0f;

// The living party member the dungeon module could send to a corpse, or null.
//
// Re-derived from DcRezRecovery::CanRecover (DcRezRecovery.cpp:361-377) with
// core calls rather than by including that module's headers: a group member
// on the SAME MAP as the dead character, alive, of a class that has a
// resurrection spell (IsRezClass, DcRezRecovery.cpp:32-44: Priest, Paladin,
// Shaman, Druid). Same map is the module's own filter (its snapshot drops any
// member on another map, DcRezRecovery.cpp:79), and it is the right one here
// too: a ghost that has released out of an instance is no longer something
// that module will walk to.
//
// Members read at the pinned core:
//   getClass        Unit.h:844        uint8 getClass() const
//   IsAlive         Unit.h:1793       bool IsAlive() const
//   GetGroup        Player.h:2520     Group* GetGroup()
//   GetFirstMember  Group.h:252       GroupReference* GetFirstMember()
//   GetMapId        Position.h:281    uint32 GetMapId() const
//   CLASS_PALADIN 2, CLASS_PRIEST 5, CLASS_SHAMAN 7, CLASS_DRUID 11
//                   SharedDefines.h:127,130,132,136
// What hostile spawn data says about a spot: how many creatures hostile to
// this character and above `aboveLevel` are placed within `radius` yards of
// (x, y) on `mapId`, and the highest-level one, for the log line.
//
// Members read at the pinned core:
//   GetAllCreatureData      ObjectMgr.h:1235   CreatureDataContainer const&
//   GetCreatureTemplate     ObjectMgr.h:773    CreatureTemplate const* (uint32)
//   CreatureData::id        CreatureData.h:372 entry in creature_template
//   SpawnData mapid/posX/posY  SpawnData.h:69,71,72
//   CreatureTemplate Name/maxlevel/faction  CreatureData.h:192,197,199
//   GetFactionTemplateEntry Unit.h:853
//   sFactionTemplateStore   DBCStores.h:121
//   FactionTemplateEntry::IsHostileTo(FactionTemplateEntry const&)
//                           DBCStructure.h:1004
struct NearbyThreat
{
    uint32 count{0};
    uint32 level{0};
    std::string name;
};

static NearbyThreat HostileSpawnsNear(Player* bot, uint32 mapId, float x, float y,
                                      float radius, uint32 aboveLevel)
{
    NearbyThreat threat;
    FactionTemplateEntry const* mine = bot ? bot->GetFactionTemplateEntry() : nullptr;
    if (!mine)
        return threat;

    float const r2 = radius * radius;
    for (auto const& spawn : sObjectMgr->GetAllCreatureData())
    {
        CreatureData const& data = spawn.second;
        if (data.mapid != mapId)
            continue;
        float const dx = data.posX - x;
        float const dy = data.posY - y;
        if (dx * dx + dy * dy > r2)
            continue;
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(data.id);
        if (!tmpl || tmpl->maxlevel <= aboveLevel)
            continue;
        FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(tmpl->faction);
        if (!theirs || !theirs->IsHostileTo(*mine))
            continue;
        ++threat.count;
        if (tmpl->maxlevel > threat.level)
        {
            threat.level = tmpl->maxlevel;
            threat.name = tmpl->Name;
        }
    }
    return threat;
}

// The level the game gives the ground at a point: the sub-area's
// area_level, or its zone's when the sub-area carries none. 0 when unknown.
//
// Members read at the pinned core:
//   Map::GetAreaId(phaseMask, x, y, z)  Map.h:253
//   sAreaTableStore                     DBCStores.h:91
//   AreaTableEntry zone/area_level      DBCStructure.h:522,526
static int32 AreaLevelAt(Map* map, uint32 phaseMask, float x, float y, float z)
{
    if (!map)
        return 0;
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(map->GetAreaId(phaseMask, x, y, z));
    if (area && area->area_level <= 0 && area->zone)
        area = sAreaTableStore.LookupEntry(area->zone);
    return area ? area->area_level : 0;
}

// Why this character should not be revived at this graveyard, or empty
// when there is no reason. See GRAVEYARD_THREAT_RADIUS for the two tests.
//
//   FindBaseMap   MapMgr.h:44   Map* FindBaseMap(uint32 mapId) const
//   GetPhaseMask  Object.h:517  uint32 GetPhaseMask() const
static std::string GraveyardRefusal(Player* bot, GraveyardStruct const& g)
{
    uint32 const level = bot->GetLevel();
    Map* map = g.Map == bot->GetMapId() ? bot->GetMap() : sMapMgr->FindBaseMap(g.Map);
    int32 const areaLevel = AreaLevelAt(map, bot->GetPhaseMask(), g.x, g.y, g.z);
    if (areaLevel > int32(level))
        return "it stands on level " + std::to_string(areaLevel) + " ground and the character is level " +
               std::to_string(level);

    NearbyThreat const threat = HostileSpawnsNear(bot, g.Map, g.x, g.y, GRAVEYARD_THREAT_RADIUS, level);
    if (threat.count)
        return std::to_string(threat.count) + " hostile spawn(s) above level " + std::to_string(level) +
               " within " + std::to_string(uint32(GRAVEYARD_THREAT_RADIUS)) + " yards, up to '" +
               threat.name + "' (level " + std::to_string(threat.level) + ")";
    return {};
}

// The nearest graveyard this character can stand at: `nearest` itself when
// it passes, else the nearest of the core's own alternatives for this corpse
// that does, else null (the caller goes to the bind point). `refused`
// collects every refusal, so the log line can say why.
//
// Members read at the pinned core:
//   GetGraveyardData          GameGraveyard.h:62  GraveyardContainer const&
//   FindGraveyardData         GameGraveyard.h:61  GraveyardData const* (id, zone)
//   IsNeutralOrFriendlyToTeam GameGraveyard.h:42
//   GetTeamId                 Player.h:2142
//   Map::GetZoneAndAreaId     Map.h:255
static GraveyardStruct const* SafeGraveyardNear(Player* bot, GraveyardStruct const* nearest,
                                                uint32 corpseMap, float cx, float cy, float cz,
                                                std::string& refused)
{
    refused = GraveyardRefusal(bot, *nearest);
    if (refused.empty())
        return nearest;
    refused = "'" + nearest->name + "': " + refused;

    // The corpse's zone and area, because that is what graveyard_zone keys
    // its links by (GameGraveyard.cpp:145-160): a graveyard linked to the
    // corpse's area or zone, and neutral or friendly to the team, is one the
    // core itself would have offered; the nearest of them is the one it
    // picked.
    Map* map = corpseMap == bot->GetMapId() ? bot->GetMap() : sMapMgr->FindBaseMap(corpseMap);
    if (!map)
        return nullptr;
    uint32 zoneId = 0;
    uint32 areaId = 0;
    map->GetZoneAndAreaId(bot->GetPhaseMask(), zoneId, areaId, cx, cy, cz);

    std::vector<std::pair<float, GraveyardStruct const*>> candidates;
    for (auto const& entry : sGraveyard->GetGraveyardData())
    {
        GraveyardStruct const& g = entry.second;
        if (g.Map != corpseMap || g.ID == nearest->ID)
            continue;
        GraveyardData const* link = sGraveyard->FindGraveyardData(g.ID, areaId);
        if (!link)
            link = sGraveyard->FindGraveyardData(g.ID, zoneId);
        if (!link || !link->IsNeutralOrFriendlyToTeam(bot->GetTeamId()))
            continue;
        float const dx = g.x - cx;
        float const dy = g.y - cy;
        candidates.emplace_back(dx * dx + dy * dy, &g);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });

    std::size_t tried = 0;
    for (auto const& candidate : candidates)
    {
        if (tried++ >= GRAVEYARD_CANDIDATES)
            break;
        GraveyardStruct const* g = candidate.second;
        std::string const why = GraveyardRefusal(bot, *g);
        if (why.empty())
            return g;
        refused += "; '" + g->name + "': " + why;
    }
    return nullptr;
}

static Player* LivingResurrectorFor(Player* dead)
{
    Group* group = dead ? dead->GetGroup() : nullptr;
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == dead || !member->IsAlive())
            continue;
        if (member->GetMapId() != dead->GetMapId())
            continue;
        switch (member->getClass())
        {
            case CLASS_PRIEST:
            case CLASS_PALADIN:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return member;
            default:
                break;
        }
    }
    return nullptr;
}

// The living party member whose resurrect offer this character is sitting on,
// or null.
//
// There is no public getter for a pending offer's caster - m_resurrectGUID is
// private (Player.h:2909) and reachable only through the two predicates beside
// it - so the caster is found the way the dungeon module finds it
// (DcFollowerActions.cpp:1730), by asking each group member whether the offer
// names it.
//
// ALIVE AND IN THE GROUP IS THE WHOLE TEST, and the "alive" half is not
// bookkeeping. Accepting resurrects the character where the caster stood
// (ResurectUsingRequestData, Player.cpp:13119-13120), so an offer whose caster
// has since died would put this character down exactly where that caster went
// down. An offer with nobody alive behind it is not one worth taking.
//
// Members read at the pinned core:
//   isResurrectRequested    Player.h:1856   bool isResurrectRequested() const
//   isResurrectRequestedBy  Player.h:1855   bool (ObjectGuid) const
//   GetGroup                Player.h:2520   Group* GetGroup()
//   GetFirstMember          Group.h:252     GroupReference* GetFirstMember()
static Player* ResurrectOfferedBy(Player* dead)
{
    if (!dead || !dead->isResurrectRequested())
        return nullptr;

    Group* group = dead->GetGroup();
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == dead || !member->IsAlive())
            continue;
        if (dead->isResurrectRequestedBy(member->GetGUID()))
            return member;
    }
    return nullptr;
}

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

// The travel backstop's whole rule in one place: what the reading means, how
// much better a reading has to be before it counts, and how long without one
// is long enough. See OverseerDecisions::Ratchet, which four drives now share
// rather than each keeping a copy of this shape.
constexpr OverseerDecisions::RatchetLimits TRAVEL_RATCHET{
    OverseerDecisions::RatchetReading::DistanceToTarget,
    TRAVEL_PROGRESS_YARDS, TRAVEL_BACKSTOP_SECONDS};

// FLIGHT (#68). Five constants, and every one of them is a threshold on a
// decision that did not exist before: whether to fly to an errand instead of
// walking to it.
//
// HOW FAR A WALK HAS TO BE BEFORE FLYING IS EVEN CONSIDERED. A flight is not
// free - the character walks to a flight master first, waits out the ride, and
// lands at a node that is rarely on top of where it was going - so a short
// errand is always quicker on foot. Measured against the trip this issue was
// opened about: Lakeshire (Redridge) to Sentinel Hill (Westfall) is about
// 3,400 yards overland and one direct taxi hop. Set well below that and well
// above the errands that are already fine on foot: a dungeon staging point is
// tens of yards, a trainer across a zone is a few hundred.
constexpr float TRAVEL_FLIGHT_MIN_YARDS = 1500.0f;

// HOW MUCH SHORTER THE FLIGHT-ADJUSTED TRIP HAS TO BE. The comparison is not
// "is there a flight" but "does flying land me nearer", which is the
// flight-adjusted distance this issue asks for: walking to the departure
// flight master, plus walking from the arrival node to the destination, versus
// walking the whole way. A margin on top of that keeps the decision away from
// the case where the two are within noise of each other and the ride, the
// dismount and the fare buy nothing.
constexpr float TRAVEL_FLIGHT_SAVING_YARDS = 500.0f;

// HOW FAR A TAXI NODE MAY BE FROM THE FLIGHT MASTER THAT STANDS FOR IT. Two
// separate lookups have to agree before a flight is issued: the nearest taxi
// node of the character's OWN team (sObjectMgr) and the nearest flight master
// SPAWN of any team (this module's travel index). In a contested zone those
// can be different places - the nearest flight master may be the other side's,
// whose node the character can never use - and issuing a flight from a node no
// creature here answers for is a character walking to a stranger and standing
// there. When they disagree by more than this, refuse.
constexpr float TRAVEL_FLIGHT_NODE_MATCH_YARDS = 100.0f;

// HOW LONG THE FLIGHT LEG MAY RUN WITHOUT THE CHARACTER LEAVING THE GROUND.
// Upstream's RPG_TRAVEL_FLIGHT has no timeout of its own: NewRpgAction only
// leaves that status when a flight ENDS (NewRpgAction.cpp, the `inFlight &&
// !IsInFlight()` transition), so a character that sets off for a flight master
// it cannot reach stays in it forever. The errand's own twenty-minute backstop
// cannot catch that either, because the walk to the flight master legitimately
// moves AWAY from the destination and would look like a stall. So the leg is
// bounded here instead, and generously: a flight master is usually a few
// hundred yards away, and this is many multiples of that.
constexpr time_t TRAVEL_FLIGHT_BACKSTOP_SECONDS = 10 * 60;

// HOW MANY FLIGHTS ONE ERRAND MAY SPEND. Without a bound this oscillates: land
// at a node, find the destination still far away, pick a flight from the node
// just landed at, and go round again. Two rather than one so a flight refused
// at the counter for something that was not true when it was decided - the
// character picked a fight on the way, most likely - is not the last word on
// the errand, and no more than two because a second hop that still has not
// landed the character near the destination is evidence the route is wrong,
// not that a third would fix it.
constexpr uint32 TRAVEL_FLIGHT_MAX_PER_ERRAND = 2;

// How long travel keeps the wheel after an errand ends (PR #2840 review).
// OnUpdate runs DriveQuests BEFORE DriveTravel, so without a grace the poll
// that releases an arrived errand can be followed immediately by a
// ChangeToDoQuest that wipes the WanderNpc state the bot is still standing in -
// and upstream wants npcStayTime = 8 seconds at the NPC (NewRpgAction.h:99)
// before it counts as having been there. Longer than one QUEST_POLL_MS plus
// that dwell, so the last poll of the errand and the first poll of the quest
// drive can never be the same poll. See TravelHoldsTheWheel.
constexpr time_t TRAVEL_HANDBACK_SECONDS = 45;

// THE GROUND UNDER AN AIM (#138). Four numbers, and the whole argument for
// them is in the GroundedStep section beside the escort code.

// HOW FAR A CHARACTER MAY BE SENT IN A STRAIGHT LINE across ground the navmesh
// has no route over. Under pathFinderDis (70, NewRpgBaseAction.h:68), which is
// the distance inside which MoveFarTo stops routing and walks straight at
// whatever it was handed (NewRpgBaseAction.cpp:117-120) - so a step this long
// is one the mover takes as a single straight walk, which is exactly the walk
// the ground check has just made in software. It is most of one TRAVEL_POLL_MS
// of running, so a stepped walk is slower than a routed one and not by much,
// and a stepped walk only ever happens where the alternative was a fall.
constexpr float TRAVEL_STEP_YARDS = 60.0f;

// HOW OFTEN THE SURFACE IS SAMPLED ALONG SUCH A STEP. The core's own smooth
// path is built on the same interval (SMOOTH_PATH_STEP_SIZE, PathGenerator.h:
// 38) for the same reason: nothing worth walking around fits between two
// samples this close together.
constexpr float TRAVEL_GROUND_SAMPLE_YARDS = 4.0f;

// HOW FAR THE SURFACE MAY FALL BETWEEN TWO SAMPLES before the step is refused.
// Under MIN_FALL_DMG_DIST (13.48, Player.cpp:14175), the height below which the
// core charges nothing for a fall - so a drop this check ALLOWS is one the
// character can take without being hurt, and a drop it refuses is one that
// would cost health. Measured from the LAST sample rather than from the start
// of the step, so a long walk downhill is not mistaken for a cliff: what is
// being refused is a single step into thin air, which is what a cliff is.
constexpr float TRAVEL_GROUND_DROP_YARDS = 10.0f;

// HOW FAR AN AIM'S OWN Z MAY BE CORRECTED onto the surface under it before the
// correction is refused as relocating the aim rather than grounding it. Small
// on purpose: an areatrigger's coordinates are the middle of its box, and the
// handler that answers the knock measures the character against that box in
// three dimensions (Player::IsInAreaTriggerRadius, Player.cpp:2216-2236), so a
// correction of a few yards keeps a character inside the doorway it is being
// walked onto while a correction of twenty would put it under the floor.
constexpr float TRAVEL_GROUND_SNAP_YARDS = 5.0f;

// How high over the last footing the ground is looked for a SECOND time, when
// looking from TRAVEL_GROUND_DROP_YARDS up found nothing. Fifty because that is
// the reach SurfaceAt already describes as the point where a drop stops being
// legible, so this asks the same primitive over its whole useful range rather
// than inventing a second, shorter one. See GroundHolds for why not finding
// ground and standing over a cliff are different answers.
constexpr float TRAVEL_GROUND_UPHILL_YARDS = 50.0f;

// How much of the remaining distance an ANGLED probe walks. Six tenths because
// the widest bearing kept (sixty degrees) then lands
// sqrt(1 + 0.36 - 0.6) = 0.87 of the way, comfortably inside the
// forward-progress rule, while a probe at the full remaining distance lands
// exactly ON it and is rejected. See the fan in GroundedStep.
constexpr float TRAVEL_STEP_SIDE_FRACTION = 0.6f;

// A follower that has stopped, and never closes the gap (#70).
//
// Two followers stopped at a zone border on the dev world 2026-08-30 and
// sat at the same coordinates for eighteen minutes, jittering ONE TO EIGHT
// YARDS, while the leader kept walking and the 265-yard gap to the rest of
// the party never closed. See KeepRosterFollowing's own stall check below
// for why this drive - not UpdateAIGroupMaster, not the stuck triggers -
// is the only thing left that could have noticed.
//
// JITTER BOUND. Wider than TRAVEL_PROGRESS_YARDS because the measured
// jitter (one to eight yards) is the noise floor this has to clear without
// tripping on a follower that is genuinely closing the gap one step at a
// time.
constexpr float FOLLOW_STALL_JITTER_YARDS = 10.0f;

// HOW LONG BEFORE A STANDING FOLLOWER COUNTS AS STUCK RATHER THAN
// REPATHING. Shorter than the measured eighteen minutes on purpose - the
// point is to act long before a stall reaches that severity - and several
// multiples of PARTY_POLL_MS (30s) so a single slow path search or a normal
// pause at a corner is never mistaken for a stall.
constexpr time_t FOLLOW_STALL_SECONDS = 5 * 60;

// How far behind counts as "should already be closing, and is not".
// AiPlayerbot.SightDistance (default: PlayerbotAIConfig.cpp:109) is the
// exact distance at which upstream's own Follow() stops chasing
// continuously and switches to a single discrete MoveTo per attempt
// (MovementActions.cpp:1180-1224) - past this line the follower depends on
// that single attempt actually landing, with nothing native to notice if it
// does not.
constexpr float FOLLOW_STALL_GAP_YARDS = 100.0f;

// The follower-stall ratchet. DistanceFromLastMark and not DistanceToTarget:
// a follower is not being sent anywhere, so there is nothing for it to get
// nearer to and the reading is how far it has moved from where it was last
// seen moving. FOLLOW_STALL_GAP_YARDS is deliberately NOT in here - it is an
// extra condition on this site's reaction, not part of what counts as
// progress: a follower right beside its leader may stand still all it likes.
constexpr OverseerDecisions::RatchetLimits FOLLOW_STALL_RATCHET{
    OverseerDecisions::RatchetReading::DistanceFromLastMark,
    FOLLOW_STALL_JITTER_YARDS, FOLLOW_STALL_SECONDS};

// A FOLLOWER TOO FAR BEHIND TO BE FOLLOWING AT ALL (#138).
//
// Past AiPlayerbot.SightDistance (100, PlayerbotAIConfig.cpp:109) upstream's
// Follow() stops chasing and issues one MoveTo(target, followDistance) per
// attempt (MovementActions.cpp:1224). That MoveTo is a single straight step of
// at most AiPlayerbot.SpellDistance (28.5, PlayerbotAIConfig.cpp:110) toward
// the master, with its Z interpolated toward the MASTER'S Z before it is
// clamped to the ground under the step (MovementActions.cpp:791-796). It knows
// nothing about the navmesh between the two of them: a master on top of a
// cliff pulls a follower at its foot straight at the rock face, a step at a
// time, forever. Measured 2026-09-01: three followers self-killed at one
// coordinate within twenty-four seconds while the leader was thousands of
// yards away, and five more earlier that hour at a zone edge.
//
// DISTANCE IS NOT THE ONLY REASON ANY MORE (#138, reopened). A follower past
// FOLLOW_STALL_GAP_YARDS whose next straight step goes over a drop is walked
// too, however short the gap - see FollowStepHolds and DriveCatchUp. This
// number is still what "stranded" means; it is no longer the only thing that
// makes following dangerous.
//
// So beyond this distance a follower is not following, it is stranded, and
// the drive stops pretending otherwise: it is given the leader's position as
// its OWN travel aim - the dungeon-run escort that already walks a member
// across open ground under `new rpg` - and MoveFarTo routes it there along
// the navmesh like any other errand. Five hundred because that is what the
// issue's own acceptance criterion measures against, and because it is five
// times SightDistance: a follower a hundred yards back is being chased one
// step at a time and usually closes, one five hundred yards back is past
// anything upstream will do for it.
constexpr float FOLLOW_CATCH_UP_YARDS = 500.0f;

// ...AND HOW CLOSE IT HAS TO GET BACK before it is handed back to `follow`.
// FOLLOW_STALL_GAP_YARDS on purpose - SightDistance is the exact line inside
// which upstream's Follow() chases continuously again (MovementActions.cpp:
// 1180-1224), so this is where following starts working, not a guess. Well
// under FOLLOW_CATCH_UP_YARDS so a follower on the line does not flap between
// the two drives every poll.
constexpr float FOLLOW_CATCH_UP_DONE_YARDS = FOLLOW_STALL_GAP_YARDS;

// How far the leader may walk from the point a catching-up follower was aimed
// at before the aim is refreshed. Half of FOLLOW_CATCH_UP_DONE_YARDS, and the
// arithmetic is the reason: a follower that ARRIVES at its aim holds there
// (see DriveTravel's escort arrival branch), and the hand-back above needs it
// within FOLLOW_CATCH_UP_DONE_YARDS of the leader to fire. With the aim never
// more than this far from the leader, arriving at it always lands inside that
// radius, so the escort cannot end with the follower holding still out of
// `follow`'s reach. Any larger and a leader who stopped just past the line
// would leave the follower standing at a stale point forever.
constexpr float FOLLOW_CATCH_UP_REAIM_YARDS = FOLLOW_CATCH_UP_DONE_YARDS / 2.0f;

// Upstream's own fuse on MoveFarTo's stuck teleport: `stuckTime`, 90 seconds
// (NewRpgBaseAction.h:76). Named here so the log line that reports the
// teleport being kept out of reach can say what it was kept from, and so a
// pin bump that changes it has one place to change. See the stuck-teleport
// block in DriveTravel for what is done with it.
constexpr uint32 UPSTREAM_MOVE_FAR_STUCK_SECONDS = 90;

// WHAT AN ESCORT TAKES OFF A CHARACTER WHILE IT IS TRAVELLING (#164).
//
// TWO MEASUREMENTS, TWELVE HOURS APART, AND THE SECOND ONE IS WHY THIS IS NOT
// A TIDINESS LIST. On 2026-09-02 a member of a staging party stood 500 yards
// from the instance door casting Herb Gathering, indefinitely, while BARRIER
// held for him and the other four waited at the door; the module had just
// handed him `new rpg` itself, exactly as the escort is designed to, and
// `gather` and `loot` were still on the engine beside it, so the walk to the
// dungeon became ordinary open-world time. That looked like a politeness
// problem. The same night it killed somebody: the LEADER, under a staging aim,
// was held on the spot by GroundedStep because every direction out of where he
// stood was a drop - the log says so in those words - and `grind`, granted
// seconds earlier by an upkeep cycle that had no idea a run was staging, walked
// him off that drop anyway. He was found at z = -511, through the floor, killed
// by himself.
//
// SO THIS IS A SAFETY SET. Every walk this module issues goes through
// GroundedStep, and GroundedStep is only ever as strong as the guarantee that
// nothing ELSE is steering the character. A second strategy with a destination
// of its own is that guarantee failing.
//
// THE CRITERION, AND IT IS ABOUT THE NON-COMBAT ENGINE ONLY: a strategy is here
// if it can send the character somewhere the escort did not choose, or start a
// fight or a duel that stops it walking. The escort's own walk is "new rpg
// wander npc" at relevance 3.0 (NewRpgStrategy.cpp:45-52), so anything above
// that which fires simply wins the tick.
//
//   gather          "timer" -> "add gathering loot" 5.0
//                   (LootNonCombatStrategy.cpp:19-23)
//   loot            "loot available" -> "loot" 6.0, "far from loot target" ->
//                   "move to loot" 7.0, "can loot" -> "open loot" 8.0
//                   (LootNonCombatStrategy.cpp:10-17). `gather` is what puts a
//                   herb or ore node INTO the loot stack and `loot` is what
//                   WALKS to it, so taking one and leaving the other leaves
//                   half of the measured behaviour in place. They are one
//                   diverter with two names.
//   quest           "gossip hello" / "use game object" / "complete quest" ->
//                   "talk to quest giver", "quest share" -> "accept quest
//                   share" (QuestStrategies.cpp:12-30) - it turns arriving
//                   anywhere near a quest giver into an accepted objective.
//   grind           "no target" -> "attack anything" 4.0
//                   (GrindingStrategy.cpp:18-29). The one that killed the
//                   leader. Note this is the NON-combat engine: the strip
//                   DriveEngagementSafety already does for an unaccompanied
//                   character is on the combat engine and is a different
//                   question with a different answer.
//   rpg             "no rpg target" -> "choose rpg target" 5.0, "far from rpg
//                   target" -> "move to rpg target" 5.0 (RpgStrategy.cpp:34-59)
//                   - an entire second traveller with a destination of its own.
//   travel          "no travel target" -> "choose travel target" 6.0
//                   (TravelStrategy.cpp:19-37) - likewise, and it picks the
//                   destination itself.
//   move random     "often" -> "move random" 1.5 (GrindingStrategy.cpp:31-41).
//                   BELOW the walk's own relevance rather than above it, and
//                   here anyway: it exists to move the character for no reason,
//                   which is precisely what a walk to a door is not for, and
//                   the walk is not on the table on every tick (MoveFarTo lets
//                   a committed move run, NewRpgBaseAction.cpp:57-83).
//   master fishing  "very often" -> "move near water" 10.0
//                   (NonCombatStrategy.cpp:37-46).
//   pvp             "enemy player near" -> "attack enemy player" 55.0
//                   (AttackEnemyPlayersStrategy.cpp:10-14).
//   duel            "duel requested" -> "accept duel", "no attackers" ->
//                   "attack duel opponent" 70.0 (DuelStrategy.cpp:9-19).
//
// WHAT IS DELIBERATELY LEFT ON, because an escort is a character walking and
// not a character switched off. `food` stops it for a few seconds and keeps it
// alive; `mount` and `collision` help it travel rather than divert it; `flee`,
// `cure` and the class buffs are about surviving whatever finds it on the way;
// `emote`, `chat` and `default` move nobody; `follow` and `new rpg` are how the
// escort works at all, and taking either is the bug this file already has three
// comments about. `explore`, `map`, `map full` and `attack tagged` register no
// triggers of their own at all (TravelStrategy.h:25-47,
// NonCombatStrategy.h:43-50) - they are flags other code reads - so there is
// nothing in them to stand down. `lfg` is the near miss: it can put a character
// into a queue that would teleport it out of the escort entirely
// (LfgStrategy.cpp:10-17), but it moves nobody by itself, this module walks a
// party through the door rather than using the finder, and this family does not
// carry it - so it is named here rather than stripped on a guess.
//
// EVERY NAME HERE IS PUT BACK. See TravelFocus for how, and for why the restore
// is provable rather than merely intended.
constexpr char const* ESCORT_DIVERT_STRATEGIES[] = {
    "gather", "loot", "quest", "grind", "rpg",
    "travel", "move random", "master fishing", "pvp", "duel",
};

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

// How often the gear drive sweeps (#145). Deliberately the dungeon-run
// cadence rather than a slower one of its own, for two reasons that both come
// off a stopwatch: a group loot roll is open for exactly sixty seconds
// (AzerothCore Group.cpp:1356 passes 60000 to SendLootStartRollToPlayer), so a
// drive that means to have an opinion about it has to look several times
// inside that window; and the moment worth acting on is the one right after a
// win, when the thing the character just fought for is sitting in its bags.
//
// WHAT A PASS COSTS, so the cadence is a choice and not an accident. Five
// characters, each with somewhere under sixty carried items, each scored by a
// pure function that does a few dozen multiplications and builds one short
// string. No database query, no pathfinding, no packet. This is the cheapest
// drive in the file and it runs at the same rate as the most expensive one.
constexpr uint32 GEAR_POLL_MS = 5000;

// How long a dungeon run's heartbeat may go cold before it is considered over.
// The arming drive touches it on every pass while anyone from the roster is
// inside, so a cold heartbeat means nobody has been seen in there.
//
// GENEROUS ON PURPOSE. Closing a run early re-opens the very stranding the run
// record exists to prevent - every drive resumes steering a character who is
// still inside an instance. Closing late costs only a finished row lingering,
// which nothing suffers from. Two minutes is many arming passes.
constexpr uint32 RUN_COLD_SECONDS = 120;

// How often the dungeon run coordinator polls (mod-overseer#88, "the party
// owns the run"). Faster than QUEST_POLL_MS on
// purpose: the failure this closes is "Bork and Grog reached the tank's
// corpse first, pulled a Defias Miner and an Evoker with no healer in range,
// and both died. Ugga was still walking" (epic #88) - a party that has
// already lost members while the slowest one is still 900 yards out. The
// barrier has to notice "everyone is here" within a few seconds of it
// becoming true, not within the twenty seconds the quest drive can tolerate
// for an errand that is not safety-critical.
constexpr uint32 DUNGEON_RUN_POLL_MS = 5000;

// How close counts as "at the staging point" for BARRIER, and how close
// counts as "close enough to each other" for the barrier predicate itself.
// Ten yards because that is the figure the epic's six independently-converged
// designs all named for the gather radius - none of them is a WoW-native
// constant the way INTERACTION_DISTANCE is (see TRAVEL_ARRIVED_POSITION_YARDS
// above), so this is that consensus number, not a measurement.
constexpr float DUNGEON_BARRIER_RADIUS_YARDS = 10.0f;

// THE STAGING WATCHDOG'S TWO NUMBERS (#164). Both are taken off measurements
// this module already made rather than chosen for feel, and both are written
// down here with what they were taken from.
//
// HOW MUCH CLOSER COUNTS AS CLOSING. FOLLOW_STALL_JITTER_YARDS, which is ten,
// and it is the same constant rather than a second one that happens to have the
// same value: it is the measured noise floor of a character that is standing
// still, one to eight yards of jitter over eighteen minutes (#70), and a stall
// detector that cannot clear its own noise floor reports stalls that are not
// there. A member that is genuinely walking beats it easily - the travel
// backstop's own measurement is 112 yards a minute (2347 yards walked before it
// was wrongly released; see the ratchet comment in overseer_decisions.h), which
// is about nine yards per DUNGEON_RUN_POLL_MS poll and so past ten within two
// of them - and the ratchet only has to be beaten ONCE to restart the clock.
constexpr float DUNGEON_STAGING_STALL_YARDS = FOLLOW_STALL_JITTER_YARDS;

// HOW LONG WITHOUT CLOSING IS LONG ENOUGH. Ninety seconds, and the number is
// borrowed rather than picked: it is UPSTREAM_MOVE_FAR_STUCK_SECONDS above,
// which is upstream's own `stuckTime` (NewRpgBaseAction.h:76) - the interval
// after which MoveFarTo concludes that a walk it is driving is not getting
// anywhere. This module deliberately holds that fuse open on every travel poll
// so upstream's recovery, a teleport onto the destination, can never fire (see
// the stuck-teleport block in DriveTravel). Taking upstream's judgement away and
// then never making one of its own is exactly how a character stands at a herb
// node for a whole staging window, so the judgement is made here instead, on
// upstream's own timescale.
//
// IT IS COMFORTABLY LONGER THAN EVERY LEGITIMATE PAUSE that is not already held
// out of the reading. Combat, flight, death and a wrong map hold the clock
// rather than run it - see RunStagingWatchdog - which leaves eating, thirty
// seconds for a full food tick, as the longest ordinary stand-still, and that
// fits three times inside this. It is also eighteen DUNGEON_RUN_POLL_MS polls,
// so no single slow poll can trip it.
//
// AND IT IS MUCH SHORTER THAN FOLLOW_STALL_SECONDS (five minutes) ON PURPOSE.
// That one bounds a follower nobody is waiting for. This one bounds a whole
// party standing at a door while one member botanises, and the stall it was
// written for lasted many minutes with the character stationary at a herb node.
constexpr time_t DUNGEON_STAGING_STALL_SECONDS = UPSTREAM_MOVE_FAR_STUCK_SECONDS;

// The staging watchdog's ratchet. DistanceToTarget and not DistanceFromLastMark,
// which is the difference between this and the follower stall it borrows its
// numbers from: a staged member IS being sent somewhere, and the question
// BARRIER could never answer is whether it is getting NEARER to that somewhere.
// "Has it moved at all" is the wrong question here - a character circling a herb
// node moves plenty.
constexpr OverseerDecisions::RatchetLimits DUNGEON_STAGING_RATCHET{
    OverseerDecisions::RatchetReading::DistanceToTarget,
    DUNGEON_STAGING_STALL_YARDS, DUNGEON_STAGING_STALL_SECONDS};

// HOW LONG A WHOLE RUN MAY SPEND GETTING STAGED (#165). A SECOND CLOCK, AND A
// DIFFERENT QUESTION FROM THE ONE ABOVE.
//
// The per-character ratchet asks "is THIS member coming", and it is answered
// wrongly, with perfect confidence, by a member that is not coming and never
// will: measured live, a run opened at 23:15:58 after a clean reset was still
// `active` and unstarted forty-three minutes later, because one member had
// walked through the door early and the barrier requires everybody within ten
// yards of the point OUTSIDE it. She was not far away, not in combat and not
// dead. She was past the finish line, and nothing about her distance was ever
// going to change. The same shape had appeared on the run before. Both were
// broken by an operator, which is the thing this module is supposed to make
// unnecessary.
//
// So the run gets a clock of its own, over RESETTING, GATHERING and BARRIER
// together - the three phases in which a party is being assembled. ENTER is
// left out because it has its own, tighter bound already
// (DUNGEON_CROSSING_BACKSTOP_SECONDS), and everything past it is a run that has
// started.
//
// TWELVE MINUTES, AND THE NUMBER IS DERIVED RATHER THAN CHOSEN. It is TWICE the
// per-character ladder's whole length: three corrections and a give-up, each
// given a full DUNGEON_STAGING_STALL_SECONDS, is six minutes, so a member that
// is corrected, starts moving, and then stalls again gets a SECOND complete
// ladder before the run it is holding up is written off. Doubling it rather
// than adding a margin is what makes that guarantee exact rather than
// approximate.
//
// AND IT SITS WHERE THE MEASUREMENTS SAY IT SHOULD: several times a healthy
// staging on this route, which takes a few minutes; comfortably under the
// twenty-minute travel backstop, so a run is not held hostage to one member's
// errand being declared unreachable; and a quarter of the forty-three minutes
// the two observed deadlocks actually ran.
//
// ERRING SHORT IS THE SAFE DIRECTION, which is the other half of why it is
// twelve and not twenty. The two mistakes are not symmetric. A run written off
// too early is closed with a reason on its row, counted against the campaign,
// and re-staged by the very next poll - from where the party now stands, which
// is nearer the door than where it started, so the retry is shorter than the
// attempt it replaces. A run written off too late is forty-three minutes of
// five characters standing still while a line that already knows what is wrong
// is printed at them. The number leans at the cheap mistake on purpose.
constexpr time_t DUNGEON_STAGING_BACKSTOP_SECONDS =
    2 * (OverseerDecisions::STAGING_NUDGE_STEPS + 1) * DUNGEON_STAGING_STALL_SECONDS;

// How close a character must be to the portal's own areatrigger before ENTER
// will knock on it for the whole party (mod-overseer#88).
//
// STRICTLY INSIDE THE SMALLEST TRIGGER THIS IS EVER AIMED AT, for exactly the
// reason TRAVEL_ARRIVED_POSITION_YARDS above is. Read out of the pinned core's
// own base world DB (data/sql/base/db_world/areatrigger.sql) rather than
// remembered: areatrigger 78, the Deadmines entrance, is
// `(78,0,-11208.5,1685.34,25.7612,7,0,0,0,0)` - radius 7, no box - and
// areatrigger 119, the way back out, is
// `(119,36,-14.3628,-393.38,64.5605,6,0,0,0,0)` - radius 6. Five yards is
// inside both with room to spare, and is the same INTERACTION_DISTANCE the
// game itself uses for "close enough to act on a thing".
//
// THIS IS A GATE, NOT AN AUTHORITY, and the difference matters. Nothing here
// decides whether a character crosses: WorldSession::HandleAreaTriggerOpcode
// re-checks IsInAreaTriggerRadius itself (MiscHandler.cpp:716) and refuses
// anyone not genuinely standing in the trigger. What this number decides is
// when the party is bunched up tightly enough that knocking for ALL of them at
// once is worth doing, which is the whole content of ENTER.
constexpr float DUNGEON_DOORSTEP_RADIUS_YARDS = 5.0f;

// How long a crossing may make no progress before the run is given up (#88).
//
// ONE NUMBER FOR BOTH DOORS, because ENTER and EXIT are the same state twice.
// They differ in which areatrigger and which map is the far side, and in
// nothing else - so a party that can get in on a five minute bound and can only
// get out on some other one would be two behaviours to reason about where there
// is only one.
//
// KEYED ON PROGRESS, NOT ON ELAPSED TIME - the same lesson #63 taught the
// travel backstop, which used to release a character that was visibly walking
// toward its target simply because the journey was long. The clock restarts
// every time one more member actually crosses, so a party filing through the
// door one at a time is never given up on, while a party that has stopped
// crossing is. Five minutes is sixty DUNGEON_RUN_POLL_MS polls and far longer
// than closing the last DUNGEON_STAGING_STANDOFF_YARDS to the door can
// honestly take.
constexpr time_t DUNGEON_CROSSING_BACKSTOP_SECONDS = 5 * 60;

// The crossing ratchet. CountAchieved, with no margin: the reading is how many
// members are already through, one more is one more, and there is no jitter in
// a count for a margin to absorb. Zero is a real reading here rather than an
// unmeasured one, which is what keeps the clock the phase transition started
// running while nobody has crossed yet.
constexpr OverseerDecisions::RatchetLimits DUNGEON_CROSSING_RATCHET{
    OverseerDecisions::RatchetReading::CountAchieved,
    0.f, DUNGEON_CROSSING_BACKSTOP_SECONDS};

// How long CLEARING waits for `dc on` to be ACCEPTED before it says, out loud,
// that the run is being fought with open-world logic (#88, #140).
//
// THIS IS A REPORTING CLOCK, NOT A RETRY CLOCK. The coordinator never issues
// `dc on` - DriveDungeonClear owns that. What this bounds is how long "armed"
// may remain unverified before the failure stops being invisible, and
// invisibility is the whole problem: the module sat unarmed inside every run
// for its entire life while `delivered`, a startup context registration and
// then (#140) an auto-installed strategy each read as confirmation in turn.
//
// A minute is several arming passes and many DUNGEON_RUN_POLL_MS polls, and
// short enough that the line lands while the party is still on the first pull
// rather than after the wipe.
constexpr time_t DUNGEON_ARMING_GRACE_SECONDS = 60;

// How long a REFUSED `dc on` waits before it is issued again (#140).
//
// An accepted `dc on` is never re-issued for the rest of that stay inside:
// DcOnAction::Execute (DungeonClearChatActions.cpp:281-322) resets the whole
// transient run state on every accepted call - approach FSM, skipped set,
// cleared anchors, long-path cache - so a second `dc on` on a run that is
// under way is a run restarted from the door. A refused one resets nothing
// (it returns at :226, :250, :257 or :266, before any of that), so retrying a
// refusal is safe, and some refusals are transient: "is dead - rez and try
// again" (:265) clears itself the moment the revival drive lands. A minute is
// the same order as DUNGEON_ARMING_GRACE_SECONDS, so a refusal that heals is
// retried about once per grace rather than eight times a minute.
constexpr time_t DUNGEON_DC_ON_RETRY_SECONDS = 60;

// WHAT COUNTS AS THE RUN MOVING once `dc on` has been accepted (#140). The
// only thing this module can read off the command is that DcOnAction returned
// true; it cannot see the run's `enabled` flag (that lives on the leader's
// value context behind headers this module deliberately does not include).
// What it CAN see is the leader's position. A run the dungeon module is
// driving walks its tank away from the door toward the first boss; a run it
// is not driving stood at the Deadmines entrance for six hours at full health
// with the strategy installed. Thirty yards is more than the party mills about
// at the door and less than the walk to the first pull; two minutes is the
// issue's own acceptance window and covers the module's own start-up (it
// builds a long path before the first step).
constexpr float DUNGEON_DC_ON_MOVE_YARDS = 30.f;
constexpr time_t DUNGEON_DC_ON_MOVE_WINDOW_SECONDS = 120;

// A RUN THAT HAS STOPPED, INSIDE (#171).
//
// WHAT WAS WATCHED. All five inside a freshly reset Deadmines, healthy,
// together, and motionless for about twenty-six minutes. The dungeon module
// said exactly what was wrong every five seconds for the whole of it:
//
//     [DC:Grug] event 'Iron Clad Door (Defias Cannon)' step 1 kind 11 result 1
//               elapsed 1587558ms timeout 30000ms
//
// Elapsed 1,587,558 ms against its own stated timeout of 30,000 ms: the step ran
// FIFTY times past its own deadline and the deadline never fired. `dc skip`
// cleared it instantly - the party moved from (-106,-665) to (-79,-726) and the
// tank issued a 142 yard path and carried on - so the designed remedy existed
// the whole time and nothing invoked it. Two stream frames ten seconds apart
// were pixel identical; a person spotted it, which is the thing this module is
// for.
//
// HOW FAR THE TANK HAS TO GO TO COUNT AS MOVING. DUNGEON_DC_ON_MOVE_YARDS, the
// constant chosen one state earlier for exactly this question and measured for
// it: "more than the party mills about at the door and less than the walk to the
// first pull". A tank clearing a dungeon covers it many times over between
// polls; a tank standing at a cannon never does. One number, one meaning.
constexpr float DUNGEON_CLEAR_STALL_YARDS = DUNGEON_DC_ON_MOVE_YARDS;

// HOW LONG WITHOUT MOVING IS A STALL RATHER THAN AN OBJECTIVE IN PROGRESS.
//
// Every legitimately slow thing is HELD OUT of the reading rather than absorbed
// by the number - combat, looting, sitting to eat or drink, anybody dead or
// waiting on a resurrect all hold the clock, see RunClearingWatchdog - so what
// this bounds is the one thing left that can keep a tank inside thirty yards
// with none of those true: a scripted objective. A healthy one on this route
// resolves in seconds to a couple of minutes.
//
// FIVE MINUTES IS TEN TIMES THE OBJECTIVE'S OWN STATED DEADLINE. The dungeon
// module prints `timeout 30000ms` beside every step, so a factor of ten is the
// margin that says this module is not second-guessing a slow objective but
// noticing a dead one. The observed stall ran fifty times past that deadline. It
// is also more than twice the longest healthy objective, a fifth of the
// twenty-six minutes the operator watched, and sixty DUNGEON_RUN_POLL_MS polls,
// so no single slow poll can trip it.
constexpr time_t DUNGEON_CLEAR_STALL_SECONDS = 5 * 60;

// The CLEARING watchdog's ratchet. DistanceFromLastMark, which is the follow
// stall's reading rather than the staging watchdog's: a clearing party is not
// being sent anywhere this module knows about - where the next boss is, is the
// dungeon module's knowledge - so the only question available is whether the
// tank has got anywhere AT ALL since it was last seen going somewhere.
constexpr OverseerDecisions::RatchetLimits DUNGEON_CLEAR_RATCHET{
    OverseerDecisions::RatchetReading::DistanceFromLastMark,
    DUNGEON_CLEAR_STALL_YARDS, DUNGEON_CLEAR_STALL_SECONDS};

// HOW MANY OBJECTIVES A RUN MAY ABANDON BEFORE IT IS ITSELF ABANDONED.
//
// Three, and each gets a whole DUNGEON_CLEAR_STALL_SECONDS to show that it
// worked, so a run is given fifteen minutes of the designed remedy before
// anything heavier happens to it. Deadmines has seven encounters: a run that has
// skipped three consecutive objectives and still moved nowhere is not a run
// being unblocked one obstacle at a time, it is a run that skipping does not
// reach - which is the case the operator asked to be ENDED rather than narrated,
// so the next poll can open a fresh one.
constexpr unsigned DUNGEON_CLEAR_SKIPS = 3;

// ------------------------------------------------- the run goes again (#144) --
//
// HOW MANY TIMES THE RESET MAY BE ASKED FOR BEFORE THE RUN IS WRITTEN OFF.
//
// A reset is not a request that gets queued and eventually honoured: the core
// decides it on the spot. Group::ResetInstances (Group.cpp:2328 at the pinned
// core) hands each bound save to InstanceMap::Reset (Map.cpp:2210), which
// returns m_mapRefMgr.IsEmpty() (Map.cpp:2249) - so an instance with anybody
// still standing in it refuses, once, immediately, and tells everyone inside
// (SendResetFailedNotify, Map.cpp:2240). Retrying is therefore only worth
// anything when the world has changed between attempts, which is why the
// PRECONDITIONS get a generous clock of their own below and the ATTEMPTS get a
// small count: three refusals in a row from a party this coordinator believes
// is entirely outside means the refusal is not about the party.
//
// AND THREE RATHER THAN ONE, because the coordinator's census and the map's own
// reference list can disagree for a tick. The census asks each member for
// GetMapId(), which a character that has just been teleported out can answer
// with its DESTINATION while it is still linked to the map it left; the map
// answers HavePlayers() from that link. One poll of slack absorbs that; three
// costs fifteen seconds and removes the whole class of spurious first refusal.
constexpr uint32 DUNGEON_RESET_ATTEMPTS = 3;

// How long the reset waits for its own preconditions - everybody out of the
// instance, nobody in combat - before giving up on them.
//
// THE SAME FIVE MINUTES THE CROSSING BACKSTOP USES, and for the same reason
// stated there: it is far longer than the thing can honestly take, and a
// coordinator that waits forever for a condition that has stopped changing is
// the failure shape this file has now paid for three times. A party that has
// just walked out of a door and is standing on the doorstep is out of combat
// within seconds; five minutes covers a straggler finishing a fight with
// something that followed them out.
constexpr time_t DUNGEON_RESET_BACKSTOP_SECONDS = 5 * 60;

// How many consecutive failed runs end the campaign (#144: "the loop stops ...
// after 3 consecutive runs that did not complete, and says which").
//
// WHAT THIS ACTUALLY COUNTS, WHICH IS NARROWER THAN THE ISSUE'S WORDING. "Did
// not complete" needs a notion of completion, and this module has none: a run's
// goal is #143 and is not built. What it can prove is a reset that will not
// take, and that failure is genuinely terminal in a way a bad run is not - the
// instance still has every boss dead in it, so the fourth attempt would walk
// into the same empty mine as the first three. Counted from the run rows rather
// than from memory so it survives a worldserver bounce.
constexpr uint32 DUNGEON_CAMPAIGN_CONSECUTIVE_FAILURES = 3;

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

// The quest re-pick ratchet, and the one whose patience is NOT a clock. Same
// reading as the follower stall - how far it has got from a mark dropped where
// the streak began - and a patience of zero, which says the caller counts:
// this site gives up after QUEST_REPICK_STRIKES consecutive picks that went
// nowhere, not after a number of minutes. Strikes and minutes are not the same
// bound (a bot on a long route stays in RPG_DO_QUEST and never reaches this
// code at all, so the polls that strike it are not evenly spaced), and turning
// one into the other to make this list read evenly would be a behaviour change
// wearing a refactor's clothes.
constexpr OverseerDecisions::RatchetLimits QUEST_REPICK_RATCHET{
    OverseerDecisions::RatchetReading::DistanceFromLastMark,
    QUEST_PROGRESS_YARDS, 0};

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
// How long a refused give is left alone before it is tried again
// (mod-overseer#169). Thirty COMMAND_POLL_MS drains, which is the whole point:
// the sender re-inserts a give it has not seen succeed, so without this the
// retry cadence is the queue's rather than the world's, and nothing a
// hand-over waits on - room in a bag, a fight ending - resolves anywhere near
// that fast. Short enough that a give unblocked by hand is moving again within
// a minute; see OverseerDecisions::GiveHeldOff for why this is a backoff and
// not a give-up.
constexpr time_t GIVE_REFUSAL_BACKOFF_SECONDS = 60;
// ...and when the memory of a refusal is thrown away entirely, so a book of
// gives that were asked once and never again cannot grow for the life of the
// process. Well past the backoff: forgetting inside it would defeat it.
constexpr time_t GIVE_REFUSAL_FORGET_SECONDS = 30 * 60;
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
    // Resolved once on the world thread in ReloadWatchList so that the chat
    // hooks, which run on map threads, never have to resolve anything.
    ObjectGuid guid;
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

// Hand every watcher that wants this kind to `fn`, as a GUID and a name.
//
// WHY NO Player* IS RESOLVED HERE, WHICH IS THE WHOLE POINT (#125).
// This runs on whatever thread the speech happened on - for a bot, a map
// update thread, and there is more than one of those. The core's own header
// says this about the call this function used to make:
//
//     // these functions return objects if found in whole world
//     // ACCESS LIKE THAT IS NOT THREAD SAFE
//     Player* FindPlayerByName(std::string const& name, bool checkInWorld = true);
//
// and it means it literally. FindPlayerByName resolves through
// PlayerNameMapHolder, a bare static std::unordered_map with NO lock at all -
// unlike HashMapHolder immediately above it in the same file, which takes a
// shared_mutex on every operation. The distinction is deliberate and it is a
// trap. Meanwhile ObjectAccessor::RemoveObject erases from that same unlocked
// map and Map::DeleteFromWorld then does `delete player`, and both run on a
// map thread because mod-playerbots logs bots out from inside the AI tick.
// One map thread walking the bucket chain while another erases from it and
// frees the Player is a data race AND a use-after-free, and it was killing
// this worldserver every few minutes with a bare SIGSEGV and no backtrace.
//
// So the rule is now: the chat hot path never touches a global player
// container. Callers get a GUID and answer audibility from it. Where a caller
// genuinely needs the object, it resolves through the map-scoped
// ObjectAccessor::GetPlayer(WorldObject const&, guid), which the same header
// lists under "these functions return objects only if in map of specified
// object" - that is, the map the calling thread already owns.
template <typename F>
void ForEachWatcher(uint32 kind, F&& fn)
{
    std::lock_guard<std::mutex> guard(g_watchMutex);
    for (WatchEntry const& entry : g_watch)
    {
        if (!(entry.channels & kind))
            continue;
        if (entry.guid.IsEmpty())
            continue;
        fn(entry.guid, entry.name);
    }
}

// Is this character online right now?
//
// The returned pointer is used as a boolean and IS NEVER DEREFERENCED, which
// is what makes this safe to ask from a map thread. FindConnectedPlayer reads
// HashMapHolder under its shared_mutex, so the container access is
// synchronised; what is unsafe about the whole-world accessors is touching an
// object another map's thread owns, and that never happens here. The answer
// can go stale immediately - a watcher who logs out mid-line leaves one row
// saying they heard it - which is a log artefact and not a fault. It answers
// "connected" rather than "in world", so it is also true during the moment of
// a loading screen; that window is smaller than the staleness above and costs
// the same nothing.
bool WatcherOnline(ObjectGuid guid)
{
    return ObjectAccessor::FindConnectedPlayer(guid) != nullptr;
}

// Mirror of Guild::BroadcastToGuild's recipient test: guild membership plus
// the matching listen right. Keeping this in one place is what stops the hook
// path and the Discord-origin path drifting apart.
// Takes a GUID rather than a Player* so the chat hooks never have to resolve
// one - see ForEachWatcher. This is Guild::HasRankRight's own body with the
// player->GetGUID() step removed. GetMember answering for THIS guild is
// already the membership test the old GetGuildId() comparison was making, and
// it reads the guild's roster rather than a cached field on the player.
bool GuildCanHear(Guild* guild, ObjectGuid guid, uint32 kind)
{
    if (!guild)
        return false;
    Guild::Member const* member = guild->GetMember(guid);
    if (!member)
        return false;
    uint32 const right = (kind == KIND_OFFICER) ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN;
    return (guild->GetRankRights(member->GetRankId()) & right) != GR_RIGHT_EMPTY;
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
    ForEachWatcher(kind, [&](ObjectGuid guid, std::string const& name)
    {
        if (heard(guid))
            RecordHeard(sender, name, kind, text, "");
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
//                   TravelAimBook::Load) - RememberAim there is a cache write
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
    uint16 mapId = 0;           // position observed while this aim was current
    float x = 0.f, y = 0.f, z = 0.f;
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
                 std::string const& travelTarget, Player* bot)
{
    std::lock_guard<std::mutex> guard(g_aimMutex);
    AimSnapshot& a = g_aimSnapshot[LowerName(name)];
    a.job = job;
    a.questAim = questAim;
    a.travelTarget = travelTarget;
    if (bot)
    {
        a.mapId = static_cast<uint16>(bot->GetMapId());
        a.x = bot->GetPositionX();
        a.y = bot->GetPositionY();
        a.z = bot->GetPositionZ();
    }
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
        // Group::GetLeaderName reads a string cached on the Group itself, so
        // this resolves no Player at all. It used to call
        // ObjectAccessor::FindPlayer and then dereference the result, which is
        // the same map-thread hazard as the chat hooks (#125): the leader
        // of a split party is on another map, owned by another map thread, and
        // free to be deleted while this one reads its name.
        d.groupLeader = group->GetLeaderName();
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

// ----------------------------------------------------------- errand column --
//
// THE ONE OWNER OF `overseer_roster.travel_npc`, AND OF EVERYTHING THAT MOVES
// WITH IT.
//
// WHY THIS IS A THING AND NOT THREE LOOSE MEMBERS ON THE WORLD SCRIPT. That
// column is the only one on the roster with two writers, and until this existed
// the two wrote it differently. Setting or clearing it is never just a write:
// three things move together and only one of them is in the table.
//
//   * THE COLUMN, which is what every reader asks. DriveTravel reads it to run
//     the errand, DriveQuests' arbitration reads it to decide who steers, and
//     DriveEngagementSafety reads it to decide whether a character is
//     unattended.
//   * THE ERRAND MEMORY (`_state`), which is the walk in flight: the spawn this
//     errand pinned, the closest the character has ever got to it, and the clock
//     the backstop reads. It belongs to ONE errand, not to a character, so it
//     has to die with the errand it was about.
//   * THE HAND-BACK CLOCK (`_handback`), the moment travel let go, which
//     TravelHoldsTheWheel reads to keep the quest drive off the wheel for
//     TRAVEL_HANDBACK_SECONDS afterwards.
//
// DriveTravel always moved all three, through one function. The dungeon run
// coordinator wrote the column raw in eight places and moved neither of the
// other two, so a release it drove was only reconciled a poll later by
// PruneVanished below - and an aim it re-issued at a target the previous errand
// had already failed at inherited that errand's clock, and was released as
// unreachable on its first poll having walked nowhere. That is the same bug
// Release's own comment describes, reached through the one door nothing was
// watching. What was seen on the dev world was aims overwritten mid-run, and a
// leader walking back out of a dungeon under a staging aim that had outlived
// the thing it was staging for.
//
// So the side effects stop being something a call site can forget. Nothing
// outside this writes the column: both drives go through Claim and Release, and
// each of those does the whole of its own job or none of it.
//
// TWO NAMED WRITES, NOT ONE WRITE WITH A FLAG. Claim TAKES the wheel and
// Release HANDS IT BACK, and the hand-back clock belongs to exactly one of
// those two acts. A `bool stampHandback` parameter would have made "does this
// stamp the clock" a question every call site answers again, which is how the
// asymmetry got here in the first place. Two verbs make it a property of what
// is being done rather than of who is doing it.
class TravelAimBook
{
public:
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
        uint32 entry{0};      // the spawn it resolved to
        bool pinned{false};   // ...and which it is NOT resolved away from again
        uint32 mapId{0};      // where that spawn is, so a map change drops the pin
        float x{0.f};
        float y{0.f};
        float z{0.f};
        bool arrived{false};  // announced already
        // THE FLIGHT LEG (#68). Three fields, all scoped to ONE errand and all
        // cleared with it, for the same reason the ratchet below is: a flight
        // is a fact about the trip being taken, not about the character.
        //
        // `flights` is how many this errand has spent, bounded by
        // TRAVEL_FLIGHT_MAX_PER_ERRAND so a badly placed node cannot make a
        // character hop back and forth forever.
        uint32 flights{0};
        // When the leg now running was issued, or 0 when none is. This is the
        // clock TRAVEL_FLIGHT_BACKSTOP_SECONDS runs on, and it is also how
        // DriveTravel tells "this character is walking to a flight master I
        // sent it to" from "this character is walking to its errand".
        time_t flightSince{0};
        // "Already said why this errand is not flying", so a character that
        // would fly but holds no node for some hop of the route says so once
        // per errand rather than every fifteen seconds. Reported rather than
        // silent on purpose: a flight that never happens and never explains
        // itself is indistinguishable from the bug #68 is about.
        bool flightSaid{false};
        // "Already said that upstream's stuck teleport was about to fire", so a
        // walk that keeps failing to get nearer is reported once per errand
        // rather than every poll it is held on the ground (#138). See the
        // stuck-teleport block in DriveTravel.
        bool stuckSaid{false};
        // "Already said that there was nowhere safe to step", so a character
        // held at the top of a cliff by GroundedStep says so once per errand
        // rather than every fifteen seconds (#138). Held to the same discipline
        // as `flightSaid` above and cleared with the errand for the same
        // reason: a refusal is about the journey being taken, not about the
        // character taking it.
        bool groundSaid{false};
        // "RE-ISSUE THIS WALK EVEN THOUGH IT LOOKS LIKE THE ONE ALREADY IN
        // FLIGHT" (#164). Set by the staging watchdog's second rung and spent
        // by DriveTravel's re-issue guard, which otherwise does exactly the
        // right thing and is the reason a lost walk cannot recover on its own:
        // a character whose `rpgInfo` still names the destination it is not
        // walking to is, to that guard, already walking to it. The flag is the
        // one caller that knows better, and it is a single poll's exception
        // rather than a mode - cleared at the instant the walk is issued, and
        // dying with the errand like everything else in this record.
        bool reissue{false};
        // The backstop's memory of whether the walk is going anywhere: the
        // nearest this character has ever been to this errand's target, 0 while
        // that has not been measured yet, and when it last got nearer. Kept in
        // the shared ratchet - see TRAVEL_RATCHET and
        // OverseerDecisions::Ratchet.
        //
        // IT HAS NO LIFETIME OF ITS OWN, and that is the whole of this class's
        // interaction with #118. The ratchet lives INSIDE the errand record, so
        // every way an errand can end already takes it: Release and
        // PruneVanished erase the record, Claim erases it before writing a new
        // aim, and the first poll of the new errand default-constructs a fresh
        // one which DriveTravel's own `target != target` branch then starts the
        // clock on. Nothing here reads or writes `best`, so the meaning of a
        // zero mark stays entirely OverseerDecisions::Ratchet's to define.
        OverseerDecisions::RatchetState progress;
    };

    // Every enabled character with an outstanding errand, name -> target.
    // Absent means '', "stay with the family"
    // (2026_08_25_00_overseer_roster_travel_npc.sql).
    //
    // ONE READER, THREE CALLERS, AND NO CACHED RESULT. DriveQuests needs this to
    // run the arbitration, DriveTravel needs it to run the errands, and
    // DriveEngagementSafety needs it to tell an unattended character from a
    // character on an errand. They are on SEPARATE timers - QUEST_POLL_MS,
    // TRAVEL_POLL_MS and ENGAGEMENT_POLL_MS - so there is no tick they reliably
    // share and no single snapshot that could serve all three without being
    // stale for some of them. Sharing the QUERY instead of a cached result keeps
    // each drive reading the table as it stands when it runs, and keeps
    // DriveTravel from having to know that DriveQuests exists - which
    // TravelHoldsTheWheel forbids, for the reason set out there.
    //
    // A null result is an EMPTY MAP and not an error, deliberately: a schema
    // older than this column fails the SELECT whole (MySQL 1054), and "nobody is
    // aimed" is the answer every caller already handles. See the schema-drift
    // block above LoadQuestAims for the whole argument.
    std::map<std::string, std::string> Load() const
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

    // TAKE THE WHEEL. The dungeon run coordinator is the only caller: it aims
    // the leader at a staging point or at a door, because the leader is the only
    // character that can be aimed at all (CanBeSentToNpc) and the followers
    // travel by following him.
    //
    // THIS DOES NOT STAMP THE HAND-BACK CLOCK, AND THAT IS THE WHOLE REASON IT
    // IS A SEPARATE VERB FROM Release. The hand-back is a promise that the quest
    // drive will not grab a character on the tick travel let go of it. Nothing
    // is being let go of here - an aim is being taken - and the aim itself is
    // already what stands the quest drive down (TravelHoldsTheWheel, case 1). A
    // clock stamped here would be a hand-back that never happened.
    //
    // THE ERRAND MEMORY DIES WITH THE ERRAND IT WAS ABOUT. A new aim is a new
    // errand even when the string happens to match one this character failed at
    // before, and inheriting the old pin and the old backstop clock is exactly
    // the failure Release's comment describes: released as unreachable on its
    // first poll, having walked nowhere. DriveTravel's own `state.target !=
    // target` reset cannot cover this, because the case that bites is the one
    // where the target is the SAME.
    //
    // AND IT IS IDEMPOTENT AGAINST THE AIM ALREADY IN FLIGHT, which is what lets
    // the coordinator call it on every poll of a crossing without a guard of its
    // own. `_state[name].target` IS the column as DriveTravel last read it, so
    // "is this character already walking to exactly this" is answerable from
    // here without asking the database a second time - which is what the
    // coordinator used to do, with a whole-roster SELECT, once per poll.
    // Re-issuing over the walk in flight would reset the pin and restart the
    // backstop clock every five seconds, and would also defeat DriveTravel's own
    // re-issue guard, which exists because ChangeToWanderNpc resets `lastReach`
    // and `startT`.
    //
    // WHEN THERE IS NO MEMORY TO CONSULT the write happens unconditionally, and
    // costs nothing: the character has no errand in flight for this loop to
    // disturb, the UPDATE writes the same string the previous poll wrote, and
    // erasing an entry that is not there is a no-op. The memory is at most one
    // TRAVEL_POLL_MS behind the table, and the same is true in the other
    // direction: if the bridge overwrites or clears this column, the next
    // DriveTravel poll re-reads it and the poll after that claims again.
    void Claim(std::string const& name, std::string const& target)
    {
        auto const it = _state.find(name);
        if (it != _state.end() && it->second.target == target)
            return;

        // Esc() rather than a bare interpolation, the same discipline every
        // other write in this file applies: the name came out of a table a
        // person edits, and a later change to the aim format cannot silently
        // reopen the question.
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET travel_npc = '{}' WHERE name = '{}'",
            Esc(target), Esc(name));
        _state.erase(name);
    }

    // GIVE THE ERRAND BACK. THE ONE TERMINAL PATH - every release, in either
    // drive, goes through here, which is why it does three things and not one.
    void Release(std::string const& name)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET travel_npc = '' WHERE name = '{}'", Esc(name));
        // THE CLOCK DIES WITH THE ERRAND (PR #2840 review). `since` is the
        // twenty-minute backstop, and a state entry outliving its errand is
        // inherited by the NEXT errand at the same target - which is then
        // released as unreachable on its first poll, having walked nowhere.
        _state.erase(name);
        // And the quest drive keeps its hands off for a moment: see
        // TravelHoldsTheWheel, case 2.
        //
        // STAMPED WHETHER OR NOT ANYTHING WAS ACTUALLY OUTSTANDING, on purpose.
        // Asking the table first would cost a query to save at most one
        // QUEST_POLL_MS of a stand-down that costs nothing - the quest drive
        // re-asserts the same aim on its next poll - and a conditional side
        // effect is a side effect a future caller can find itself on the wrong
        // side of. Unconditional is what makes this impossible to skip.
        _handback[name] = std::time(nullptr);
    }

    // An errand can also end without either drive touching it: the bridge owns
    // the column too and clears it when it re-aims the family. Such a row simply
    // stops coming back from Load(), so nothing INSIDE DriveTravel's loop can
    // see it go, and its state entry would sit there with a running clock
    // waiting to poison the next errand. Comparing the memory against the rows
    // that did come back is the only place that absence is visible.
    void PruneVanished(std::set<std::string> const& stillAimed)
    {
        for (auto it = _state.begin(); it != _state.end();)
        {
            if (stillAimed.count(it->first))
            {
                ++it;
                continue;
            }
            // The character may be mid-walk under an aim nobody is renewing any
            // more, so this is a release like any other and takes the same grace.
            _handback[it->first] = std::time(nullptr);
            it = _state.erase(it);
        }
    }

    // HAS TRAVEL LET GO OF THIS CHARACTER TOO RECENTLY FOR THE QUEST DRIVE TO
    // PICK IT UP? Case 2 of the arbitration, and the only part of it that is
    // about this column's own bookkeeping rather than about what a character can
    // be asked to do - which is why the rest of TravelHoldsTheWheel stays where
    // it is, with the strategy check it needs.
    //
    // Sweeps the entry once it lapses: a refusal that outlives its reason is its
    // own bug, the same shape the repick memory's give-up set already uses
    // (infra#2801).
    bool WithinHandbackGrace(std::string const& name)
    {
        auto const it = _handback.find(name);
        if (it == _handback.end())
            return false;
        if (std::time(nullptr) - it->second < TRAVEL_HANDBACK_SECONDS)
            return true;
        _handback.erase(it);
        return false;
    }

    // The walk in flight, for the loop that is actually walking it. DriveTravel
    // owns what goes IN this record - the pin, the progress, the "said already"
    // flag - and this owns its LIFETIME: it is created here on demand, and
    // destroyed by Claim, Release and PruneVanished, which are every way an
    // errand can begin or end.
    TravelState& StateFor(std::string const& name)
    {
        return _state[name];
    }

private:
    std::map<std::string, TravelState> _state;
    // When travel last let go of a character, so the quest drive does not pick
    // it up on the same tick the errand ended. Written by Release and by
    // PruneVanished - every way an errand can end - and read only by
    // WithinHandbackGrace, which sweeps an entry once it lapses. World thread
    // only, like everything else on this loop.
    std::map<std::string, time_t> _handback;
};
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

        ForEachWatcher(kind, [&](ObjectGuid guid, std::string const& name)
        {
            // The speaker hears themselves - that line belongs in their log.
            if (guid == player->GetGUID())
            {
                RecordHeard(player, name, kind, msg, "");
                return;
            }
            // Map-scoped resolution, and nothing is lost by it: audibility
            // here is distance on the same map, so a watcher the speaker's
            // map does not hold could never have satisfied IsWithinDistInMap
            // in the first place. GetPlayer also checks IsInWorld, which is
            // the other half of what the old name lookup was doing.
            if (Player* watcher = ObjectAccessor::GetPlayer(*player, guid))
                if (player->IsWithinDistInMap(watcher, range))
                    RecordHeard(player, name, kind, msg, "");
        });
        return true;
    }

    // whisper - audible to exactly two characters.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg,
                            Player* receiver) override
    {
        if (!Interested(KIND_WHISPER))
            return true;

        ForEachWatcher(KIND_WHISPER, [&](ObjectGuid guid, std::string const& name)
        {
            // Pure identity, so no lookup of any kind is needed: a watcher who
            // is one of the two ends of this whisper is online by definition.
            if (guid == player->GetGUID() || (receiver && guid == receiver->GetGUID()))
                RecordHeard(player, name, KIND_WHISPER, msg, "");
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

        ForEachWatcher(kind, [&](ObjectGuid guid, std::string const& name)
        {
            if (!group->IsMember(guid))
                return;
            if (subgroupOnly && group->GetMemberGroup(guid) != senderSub)
                return;
            // Group membership survives logout, so the offline skip that the
            // old name resolution did implicitly has to be made explicit.
            if (!WatcherOnline(guid))
                return;
            RecordHeard(player, name, kind, msg, "");
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

        ForEachWatcher(kind, [&](ObjectGuid guid, std::string const& name)
        {
            // Guild membership survives logout just as group membership does.
            if (GuildCanHear(guild, guid, kind) && WatcherOnline(guid))
                RecordHeard(player, name, kind, msg, "");
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
        PLAYERHOOK_ON_BEFORE_LOGOUT,
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

    // NO FOLLOWER KEEPS A MASTER THAT IS ABOUT TO BE FREED (#131).
    //
    // KeepRosterFollowing points every follower's PlayerbotAI::master - a raw
    // Player* - at the leader. Its own comment argues that upstream clears
    // that pointer on logout, and it did, for the headless bots this module
    // used to spawn: RandomPlayerbotMgr::OnPlayerLogout walks ITS OWN
    // PlayerBotMap and nulls every master that pointed at the departing
    // player (RandomPlayerbotMgr.cpp:2509-2515). Under #131 nobody on the
    // roster is a headless bot any more. Each is a SELFBOT - a real session
    // handed a PlayerbotAI by `self` (PlayerbotMgr.cpp:1064-1065, run at
    // login by PlayerbotMgr::OnPlayerLogin when selfBotLevel > 2) - and a
    // selfbot is in NO holder's map: AddPlayerbotData registers the AI and
    // nothing else. So when the leader's client drops and its session logs
    // the character out, that walk finds nobody, four followers keep a
    // master that has just been deleted, and the very next AI tick does
    // GET_PLAYERBOT_AI(master), which reads master->GetGUID() off freed
    // memory (PlayerbotAI.cpp:433-434, PlayerbotMgr.cpp:1792). Upstream has
    // the same hole for any party of selfbots; it is closed here for the
    // family because this module is what points them at each other.
    //
    // WHY THIS HOOK. OnPlayerBeforeLogout is the first thing LogoutPlayer
    // calls (WorldSession.cpp:716), unconditionally - ahead of the
    // `redirecting` guard that gates OnPlayerLogout at :850 - and the Player
    // is still whole. The group is walked rather than the roster resolved by
    // name: the followers this module pointed at the leader are by
    // construction in the leader's group, the group's member list is what
    // the core itself walks on every thread, and the global name map is the
    // unlocked container #125 exists to keep off any path that is not the
    // world update (see ForEachWatcher).
    void OnPlayerBeforeLogout(Player* player) override
    {
        // Roster only. A production world's hundreds of headless bots are
        // covered by upstream's own walk, which is what this exists to
        // replace for the one party upstream cannot see.
        if (!player || !OnRoster(player->GetName()))
            return;
        Group* group = player->GetGroup();
        if (!group)
            return;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == player)
                continue;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI || memberAI->GetMaster() != player)
                continue;
            memberAI->SetMaster(nullptr);
            LOG_INFO("module.overseer",
                     "overseer: '{}' is logging out, so '{}' no longer follows it - "
                     "the pointer would dangle otherwise",
                     player->GetName(), member->GetName());
        }
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
        _gearTimer += diff;
        _eventTimer += diff;
        _engagementTimer += diff;
        _terrainRecoveryTimer += diff;
        _deathTimer += diff;
        _dungeonRunTimer += diff;

        // Load the watch list before the first poll, not 30s after startup.
        if (!_watchLoaded)
        {
            _watchLoaded = true;
            ReloadWatchList();
            // And the roster, for the same reason and a sharper one: the event
            // hooks record NOTHING until they know who is on it, so leaving
            // this to the roster poll would mean a blind window after every
            // restart - which is precisely when a character is most likely to
            // do something worth having a record of.
            ReloadRosterNames();
            // And what this realm is, for the same reason and one more: until
            // this row exists the site cannot say which world it is showing,
            // and "which world is this" is the question it must never answer
            // late (mod-overseer#184).
            WriteBuildReport();
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
            KeepRosterAttended();
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
        // BEFORE DriveTravel, so a staging aim this poll writes onto the
        // leader's travel_npc is picked up by DriveTravel the SAME tick
        // rather than waiting a full TRAVEL_POLL_MS - the same reasoning
        // DriveEngagementSafety's own placement below documents for why order
        // inside one OnUpdate matters when two drives can run in the same
        // tick.
        if (_dungeonRunTimer >= DUNGEON_RUN_POLL_MS)
        {
            _dungeonRunTimer = 0;
            DriveDungeonRun();
        }
        // AND THE TRAVEL DRIVE POLLS FASTER WHILE A RUN IS ESCORTING (#122).
        //
        // An `at:` aim ends in patch 0012's ChangeToIdle the moment the walk
        // closes to INTERACTION_DISTANCE, and NewRpgStatusUpdateAction turns
        // RPG_IDLE into a randomly chosen status on the bot's next AI tick - so
        // an escorted member standing on a doorstep waiting for the rest of the
        // party is only held there by DriveTravel renewing the lease. At
        // TRAVEL_POLL_MS that renewal is fifteen seconds behind the idle, which
        // is long enough for a character to walk a hundred yards away from a
        // door it had already reached. Matching the coordinator's own cadence
        // makes the renewal never more than one coordinator poll late, so the
        // party assembles instead of milling.
        //
        // Only while an escort exists. With none - which is every poll outside a
        // dungeon run - this is TRAVEL_POLL_MS exactly as before, and the two
        // extra loader queries per poll are not paid. A catch-up walk (#138)
        // is an escort in this sense too and wants the same thing for the same
        // reason: a follower that has reached where the leader stood is held
        // there only by the lease being renewed.
        uint32 const travelPoll =
            _dungeonEscorts.empty() ? TRAVEL_POLL_MS : DUNGEON_RUN_POLL_MS;
        if (_travelTimer >= travelPoll)
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
        // AFTER the dungeon run coordinator and after the travel drive, on
        // purpose and for a plainer reason than either of those two have: a
        // character that has just been walked somewhere, or has just had a run
        // phase change under it, is a character whose combat state this drive
        // is about to ask about. Cheap either way; this order means the answer
        // is this tick's.
        if (_gearTimer >= GEAR_POLL_MS)
        {
            _gearTimer = 0;
            DriveGear();
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
        // LAST, so a recovery is the final movement decision in this world
        // tick. A quest, travel or follow drive cannot overwrite the teleport
        // before the next update sees that the character is already in flight.
        if (_terrainRecoveryTimer >= TERRAIN_RECOVERY_POLL_MS)
        {
            _terrainRecoveryTimer = 0;
            DriveBelowTerrainRecovery();
        }
    }

private:
    // NOBODY ON THE ROSTER PLAYS WITHOUT A REAL CLIENT ATTACHED (#131).
    //
    // THE RULE, FROM THE OPERATOR, STATED AS AN ABSOLUTE: a family character
    // is only ever playing while a real game client is logged in and streaming
    // its point of view. The stream is the product; a character levelling,
    // travelling and dying with nobody watching and nothing recording is
    // content destroyed rather than produced. Measured during one multi-hour
    // client outage: five characters gained levels, crossed three zones and
    // fought, off camera, the whole time.
    //
    // WHAT THIS REPLACES. This function used to be KeepRosterOnline: every
    // roster character not found in the world was logged in as a headless
    // bot through sRandomPlayerbotMgr.AddPlayerBot(guid, 0), so the family
    // played continuously whether or not any client was attached. Then a real
    // client logging in as that character EVICTED the bot: mod-playerbots'
    // secure-login script intercepts CMSG_PLAYER_LOGIN, finds the altbot
    // holding the same guid and force-logs it out through its holder
    // (PlayerbotsSecureLogin.cpp:45-50), and LogoutPlayerBot then deletes the
    // Player, its PlayerbotAI and its session in one call
    // (PlayerbotMgr.cpp:408-410). Every client (re)connection was therefore a
    // teardown of a character this module's drives were steering by name. A
    // peer review of the segfault proposed exactly that as the crash: a
    // roster name resolving, mid-eviction, to a Player whose AI is being
    // destroyed, with SteerableAI's IsInWorld/GetSession guard being a
    // check-then-use that cannot cover the botAI calls after it. The
    // measurements fit - hundreds of headless bots run for days at restart 0
    // where nothing ever evicts them, and the five client-attached characters
    // crashed their world minutes after every reconnect. If a roster
    // character is never spawned as a background bot, there is no bot
    // session to evict, and the product rule and the stability fix are the
    // same change.
    //
    // THE DISCRIMINATOR IS THE SESSION SOCKET, NOT THE PRESENCE OF A
    // PlayerbotAI. A real client's session holds a WorldSocket; a spawned
    // bot's session is constructed with none (PlayerbotMgr.cpp:203 passes
    // nullptr for the socket and true for isBot). And a selfbot - the filmed
    // character, a real session that has been handed a PlayerbotAI so the
    // engine plays it - has BOTH a PlayerbotAI and a socket, which is why the
    // AI cannot be the test. WorldSession::IsSocketClosed() is the core's own
    // public answer: `!m_Socket || !m_Socket->IsOpen()` (WorldSession.cpp:657,
    // declared public at WorldSession.h:1211), the same test Group.cpp:2435
    // uses to decide whether a member can still be talked to.
    //
    // TWO WAYS A ROSTER CHARACTER CAN BE IN THE WORLD WITH NO SOCKET, and each
    // gets the eviction its own session kind understands:
    //
    //   1. A HEADLESS BOT (session->IsBot()): somebody spawned it - an older
    //      image of this module, a `.bot add`, anything. It is logged out
    //      through the holder that owns it, the way the secure-login script
    //      does: the master's PlayerbotMgr if it has a master with one, else
    //      the random-bot holder. LogoutPlayerBot is synchronous and frees the
    //      Player, so nothing here touches the pointer after the call.
    //   2. A REAL SESSION WHOSE CLIENT WENT AWAY: the core keeps a
    //      disconnected player in the world for up to 60 seconds so it can
    //      resume (WorldSessionMgr.cpp:195, the offline-session sweep), and
    //      that sweep also honours IsKicked(). KickPlayer with setKicked
    //      (WorldSession.cpp:903-914) closes nothing that is not already
    //      closed and marks the session, so the next session update logs the
    //      character out instead of letting it stand there for a minute as a
    //      selfbot with nobody watching. The supervisor's relog then goes
    //      through an ordinary login rather than a resume; it loses nothing.
    //
    // ACROSS A RESTART the rule holds by construction: the core clears
    // `characters.online` at startup and nothing in this module logs anybody
    // in, so a roster character is offline until its client connects. And it
    // does NOT fight the stream supervisors: a session with a live socket is
    // exactly what a supervisor produces, and this never touches one.
    //
    // SCOPED TO THE ROSTER. Everything here iterates overseer_roster names.
    // The hundreds of headless bots a production world runs on purpose are not
    // on that roster and are not looked at.
    //
    // WHY A SWEEP AND NOT A LOGIN HOOK. OnPlayerLogin fires inside
    // HandlePlayerLoginFromDB, before the holder has inserted the bot into its
    // map, so LogoutPlayerBot called from there would find nothing to log out.
    // A sweep a few seconds later finds it where the holder put it.

    // Read the roster and publish it to the event hooks. Split out of
    // KeepRosterAttended so startup can prime the hooks before the first
    // roster poll, which the roster timer's seeding deliberately leaves for a
    // few ticks.
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

    // Does a real game client hold this character's session right now? This
    // is the one question every drive in this module asks before steering a
    // roster character, and the one the roster sweep asks before evicting
    // one. See KeepRosterAttended for why the socket is the test.
    static bool ClientAttached(Player const* player)
    {
        if (!player || !player->IsInWorld())
            return false;
        WorldSession const* session = player->GetSession();
        return session && !session->IsSocketClosed();
    }

    // Log a headless bot out through whichever holder owns it. Mirrors
    // mod-playerbots' own eviction on a real login
    // (PlayerbotsSecureLogin.cpp:31-50): a bot added by a master lives in
    // that master's PlayerbotMgr map, anything else in the random-bot holder,
    // and LogoutPlayerBot is a silent no-op on a holder that does not have
    // the guid - so the same order of preference is used here. The Player is
    // freed by the call; the caller must not use it afterwards.
    static void EvictHeadlessBot(Player* bot)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        ObjectGuid const guid = bot->GetGUID();
        if (botAI)
        {
            if (Player* master = botAI->GetMaster())
            {
                if (PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master))
                {
                    if (mgr->GetPlayerBot(guid))
                    {
                        mgr->LogoutPlayerBot(guid);
                        return;
                    }
                }
            }
        }
        sRandomPlayerbotMgr.LogoutPlayerBot(guid);
    }

    void KeepRosterAttended()
    {
        // The whole list is read every pass, because the event hooks need the
        // names of everybody on the roster and a row can be enabled between
        // polls.
        std::vector<std::string> const names = ReloadRosterNames();

        for (std::string const& name : names)
        {
            Player* player = ObjectAccessor::FindPlayerByName(name);
            if (!player)
                continue;   // offline, which is the correct state with no client

            if (ClientAttached(player))
                continue;   // a real client is playing it; leave it alone

            WorldSession* session = player->GetSession();
            if (!session)
                continue;   // mid-teardown already; nothing to add

            if (session->IsBot())
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' is in the world as a headless bot with no client "
                         "attached - logging it out, because the family only plays on "
                         "camera", name);
                EvictHeadlessBot(player);
                continue;   // player is freed
            }

            if (session->IsKicked())
                continue;   // already marked; the session sweep will take it

            LOG_INFO("module.overseer",
                     "overseer: '{}' has lost its client (socket closed) - kicking the "
                     "session so it logs out now rather than standing unobserved for "
                     "the core's 60-second resume window", name);
            session->KickPlayer("overseer: no client attached");
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
        // FIRST STATEMENT, BEFORE ANY `return` CAN HAPPEN (#138) - the same
        // discipline DriveDungeonRun opens with, for the same reason: a
        // catch-up walk is marked by KeepRosterFollowing at the bottom of
        // this poll, and every early exit above it - no roster, fewer than
        // two present, no group - has to end whatever the previous poll
        // stopped marking, or a follower keeps `new rpg` after the party it
        // was catching up to has gone.
        SweepCatchUps();

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
            // Present means playing on camera (#131). A roster character in
            // the world with no client is on its way out - KeepRosterAttended
            // evicts it - and inviting it, pointing it at a leader or handing
            // it leadership for the seconds in between would be steering a
            // character nobody is watching.
            Player* p = ObjectAccessor::FindPlayerByName(name);
            if (ClientAttached(p))
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
        //
        // THE ROSTER'S `lead` IS THE ONLY THING THAT DECIDES WHO LEADS (#129).
        //
        // There used to be a default below this comment: with no `lead` row,
        // the lowest-level member led, on the theory that the leader is the
        // only traveller and so whoever is behind should be the one choosing
        // where five characters go. It never ran once - the bridge pins `lead`
        // every cycle, so `wantsToLead` was never empty - and when the
        // operator was asked, the answer was that it is the wrong design
        // anyway. In the operator's words: the leader should follow the
        // lowest level to help them level up, but should ALWAYS tank for
        // them; the leader leads to do whatever the lowest-level member needs
        // to do. Leadership by seniority is a roleplay premise, and the tank
        // in front is a safety one. So "the leader serves the youngest" is
        // implemented where the leader's errands are chosen - DriveQuests'
        // own-log pick, see QuestServesTheYoungest - and not by moving the
        // leader. With no `lead` row the group keeps whatever leader it has.
        if (!wantsToLead.empty())
        {
            if (Player* head = ObjectAccessor::FindPlayerByName(wantsToLead))
            {
                if (head->GetGroup() == group && group->GetLeaderGUID() != head->GetGUID())
                {
                    group->ChangeLeader(head->GetGUID());
                    group->SendUpdate();
                    LOG_INFO("module.overseer",
                             "overseer: '{}' now leads the party at level {} - the roster "
                             "says so, and the family follows its leader",
                             wantsToLead, static_cast<uint32>(head->GetLevel()));
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
    // WHY AN EXPLICIT MASTER STICKS. Since #135 the leader is kept a selfbot
    // (its master is itself - see the leader block inside the function), and
    // the recheck at PlayerbotAI.cpp:437 fires only for a master that is a
    // bot AND not a selfbot. A follower pointed at the leader therefore never
    // enters the recheck at all. The one path that still can is a follower
    // whose master is NULL - after the leader logs out and back in, before
    // this poll - and for that follower FindNewMaster now returns the leader
    // (PlayerbotAI.cpp:4420) and upstream assigns it itself, with
    // ResetStrategies and `+follow` (PlayerbotAI.cpp:442-448). That is the
    // same end state this poll produces, reached a little earlier, so the
    // "now follows" line below appears at most once per leader change and
    // sometimes not at all. Either way the assignment below is never
    // overwritten between polls.
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
    // player (RandomPlayerbotMgr.cpp:2509-2515). That covered these five
    // while KeepRosterOnline logged them in as headless bots in that holder's
    // map. It does NOT cover them now (#131): every roster character is a
    // selfbot on a real session, and a selfbot is in no holder's map at all,
    // so that walk finds nobody. OverseerEventScript::OnPlayerBeforeLogout
    // is what clears the followers' master for the family now - see it for
    // the whole argument. This module still keeps no Player* of its own
    // between polls; the guarantee covers PlayerbotAI::master and nothing
    // else.
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

        // NOBODY IN THIS FAMILY IS EVER AWAY.
        //
        // The 3.3.5 client flags itself Away after a stretch with no keyboard
        // or mouse input, and the recorded client never has any: it is an
        // unattended window that exists to be filmed while the server's AI
        // drives the character. So it sets Away, the AI moves the character and
        // clears it, the client idles and sets it again, forever. The operator
        // watching the stream sees the chat pane fill with it:
        //
        //     You are now Away: Away        (x14, in one screenshot)
        //
        // It is not only noise. An Away character is one the world treats as
        // idle, and idle is what gets a session closed - the same class of
        // problem the base config already fights with CloseIdleConnections = 0.
        // Three times on 2026-08-30 the recorded client was found back at the
        // character-select screen with the agent still reporting it healthy,
        // and each time the dungeon brain went off with it, because the run's
        // only authorized issuer is that selfbot.
        //
        // Cleared rather than toggled: ToggleAFK (Player.h:1149) FLIPS the
        // flag, so calling it on a character that is not away would set it.
        // RemovePlayerFlag (Player.h:1127, public from Player.h:1091) is
        // idempotent, and isAFK (Player.h:1151) keeps the log honest - it says
        // something only on the polls where there was actually something to
        // clear.
        for (Player* p : present)
        {
            if (!p || !p->isAFK())
                continue;
            LOG_INFO("module.overseer",
                     "overseer: '{}' had flagged itself Away - clearing it, because an "
                     "unattended client with no keyboard is not a person who stepped out",
                     p->GetName());
            p->RemovePlayerFlag(PLAYER_FLAGS_AFK);
        }

        // THE LEADER IS ITS OWN MASTER (#135). Not "follows nobody": a leader
        // whose master is nullptr follows nobody too, and that is what this
        // block used to set, but it also makes the leader fail IsSelfBot -
        // `botAI && botAI->GetMaster() == player` (PlayerbotAI.cpp:4397-4402)
        // - and IsSelfBot is the test the dungeon module's IsAuthorized
        // applies to the issuer of every `dc` command
        // (DungeonClearChatActions.cpp:60-72). With the leader's master
        // cleared and every follower's master pointed at the leader (below),
        // NOBODY in the party passed it, and AuthorizedDcIssuer found no one
        // for as long as anyone watched:
        //
        //     'Og' is inside map 36 but no groupmate may issue a
        //     dungeon-clear command - ... so the dungeon brain stays OFF
        //
        // repeated every eight seconds with all five clients attached. The
        // dungeon brain had never armed on its own.
        //
        // Master == self is exactly what `self` sets when a client logs in
        // under SelfBotLevel 3: PlayerbotMgr::OnPlayerLogin runs the `self`
        // command (PlayerbotMgr.cpp:1657-1658), which does
        // `GET_PLAYERBOT_AI(master)->SetMaster(master)` (PlayerbotMgr.cpp:1065).
        // So a freshly logged-in leader already IS a selfbot; what this block
        // must do is keep it one. Two things take it away. A character that
        // was a follower when it was promoted (leadership drifts to the
        // roster's `lead` one poll after it changes) still carries the OLD
        // leader as master, which this block used to null. And nothing ever
        // restored the self-master afterwards, because upstream's own per-tick
        // recheck assigns only what FindNewMaster hands back, which for a
        // leader that is not a selfbot is nullptr (see the WHY block above).
        //
        // A SELF-MASTER STICKS ACROSS THE TICK. UpdateAIGroupMaster rechecks
        // only when `!master || (masterBotAI && !IsSelfBot(master))`
        // (PlayerbotAI.cpp:437); with master == bot, IsSelfBot(master) is
        // true by definition and the recheck does not fire. The no-group
        // branch (PlayerbotAI.cpp:417-426) and the battleground branch
        // (:430-431) only clear a RANDOM bot's master, and roster characters
        // are on named accounts. OverseerEventScript::OnPlayerBeforeLogout
        // skips the departing character itself. The old worry that
        // RandomPlayerbotMgr::OnPlayerLogin re-points the leader at an
        // arriving groupmate (RandomPlayerbotMgr.cpp:2582-2586) no longer
        // applies: that walk covers its own PlayerBotMap, and under #131 a
        // roster selfbot is in no holder's map.
        //
        // A cohesion loop is still impossible: a leader whose master is
        // itself walks toward nobody, same as one with no master. And a
        // genuine person at the keyboard is still left alone twice over: no
        // PlayerbotAI means this block is skipped, and a master that IS a
        // person (IsRealPlayer, PlayerbotAI.cpp:4389-4394) is not
        // overwritten, which is the same rule the follower loop below applies.
        //
        // Nothing here touches a character with no client attached: #131
        // logs those out, and this drive only ever runs over `present`.
        if (PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader))
        {
            if (leaderAI->GetMaster() != leader && !IsRealPlayer(leaderAI->GetMaster()))
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' leads, so it is its own master - the tank must "
                         "pass IsSelfBot to issue the dungeon-clear command",
                         leader->GetName());
                leaderAI->SetMaster(leader);

                // READ BACK THROUGH THE EXACT PREDICATE THE DUNGEON MODULE
                // USES, not through GetMaster(): IsSelfBot goes through
                // GET_PLAYERBOT_AI(leader) afresh, so this also catches the
                // AI this block holds not being the one registered for the
                // character. If it fires, no `dc` command from the leader
                // will ever be accepted, and the log should say so before
                // DriveDungeonClear finds nobody.
                if (!IsSelfBot(leader))
                    LOG_ERROR("module.overseer",
                              "overseer: '{}' was made its own master and still does not "
                              "pass IsSelfBot - the dungeon module will refuse every "
                              "command it issues",
                              leader->GetName());
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
            // A HUMAN AT THE KEYBOARD IS LEFT ALONE - AND A FILMED SELFBOT IS
            // NOT ONE. This guard used to ask HasGameClientMaster(), which is
            // `IsRealPlayer(master) || IsSelfBot(master)`
            // (PlayerbotAI.cpp:4459). Execution only reaches here inside
            // `if (PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader))`, and
            // IsRealPlayer is `player && !GET_PLAYERBOT_AI(player)`
            // (PlayerbotAI.cpp:4389) - so the real-player half is ALWAYS false
            // here and the test could only ever exempt a SELFBOT. That is the
            // one case it must not exempt.
            //
            // The recorded character is a selfbot with nobody at its keyboard:
            // an unattended client that exists to be filmed while the server's
            // AI drives it. Measured on the dev world 2026-08-30, the grant
            // fired twice while nobody was logged in:
            //
            //     16:17:30 'Grug' leads but did not carry `new rpg` - granting it
            //     16:18:00 'Grug' leads but did not carry `new rpg` - granting it
            //
            // then the recorder attached at 16:22:03, HasGameClientMaster()
            // went true, and it never fired again. A strategy probe either side
            // is decisive: `new rpg` present at 14:53, gone at 16:22. Four
            // minutes later the travel drive reported the LEADER unwalkable,
            // and followers travel by following - so one exempted character
            // froze all five, in a field, gathering herbs, for hours.
            //
            // A genuine person is still left alone: no PlayerbotAI means no
            // leaderAI and this whole block is skipped.
            // Unless it is held after a revival, which took `new rpg` off on
            // purpose and gives it back itself - see HoldAfterRevival.
            if (!leaderAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) &&
                !HeldAfterRevival(leader->GetName()))
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' leads but did not carry `new rpg` - granting it, "
                         "because a quest status nothing executes is a character standing "
                         "in a field", leader->GetName());
                leaderAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);

                // READ IT BACK, BECAUSE A GRANT THAT DOES NOT TAKE IS EXACTLY
                // WHAT HID THIS BUG. The old code granted and moved on, so the
                // log showed a confident "granting it" on every poll while the
                // strategy was never actually held - a line that reads as
                // success is worse than no line at all. The command surface
                // already works this way ("reading the live strategy list back
                // before it counts"); this is the same discipline on the path
                // that runs without anybody asking.
                //
                // ERROR rather than WARN: if this fires, the leader cannot walk
                // and therefore neither can the family, which is the single
                // most consequential state this module can be in.
                if (!leaderAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
                    LOG_ERROR("module.overseer",
                              "overseer: '{}' still does not carry `new rpg` after being "
                              "granted it - the leader cannot be sent anywhere and the "
                              "followers travel by following, so nobody will move",
                              leader->GetName());
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
            // ONLY AN ACTUAL PERSON WINS, not merely "a master with a client".
            // HasGameClientMaster() counts a SELFBOT master too, and the
            // recorded character is exactly that - so every follower whose
            // master was the filmed character got skipped here forever.
            //
            // Measured on the dev world 2026-08-30: moving `lead` to a true bot
            // re-formed the party ("'Grog' now leads the party") and NOT ONE
            // follower was re-pointed. No `now follows 'Grog'` line was ever
            // emitted, because all four still had the filmed selfbot as master.
            // Leadership could neither walk nor be handed away while the
            // recorder was attached.
            //
            // IsRealPlayer is null-safe (PlayerbotAI.cpp:4389-4394), so a
            // master of nullptr falls through to the assignment below, which is
            // what should happen to a follower that has lost its master.
            if (IsRealPlayer(botAI->GetMaster()))
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
            // `follow` IS DROPPED INSIDE A RUN NO LONGER - IT WAS THE ONLY
            // THING HOLDING THE PARTY TOGETHER.
            //
            // The drop shipped on the hypothesis that mod-dungeon-clear steers
            // each character itself, so the pin to a master was all that stood
            // between an armed party and a cleared dungeon. Measured on the dev
            // world 2026-08-30, that hypothesis is false. With `follow` gone the
            // party does not fan out and fight; it simply stops, and any attempt
            // to push it with per-character waypoints arrives PIECEMEAL:
            //
            //     Bork and Grog reached the tank's corpse first, pulled a
            //     Defias Miner and an Evoker with no healer in range, and both
            //     died. Ugga - the priest - was still walking. `Grog has died.
            //     Bork has died.`
            //
            // A five-man that arrives one at a time is five deaths, not a
            // clear. Until something actually drives the party as a group,
            // `follow` behind the tank IS the group behaviour, and taking it
            // away costs the healer her position beside the people she heals.
            //
            // The pin is real and still worth solving - see the second gate in
            // #100, where UpdateAIGroupMaster re-adds `follow` every tick - but
            // it is not solved by removing the only cohesion this party has.
            // A character held after a revival has `follow` off ON PURPOSE -
            // see HoldAfterRevival - and gets it back when the hold ends.
            if (!botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT) &&
                !HeldAfterRevival(p->GetName()))
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' had a master but no follow strategy - granting "
                         "it, because a master nobody walks toward is not cohesion",
                         p->GetName());
                botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
            }

            // AND `new rpg` COMES OFF A FOLLOWER NOBODY IS ESCORTING (#122).
            //
            // The invariant this family runs on is that exactly one character
            // travels on its own and it is the leader; a follower that also
            // carries `new rpg` outranks its own `follow` in the same engine
            // ("new rpg status update" is relevance 11 against
            // FollowMasterStrategy's 1) and walks off to grind, camp or quest
            // wherever it likes. That was true before an escort existed and it
            // stayed true because nothing ever granted it to a follower.
            //
            // Something does now, so the invariant needs an enforcer rather
            // than a habit. A dungeon run leases the strategy to a member while
            // it walks it to a doorway and hands it back through its own sweep
            // (see the escort section beside CanBeSentToNpc); this is the
            // backstop under that sweep, and it is deliberately a whole
            // separate mechanism on a separate clock - a hand-back that only
            // ever happens on the path that granted it is a hand-back that a
            // crash, a leadership change or an unforeseen `return` can skip.
            // Also catches an old leader demoted mid-run, who carries it in his
            // own right until he stops being the leader.
            if (botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) &&
                !IsEscorted(p->GetName()))
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' follows but carries `new rpg` with no escort "
                         "asking for it - taking it back, because a follower that travels "
                         "on its own is the scatter, not the family",
                         p->GetName());
                botAI->ChangeStrategy("-new rpg", BOT_STATE_NON_COMBAT);
                if (botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
                    LOG_ERROR("module.overseer",
                              "overseer: '{}' still carries `new rpg` after being told to "
                              "drop it - it will keep travelling on its own",
                              p->GetName());
            }

            // A FOLLOWER THAT HAS STOPPED, AND NEVER CLOSES THE GAP (#70).
            //
            // Everything above this checks that `follow` and the master are
            // PRESENT. Neither one says whether following is actually
            // WORKING - and measured on the dev world 2026-08-30, it can be
            // both present and correct while the character never moves
            // again: two followers sat at the same coordinates for eighteen
            // minutes, jittering one to eight yards, full health, out of
            // combat, still grouped, master and `follow` both intact the
            // entire time, while the leader kept walking and the 265-yard
            // gap to the rest of the party never closed.
            //
            // NEITHER KNOWN GATE EXPLAINS IT. UpdateAIGroupMaster re-enters
            // every tick (PlayerbotAI.cpp:397, :439-448) but only acts when
            // the master is missing or is a bot that is not a selfbot
            // (PlayerbotAI.cpp:437). Since #135 the leader is kept a selfbot,
            // so a follower whose master is the leader never enters that
            // branch - and when it was a plain bot, FindNewMaster
            // (PlayerbotAI.cpp:4410-4449) returned nullptr for every one of
            // these five, because its member loop only returns a member with
            // no PlayerbotAI or that IS a selfbot. Either way nothing
            // upstream is watching a follower that stands still while a
            // correctly-assigned master walks away.
            //
            // THE STUCK TRIGGER DOES NOT COVER IT EITHER, AND THE REASON
            // ASSUMED GOING IN WAS WRONG. mod-playerbots ships
            // MoveStuckTrigger/MoveLongStuckTrigger for exactly this shape
            // of problem (StuckTriggers.cpp:13-53), wired to a "reset"
            // action through the "maintenance" strategy
            // (MaintenanceStrategy.cpp:53-60). The guess going in was that
            // `IsRealPlayer(botAI->GetMaster())` inside IsActive()
            // (StuckTriggers.cpp:14-15) suppresses it for these bots because
            // their master can be a selfbot. Checked directly and that guess
            // is WRONG: IsRealPlayer is `player && !GET_PLAYERBOT_AI(player)`
            // (PlayerbotAI.cpp:4389-4394), and every master this function
            // ever assigns is another roster BOT - selfbot or plain - which
            // always has a PlayerbotAI, so IsRealPlayer(master) is false
            // here regardless and never suppresses anything. The real reason
            // the trigger never fires is upstream: "maintenance" is only
            // ever added inside `if ((sRandomPlayerbotMgr.IsRandomBot(
            // player)) ...)` (AiFactory.cpp:591), and both call sites that
            // would add it are themselves commented out
            // (AiFactory.cpp:627-628, :656-657). Roster characters are on
            // named accounts and are therefore never random bots - the same
            // gate this file's own notes already say to suspect first - so
            // the strategy carrying the trigger is never added to any of
            // these five, present-master guard or not.
            //
            // THE LEVER: a stale movement generator, not a GM shortcut.
            // Beyond AiPlayerbot.SightDistance (100 yards by default -
            // PlayerbotAIConfig.cpp:109 - which the measured 265-yard gap is
            // already past), upstream's own Follow() stops chasing
            // continuously and switches to a single discrete MoveTo per
            // attempt (MovementActions.cpp:1180-1224), reissued only when
            // the previous target is not a "duplicate" of the last one
            // commanded (MovementActions.cpp:899-909). A generator that
            // keeps producing small motion without ever arriving - which is
            // what "jittering one to eight yards" for eighteen minutes looks
            // like - is exactly the state MotionMaster::Clear()
            // (MotionMaster.h:193, public from :160) exists to discard: it
            // drops whatever the current movement generator is doing and
            // leaves the bot idle, so the NEXT `follow` tick starts a fresh
            // MoveTo at the leader's now-current position instead of
            // whatever the stale one was repeating. GetMotionMaster() is
            // public (Unit.h:1758, public from Unit.h:666).
            //
            // NOT CLAIMED PROVEN - MEASURED SEPARATELY FROM INFERRED. This
            // module does not run against the live world to confirm a fix
            // takes; the standing instruction is to never touch the running
            // world to shortcut evidence. What is measured is the
            // detection: the position telemetry this function already reads
            // every poll is everything the issue asked it to use. The
            // recovery below is the least this function can safely try with
            // public API only - and it is loud about trying, so if it does
            // not clear the stall the log already has the timestamp,
            // position and gap the next attempt needs.
            //
            // ONE NUDGE PER STALL, NOT ONE PER POLL. FOLLOW_STALL_SECONDS
            // already waits out normal repathing before acting once; acting
            // again every poll after that would just interrupt whatever the
            // fresh MoveTo above is in the middle of doing.
            // GetDistance2d(x, y) (Object.h:538, public from :479) rather
            // than a hand-rolled sqrt: it is the same 2D distance the engine
            // itself already uses everywhere else in this file.
            FollowStallState& stall = _followStall[p->GetName()];
            float const movedFromLast =
                stall.seen ? p->GetDistance2d(stall.x, stall.y) : 0.f;
            OverseerDecisions::RatchetVerdict const progress =
                OverseerDecisions::Ratchet(stall.progress, movedFromLast,
                                           std::time(nullptr), FOLLOW_STALL_RATCHET);

            // THE MARK IS DROPPED HERE, not inside the ratchet, because it is
            // a position and the ratchet is fed one number. `progress.since`
            // is assigned with it for the one case the ratchet cannot see -
            // the very first poll, when there is no mark to measure from and
            // the reading is not a reading at all. On every other poll that
            // reaches this branch the ratchet has already set it, to the same
            // second.
            if (!stall.seen || progress.progressed)
            {
                stall.seen = true;
                stall.x = p->GetPositionX();
                stall.y = p->GetPositionY();
                stall.progress.since = std::time(nullptr);
                stall.nudged = 0;
            }
            // A NUDGE, NOT A GIVE-UP, and that is why the verdict is only one
            // of three things asked here. There is nothing to release: a
            // follower has no errand, so the reaction is to clear a stale
            // movement generator and let it be tried again later, which is
            // what the cooldown below is for.
            else if (p->GetDistance2d(leader) > FOLLOW_STALL_GAP_YARDS &&
                     progress.stalled &&
                     std::time(nullptr) - stall.nudged > FOLLOW_STALL_SECONDS)
            {
                LOG_WARN("module.overseer",
                         "overseer: '{}' has not moved more than {} yards in over {} "
                         "minutes and is {} yards from '{}' - clearing its movement "
                         "so the next follow tick starts fresh",
                         p->GetName(), static_cast<uint32>(FOLLOW_STALL_JITTER_YARDS),
                         static_cast<uint32>(FOLLOW_STALL_SECONDS / 60),
                         static_cast<uint32>(p->GetDistance2d(leader)), leader->GetName());
                p->GetMotionMaster()->Clear();
                stall.nudged = std::time(nullptr);
            }

            // AND A FOLLOWER TOO FAR BACK FOR ANY OF THE ABOVE TO HELP IS
            // WALKED, NOT SNAPPED (#138). The stall check clears a generator
            // that has stopped producing motion; it cannot help a follower
            // whose every step is aimed straight at a cliff face. See
            // FOLLOW_CATCH_UP_YARDS and DriveCatchUp.
            DriveCatchUp(p, leader);
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
    // TravelAimBook::Load each select one column and each return an EMPTY map
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
    //
    // AND A REAL CLIENT MUST BE ATTACHED (#131). ClientAttached is the
    // in-world-with-a-session check above plus the session socket, which is
    // what separates a filmed character from a headless bot - see
    // KeepRosterAttended. A roster character with no client is not steered by
    // any drive, full stop: the sweep will have it out of the world within a
    // poll, and until then nothing here moves it. Every drive already comes
    // through this one function, so the rule is enforced in exactly one place.
    static PlayerbotAI* SteerableAI(Player* bot)
    {
        if (!ClientAttached(bot))
            return nullptr;
        return GET_PLAYERBOT_AI(bot);
    }

    // The errand column's reader lives on TravelAimBook (`_travelAims.Load()`),
    // with the same read-on-its-own discipline as the two loaders either side of
    // it and the same degrade on a schema that has no `travel_npc`. It is over
    // there because setting, clearing and reading that column are one ownership
    // and not three - see that class for the whole argument, and for why the
    // only UPDATE of `travel_npc` left anywhere is the one inside it.

    // Every enabled character's job mode (infra#2834), name -> mode. Absent
    // means the schema's own default, 'quest'
    // (2026_08_26_01_overseer_roster_job.sql) - so a row nobody has touched, a
    // row explicitly set to 'quest', and a `job` column that does not exist
    // yet are all the same thing to DriveQuests, deliberately: read-on-its-own,
    // same discipline as LoadQuestAims and TravelAimBook::Load and for the same
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

    // The id of the active run on this map, or 0 when there is none. The
    // arming drive keys "has `dc on` been issued" on it (#140) and the run
    // coordinator asks the same question of the same key, so both read it
    // from here rather than from two queries that could disagree.
    static uint32 ActiveRunIdOnMap(uint32 mapId)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT id FROM overseer_dungeon_run WHERE state = 'active' AND map_id = {} LIMIT 1",
            mapId);
        return result ? result->Fetch()[0].Get<uint32>() : 0;
    }

    // Open a run, or keep the open one alive. Called by the arming drive, which
    // is the component that already notices a character is inside. Returns the
    // run's id, which is what the arming drive remembers having issued `dc on`
    // for (#140); 0 only if the row vanished between the two statements.
    //
    // ONE RUN PER MAP, not per character: the five of them share an instance and
    // a run is the thing they share. A second character walking in joins the
    // run that exists rather than starting a rival one.
    static uint32 OpenOrTouchRun(std::string const& leaderName, uint32 mapId)
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
        // this call. One query answers both "which run" and "was it just
        // opened", on a path that runs once per ENGAGEMENT_POLL_MS per
        // character inside.
        QueryResult row = CharacterDatabase.Query(
            "SELECT id, last_progress_at = started_at FROM overseer_dungeon_run "
            "WHERE state = 'active' AND map_id = {} LIMIT 1", mapId);
        if (!row)
            return 0;

        uint32 const runId = row->Fetch()[0].Get<uint32>();
        // A comparison comes back from MySQL as a BIGINT, not the TINYINT a
        // boolean column would be, so it is read as one.
        if (row->Fetch()[1].Get<int64>() != 0)
        {
            LOG_INFO("module.overseer",
                     "overseer: opened dungeon run {} on map {} - '{}' is the first of "
                     "the roster inside, so the run now owns whoever is in there",
                     runId, mapId, leaderName);
        }
        return runId;
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

    // ------------------------------------------------- the run goes again (#144) --
    //
    // THE CAMPAIGN: how many runs are wanted, how many have happened, and how
    // each one ended. Everything below is bookkeeping the RESET phase and the
    // end-of-run paths in DriveDungeonRun read; the reset itself is
    // ResetGroupInstance, further down beside the coordinator that calls it.
    //
    // WHY THE SCHEMA IS PROBED RATHER THAN ASSUMED. The DDL and the reader ship
    // in DIFFERENT images and arrive in either order - the long comment above
    // LoadQuestAims carries the full mechanism, and its consequence is that a
    // SELECT naming a column the table does not have fails WHOLE. LoadJobs
    // survives that by having a fallback that means the same thing as an empty
    // result ("everybody questing"). The campaign columns have no such luck: an
    // unreadable cap is not "run it zero times" and it is certainly not "run it
    // forever", so absence has to be a distinguishable answer rather than a
    // value. INFORMATION_SCHEMA answers it once per process, cheaply, and the
    // answer cannot change under a running worldserver without a db-import that
    // would restart it anyway.
    enum class SchemaColumns : uint8 { Unknown, Absent, Present };
    SchemaColumns _runCapColumns{SchemaColumns::Unknown};
    SchemaColumns _runAccountingColumns{SchemaColumns::Unknown};

    // `columns` is a SQL list literal written at this call site and never built
    // from anything a person or a row can influence, which is why it can be
    // interpolated at all - the same discipline every other query in this file
    // follows, where anything from the world goes through Esc().
    static bool SchemaHasColumns(char const* table, char const* columns, uint64 expected)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' "
            "AND COLUMN_NAME IN ({})", table, columns);
        return result && result->Fetch()[0].Get<uint64>() >= expected;
    }

    // Can this database hold a campaign at all? Said out loud the first time the
    // answer is no, because "the family ran the dungeon once and stopped" with
    // no line explaining it is exactly the kind of silence this module keeps
    // being rebuilt to remove.
    bool CampaignColumnsPresent()
    {
        if (_runCapColumns == SchemaColumns::Unknown)
        {
            bool const present = SchemaHasColumns(
                "overseer_roster", "'dungeon_runs_wanted','dungeon_runs_done'", 2);
            _runCapColumns = present ? SchemaColumns::Present : SchemaColumns::Absent;
            if (!present)
                LOG_WARN("module.overseer",
                         "overseer: overseer_roster has no dungeon_runs_wanted / "
                         "dungeon_runs_done, so this worldserver is newer than its "
                         "database (2026_09_02_01_overseer_roster_dungeon_runs.sql has "
                         "not been applied). A dungeon run will still RESET the instance "
                         "before it starts, but it will not repeat - a cap that cannot be "
                         "read is not a cap, and looping without one is the worse "
                         "direction to guess in");
        }
        return _runCapColumns == SchemaColumns::Present;
    }

    // Can a run be accounted for? Separate from the cap on purpose: they are two
    // migrations against two tables, and a database that has one and not the
    // other should lose exactly the half it is missing.
    bool RunAccountingPresent()
    {
        if (_runAccountingColumns == SchemaColumns::Unknown)
        {
            bool const present = SchemaHasColumns(
                "overseer_dungeon_run",
                "'campaign_id','run_number','outcome','members'", 4);
            _runAccountingColumns = present ? SchemaColumns::Present : SchemaColumns::Absent;
            if (!present)
                LOG_WARN("module.overseer",
                         "overseer: overseer_dungeon_run has no campaign_id / run_number / "
                         "outcome / members (2026_09_02_02_overseer_dungeon_run_accounting"
                         ".sql has not been applied), so runs cannot be numbered or given "
                         "an outcome and the campaign loop stays off");
        }
        return _runAccountingColumns == SchemaColumns::Present;
    }

    struct DungeonCampaignCap
    {
        uint32 wanted{0};
        uint32 done{0};
        bool known{false};   // false = this database cannot answer, not "zero"
    };

    // The cap, read off the LEADER's roster row - the same row `job` is read
    // from, for the reason that column's own migration gives: the leader is the
    // only character that can be aimed, and the bridge writes the whole roster's
    // job in one pass, so a per-character cap would be five opinions about one
    // party.
    DungeonCampaignCap LoadCampaignCap(std::string const& leaderName)
    {
        DungeonCampaignCap cap;
        if (!CampaignColumnsPresent())
            return cap;

        QueryResult result = CharacterDatabase.Query(
            "SELECT dungeon_runs_wanted, dungeon_runs_done FROM overseer_roster "
            "WHERE name = '{}' AND enabled = 1", Esc(leaderName));
        if (!result)
            return cap;   // no such roster row - the caller treats that as unknown

        cap.wanted = result->Fetch()[0].Get<uint32>();
        cap.done = result->Fetch()[1].Get<uint32>();
        cap.known = true;
        return cap;
    }

    // One more run has ended, whatever it was. Incremented rather than written
    // to a computed value so that a person editing the column between two runs
    // (raising the cap mid-campaign, or resetting the count to start again)
    // cannot be silently overwritten by this module's idea of the number.
    void CountRunDone(std::string const& leaderName)
    {
        if (!CampaignColumnsPresent())
            return;
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET dungeon_runs_done = dungeon_runs_done + 1 "
            "WHERE name = '{}'", Esc(leaderName));
    }

    // A NEW CAMPAIGN'S ID, ALLOCATED WITHOUT A READ-BACK. MAX + 1 rather than
    // "the first run's own id" precisely so that nothing here has to insert a
    // row and then ask the database what id it got - see OpenOrTouchRun's own
    // comment for why this module treats a read-back after a write as a thing to
    // design around rather than a thing to do casually. The world thread is the
    // only writer, so there is no second allocator to race.
    static uint32 AllocateCampaignId()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT COALESCE(MAX(campaign_id), 0) + 1 FROM overseer_dungeon_run");
        // READ AS uint64 AND NARROWED, not read as uint32. An expression comes
        // back from MySQL as a BIGINT whatever the column it was computed from
        // was - the same fact OpenOrTouchRun already writes down about its own
        // `last_progress_at = started_at` comparison, and the same reason it
        // reads that as an int64.
        return result ? static_cast<uint32>(result->Fetch()[0].Get<uint64>()) : 0;
    }

    // Which campaign are this leader's runs on this map currently part of? Read
    // from the rows rather than remembered, so a worldserver bounce mid-campaign
    // picks the campaign back up instead of starting a rival one.
    static uint32 CampaignInProgress(std::string const& leaderName, uint32 mapId)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT campaign_id FROM overseer_dungeon_run "
            "WHERE leader_name = '{}' AND map_id = {} AND campaign_id <> 0 "
            "ORDER BY id DESC LIMIT 1", Esc(leaderName), mapId);
        return result ? result->Fetch()[0].Get<uint32>() : 0;
    }

    // How many of this campaign's most recent runs failed at the reset, counting
    // back from the newest and stopping at the first that did not.
    static uint32 TrailingResetFailures(uint32 campaignId)
    {
        if (!campaignId)
            return 0;

        QueryResult result = CharacterDatabase.Query(
            "SELECT outcome FROM overseer_dungeon_run WHERE campaign_id = {} "
            "ORDER BY id DESC LIMIT {}", campaignId,
            DUNGEON_CAMPAIGN_CONSECUTIVE_FAILURES);
        if (!result)
            return 0;

        uint32 failures = 0;
        do
        {
            if (result->Fetch()[0].Get<std::string>() != "reset_failed")
                break;
            ++failures;
        } while (result->NextRow());
        return failures;
    }

    // Put a run row into the campaign it belongs to. `AND campaign_id = 0` is
    // what makes this safe to call on every poll and safe against an ADOPTED run
    // that was already numbered by the process that started it: a run already in
    // a campaign is never renumbered by a later one.
    void StampRunIntoCampaign(uint32 runId, uint32 campaignId, uint32 runNumber,
                              std::string const& members)
    {
        if (!RunAccountingPresent() || !runId || !campaignId)
            return;
        CharacterDatabase.Execute(
            "UPDATE overseer_dungeon_run SET campaign_id = {}, run_number = {}, "
            "members = '{}' WHERE id = {} AND campaign_id = 0",
            campaignId, runNumber, Esc(members), runId);
    }

    // Read a run's campaign back, for the coordinator that adopts a run it did
    // not start (a worldserver bounce with the party still inside). Returns
    // false when this schema cannot answer or the row has gone.
    bool ReadRunCampaign(uint32 runId, uint32& campaignId, uint32& runNumber)
    {
        if (!RunAccountingPresent() || !runId)
            return false;
        QueryResult result = CharacterDatabase.Query(
            "SELECT campaign_id, run_number FROM overseer_dungeon_run WHERE id = {}",
            runId);
        if (!result)
            return false;
        campaignId = result->Fetch()[0].Get<uint32>();
        runNumber = result->Fetch()[1].Get<uint32>();
        return true;
    }

    // CLOSE A RUN BECAUSE IT IS OVER, rather than leaving it to the cold
    // heartbeat.
    //
    // Before #144 nothing ever closed a run deliberately: CloseAbandonedRuns was
    // the only writer of `state = 'ended'`, and it takes RUN_COLD_SECONDS to
    // notice. That was harmless while a run was a one-off. It is not harmless in
    // a loop, because the run row is unique per active map: the next run would
    // either collide with a corpse of the last one or, worse, silently JOIN it -
    // one row, two runs, and run numbers 1, 2, 3 that never happened.
    //
    // `AND state = 'active'` for the same reason CloseAbandonedRuns carries its
    // predicate into its own UPDATE: closing by id alone would re-close a run
    // something else had already finished and overwrite its reason.
    void CloseRun(uint32 runId, char const* outcome, std::string const& reason)
    {
        if (!runId)
            return;

        if (RunAccountingPresent())
            CharacterDatabase.Execute(
                "UPDATE overseer_dungeon_run SET state = 'ended', ended_at = NOW(), "
                "outcome = '{}', ended_reason = '{}' WHERE id = {} AND state = 'active'",
                outcome, Esc(reason), runId);
        else
            // No outcome column to write to. The run is still closed, because
            // leaving it open is the thing that actually breaks the next run.
            CharacterDatabase.Execute(
                "UPDATE overseer_dungeon_run SET state = 'ended', ended_at = NOW(), "
                "ended_reason = '{}' WHERE id = {} AND state = 'active'",
                Esc(reason), runId);
    }

    // A RUN THAT NEVER GOT INSIDE STILL HAPPENED. The arming drive is the only
    // thing that opens a run row, and it opens one when it sees a character on
    // the instance map - so a reset that will not take produces no row at all,
    // and the campaign would show a gap where its failures were. #144 asks for
    // the opposite: a failed reset "counts as a failed run". So this inserts the
    // row itself, born ended.
    //
    // THAT CANNOT COLLIDE WITH THE ONE-ACTIVE-RUN-PER-MAP UNIQUE KEY, and the
    // reason is the generated column that key is built on: `active_map` holds
    // the map id only while the run is active and NULL once it has ended, and
    // NULLs do not collide. A row inserted already-ended is therefore invisible
    // to that key by construction.
    void RecordResetFailedRun(std::string const& leaderName, uint32 mapId,
                              uint32 campaignId, uint32 runNumber,
                              std::string const& reason, std::string const& members)
    {
        if (!RunAccountingPresent())
            return;
        CharacterDatabase.Execute(
            "INSERT INTO overseer_dungeon_run "
            "(leader_name, map_id, state, ended_at, ended_reason, campaign_id, "
            " run_number, outcome, members) "
            "VALUES ('{}', {}, 'ended', NOW(), '{}', {}, {}, 'reset_failed', '{}')",
            Esc(leaderName), mapId, Esc(reason), campaignId, runNumber, Esc(members));
    }

    // WAS THAT A WIPE, ASKED OF THE DEATH RECORD RATHER THAN OF A GUESS.
    //
    // The coordinator sees the same thing for a wipe and for a relog: the map
    // holds nobody any more. The first draft told them apart by asking whether
    // any member was currently a ghost, which is a reading of the moment this
    // poll happened to land on rather than of the run - a party that has already
    // resurrected at the graveyard looks identical to one that walked out.
    //
    // overseer_death carries the fact properly: one un-coalesced row per death,
    // with the map it happened on and when. A wipe is then a statement about the
    // run's own window - at least as many DISTINCT roster characters died on the
    // instance map since this run started as went into it. Distinct, because a
    // character that dies twice is not two members.
    //
    // A schema with no overseer_death answers nullptr, which reads as "not a
    // wipe" - the honest answer for a database that cannot tell.
    static bool RunEndedInAWipe(uint32 runId, uint32 mapId, uint32 wentIn)
    {
        if (!runId || !wentIn)
            return false;

        QueryResult result = CharacterDatabase.Query(
            "SELECT COUNT(DISTINCT d.character_name) FROM overseer_death d "
            "JOIN overseer_dungeon_run r ON r.id = {} "
            "WHERE d.map = {} AND d.created_at >= r.started_at", runId, mapId);
        if (!result)
            return false;

        return result->Fetch()[0].Get<uint64>() >= wentIn;
    }

    // ------------------------------------------- the leader serves the youngest --
    //
    // THE OPERATOR'S DESIGN (#129), in the operator's own words: the leader
    // should follow the lowest level to help them level up, but should ALWAYS
    // tank for them; the leader leads to do whatever the lowest-level member
    // needs to do, and the lowest-level member does the quest if everyone
    // else already did it.
    //
    // This is NOT "the lowest level leads". The leader stays leader and stays
    // in front, because the leader is the tank and the party fights what the
    // tank pulls. What changes is what the party DOES: where it goes and which
    // quest it pursues are decided by what the youngest still needs.
    //
    // WHY IT DID NOT HAPPEN BEFORE, all measured. Only the group leader
    // carries `new rpg`, and `new rpg` is the only strategy that walks a
    // character anywhere, so the leader is the only traveller and the only
    // earner: on three consecutive days the leader was granted `new rpg` 149,
    // 143 and 146 times and never once stripped of it, while every other
    // member was granted it and then stripped within the cycle. And the
    // leader's errand, when the council had not aimed it, was picked from the
    // leader's OWN log in slot order - so the party went wherever the
    // character furthest ahead happened to have unfinished business, and the
    // character furthest behind tagged along on quests it did not hold. The
    // one piece of code written to counter that - a lowest-level-leads
    // default in KeepRosterGrouped - was gated on a `lead` column the bridge
    // pins every cycle, and had never executed.
    //
    // WHAT THIS DOES. The leader still picks from its own log, because a
    // character can only be aimed at a quest it holds (NewRpgDoQuestAction
    // idles on anything else). But the pick is RANKED rather than
    // first-in-slot-order:
    //
    //   - a quest the youngest present member also holds unfinished (in its
    //     log as INCOMPLETE or COMPLETE) ranks above every quest it does not,
    //     because that is an errand the party can do together where the
    //     youngest needs it done;
    //   - among those, a quest more of the OTHER members have already been
    //     rewarded for ranks higher, because that is the quest "everyone else
    //     already did" and the one whose absence is widening the spread;
    //   - everything else keeps its old order, so with the youngest holding
    //     nothing in common the behaviour is byte for byte what it was.
    //
    // And a self-picked quest the youngest does NOT hold is given up in favour
    // of one it does, the moment one is eligible - a lease of up to thirty
    // minutes on an errand that helps nobody behind is exactly the "fixed
    // order" the issue asks this to stop being. Only a quest still INCOMPLETE
    // is pre-empted; one that is COMPLETE is being walked to its hand-in, and
    // finishing it costs one walk and gains one turn-in.
    //
    // MEASURED WITH THE RIGHT FILTER. "In the log" is QUEST_STATUS_INCOMPLETE
    // (3) or QUEST_STATUS_COMPLETE (1), read through Player::GetQuestStatus -
    // never status 0, NONE, which the core deliberately never gives a log slot
    // (see the issue's correction). The same `status IN (1,3)` is what this
    // module's own DoShare uses, and what the acceptance check must use.
    //
    // WHAT THIS DOES NOT DO, said plainly. When the youngest holds a quest that
    // everyone else INCLUDING the leader has already rewarded, the leader
    // cannot be aimed at it - it is not in his log and cannot be again. That
    // quest can only be done by the youngest, and doing it means the youngest
    // travelling on its own errand with the tank going along. That is the
    // second half of the operator's design and it is a change to who carries
    // `new rpg`, which this file holds on the leader alone for a measured
    // reason (the 937-yard scatter). It is tracked separately.
    //
    // Level is read off the live Player rather than the roster, so a ding
    // between polls moves the choice on the next one. TIES BREAK BY NAME so
    // the youngest is stable: two characters at the same level must not hand
    // the party's errands back and forth every poll.
    static Player* YoungestOf(std::vector<Player*> const& present)
    {
        Player* youngest = nullptr;
        for (Player* p : present)
        {
            if (!p)
                continue;
            if (!youngest || p->GetLevel() < youngest->GetLevel() ||
                (p->GetLevel() == youngest->GetLevel() && p->GetName() < youngest->GetName()))
                youngest = p;
        }
        return youngest;
    }

    // Is this quest sitting unfinished in this character's log? INCOMPLETE or
    // COMPLETE and nothing else - the same two statuses the own-log walk in
    // DriveQuests accepts, so the two cannot disagree about what "held" means.
    static bool HoldsUnfinished(Player const* player, uint32 questId)
    {
        if (!player)
            return false;
        QuestStatus const status = player->GetQuestStatus(questId);  // Player.h:1492
        return status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE;
    }

    // How much doing this quest now serves the youngest. Score() is what the
    // pick is ranked by; zero means nothing about this quest helps whoever is
    // behind, and such quests keep their slot order among themselves.
    struct YoungestNeed
    {
        bool youngestHolds = false;
        uint32 othersRewarded = 0;
        uint32 Score() const { return (youngestHolds ? 100u : 0u) + othersRewarded; }
    };
    static YoungestNeed QuestServesTheYoungest(uint32 questId, Player const* leader,
                                               Player const* youngest,
                                               std::vector<Player*> const& present)
    {
        YoungestNeed need;
        // The leader IS the youngest: its own log already serves it, and the
        // ranking would only be counting the leader's own quests against
        // itself.
        if (!youngest || youngest == leader)
            return need;
        need.youngestHolds = HoldsUnfinished(youngest, questId);
        for (Player const* p : present)
        {
            if (!p || p == leader || p == youngest)
                continue;
            if (p->GetQuestRewardStatus(questId))  // Player.h:1491
                ++need.othersRewarded;
        }
        return need;
    }

    void DriveQuests()
    {
        // Read first, and each on its own, so that neither aim column can stop
        // this drive from running - see above.
        std::map<std::string, uint32> const aims = LoadQuestAims();
        std::map<std::string, std::string> const travelAims = _travelAims.Load();
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

        // The roster is read out in full before anybody is steered, because
        // the leader's pick below is ranked against the WHOLE family - who is
        // youngest, who has already been rewarded for what - and that has to
        // be known before the leader's row comes round, wherever in the
        // result it happens to sit.
        std::vector<std::pair<std::string, bool>> roster;
        do
        {
            Field* fields = result->Fetch();
            roster.emplace_back(fields[0].Get<std::string>(), fields[1].Get<uint8>() != 0);
        } while (result->NextRow());

        // Everybody present and steerable this tick. Resolved once here and
        // never held past this call: the pointers are valid for the length of
        // this world-thread tick and nothing below logs anybody out.
        std::vector<Player*> present;
        for (auto const& row : roster)
        {
            Player* member = ObjectAccessor::FindPlayerByName(row.first);
            if (SteerableAI(member))
                present.push_back(member);
        }
        Player* const youngest = YoungestOf(present);

        for (auto const& row : roster)
        {
            std::string const& name = row.first;
            bool const isLead = row.second;

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
            Player* const bot = ObjectAccessor::FindPlayerByName(name);
            RememberAim(name, jobIt == jobs.end() ? "quest" : jobIt->second, aim,
                       travelTarget, bot);

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
            //
            // UNLESS THE LEADER IS ON AN ERRAND THAT HELPS NOBODY BEHIND
            // (#129). A quest this drive picked for the leader out of its own
            // log, still INCOMPLETE, that the youngest does not hold, is
            // pre-empted the moment the leader's log offers one the youngest
            // does hold - see QuestServesTheYoungest. Guarded three ways so it
            // cannot become the walks-forever loop the check above prevents:
            // only a SELF-picked quest (the council's aim is arbitrated above
            // and never touched here), only while INCOMPLETE (a COMPLETE one is
            // being handed in), and only when a strictly better candidate
            // exists - after which the leader is on a youngest-held quest and
            // this cannot fire again until the youngest changes.
            bool preempted = false;
            if (onQuest && isLead && youngest && youngest != bot && working &&
                working == state.repick.lastPicked &&
                bot->GetQuestStatus(working) == QUEST_STATUS_INCOMPLETE &&  // Player.h:1492
                !HoldsUnfinished(youngest, working))
            {
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE && !preempted; ++slot)
                {
                    uint32 const questId = bot->GetQuestSlotQuestId(slot);  // Player.h:1510
                    if (!questId || questId == working || !HoldsUnfinished(bot, questId) ||
                        !HoldsUnfinished(youngest, questId))
                        continue;
                    if (botAI->lowPriorityQuest.count(questId) ||  // PlayerbotAI.h:605
                        state.repick.givenUp.count(questId))
                        continue;
                    preempted = true;
                }
                if (preempted)
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is on quest {} which '{}' (level {}, the "
                             "youngest) does not hold - giving it up for one the youngest "
                             "needs, because the leader serves the lowest level",
                             name, working, youngest->GetName(),
                             static_cast<uint32>(youngest->GetLevel()));
                    // Not a strike: the quest was not abandoned as unreachable,
                    // it was set aside on purpose. Forgetting it keeps the
                    // strike walk below from judging it.
                    state.repick.strikes = 0;
                    state.repick.lastPicked = 0;
                }
            }
            if (onQuest && !preempted)
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

                // HAS IT GOT ANYWHERE AT ALL SINCE THE MARK - the same rule the
                // follower-stall check asks, through the same function, so the
                // two cannot drift apart. The mark is where this character
                // stood when the strike streak began, and the branch below
                // moves it, so the best distance from it is always zero.
                //
                // ONLY THE COMPARISON HALF. This site's patience is
                // QUEST_REPICK_STRIKES consecutive picks, not a number of
                // minutes, so it counts its own strikes below rather than
                // taking a verdict from a clock it never had. See
                // QUEST_REPICK_RATCHET.
                //
                // Position::GetExactDist2d (Position.h:170, and `struct
                // Position` at Position.h:26 so public) rather than the squared
                // comparison it replaces: it is sqrt of exactly that same sum
                // (Position.h:161), so the threshold is unmoved. Deliberately
                // NOT WorldObject::GetDistance2d (Object.h:538), which the
                // follower stall does want but this does not - it subtracts the
                // character's own size and clamps at zero, which would quietly
                // shift a 45-yard threshold by a bot's width.
                bool const moved = OverseerDecisions::RatchetProgressed(
                    bot->GetExactDist2d(repick.fromX, repick.fromY),  // Position.h:170
                    0.f, QUEST_REPICK_RATCHET);

                if (!stillActionable)
                {
                    // Handed in, abandoned, or otherwise gone. Forget it
                    // silently: nothing here is evidence about reachability.
                    repick.strikes = 0;
                    repick.lastPicked = 0;
                }
                else if (moved)
                {
                    // Going somewhere. Restart the count and drop a fresh mark
                    // so the next streak is measured from where it actually is.
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

            // THE WALK COLLECTS; IT NO LONGER PICKS (#129). Every eligible
            // quest in the leader's log is gathered first, in slot order, and
            // then ONE is chosen by how much it serves the youngest. With
            // nothing in the log serving the youngest, the first eligible slot
            // wins exactly as it always did.
            struct Candidate
            {
                uint32 questId;
                Quest const* quest;
                YoungestNeed need;
            };
            std::vector<Candidate> candidates;
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
                if (!HoldsUnfinished(bot, questId))
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;

                candidates.push_back(
                    {questId, quest, QuestServesTheYoungest(questId, bot, youngest, present)});
            }

            // The best-scoring candidate, and on a tie the EARLIEST slot -
            // strictly greater, never greater-or-equal, so a run of zeros
            // resolves to the first of them, which is the old behaviour.
            Candidate const* chosen = nullptr;
            for (Candidate const& candidate : candidates)
                if (!chosen || candidate.need.Score() > chosen->need.Score())
                    chosen = &candidate;

            if (chosen)
            {
                uint32 const questId = chosen->questId;
                if (chosen->need.Score())
                    LOG_INFO("module.overseer",
                             "overseer: '{}' now working quest {} ({}) - picked from its own "
                             "log because '{}' (level {}, the youngest) {} it and {} other "
                             "member(s) have already finished it",
                             name, questId, chosen->quest->GetTitle(), youngest->GetName(),
                             static_cast<uint32>(youngest->GetLevel()),
                             chosen->need.youngestHolds ? "also holds" : "does not hold",
                             chosen->need.othersRewarded);
                else
                    LOG_INFO("module.overseer",
                             "overseer: '{}' now working quest {} ({}) - picked from its own "
                             "log, nothing chosen and nothing in it serves the youngest",
                             name, questId, chosen->quest->GetTitle());
                botAI->rpgInfo.ChangeToDoQuest(questId, chosen->quest);  // NewRpgInfo.h:106

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
        }
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
    // READ ON ITS OWN, like LoadQuestAims and TravelAimBook::Load and for the same
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

    // THE OTHER HALF OF THE SAME COLUMN. Something has to WRITE the errand
    // ClearLearnAim clears, and until now nothing inside the worldserver did.
    // The roster's declared end state was read only as a PERMISSION - it could
    // refuse a wrong trade, and it could not name a right one - so every move
    // toward it had to be scripted from outside, one at a time. That is how
    // four of the five ended up holding a pair nobody assigned them while the
    // column saying what they should hold was correct the whole time.
    void AimLearnAt(std::string const& name, uint32 skill)
    {
        CharacterDatabase.Execute(
            "UPDATE overseer_roster SET learn_skill = {} WHERE name = '{}'",
            skill, Esc(name));
    }

    // Every primary profession this character actually holds, and what each
    // one would cost to give up.
    //
    // WALKED RATHER THAN TABULATED. There is no "list my professions" on
    // Player - the skill map is private and the only way in is by id - so the
    // ids come from the DBC the core already loaded and
    // IsPrimaryProfessionSkill does the filtering, which is the same test
    // UnlearnProfession trusts three functions down. A table of the eleven
    // primaries written out in this file would be a second spelling of
    // something SkillLineStore already holds, and the only thing a second
    // spelling can add is a way to disagree. It costs one DBC lookup per row
    // per character, against a poll measured in tens of seconds.
    std::vector<OverseerDecisions::ProfessionHolding> HeldPrimaries(Player* bot)
    {
        std::vector<OverseerDecisions::ProfessionHolding> held;
        for (uint32 id = 0; id < sSkillLineStore.GetNumRows(); ++id)
        {
            if (!IsPrimaryProfessionSkill(id) || !bot->HasSkill(id))
                continue;

            OverseerDecisions::ProfessionHolding holding;
            holding.skill = static_cast<unsigned>(id);
            holding.value = static_cast<unsigned>(bot->GetPureSkillValue(id));
            held.push_back(holding);
        }
        return held;
    }

    // The price dies with the request, for the same reason TravelAimBook::Release
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

        // NOT A SHORTCUT ANY MORE. This used to read "bot->HasSkill(skill) means
        // done" and clear the errand right here - correct for a brand-new
        // profession, wrong for #74: characters already HOLD herbalism, capped
        // at Apprentice (75), standing next to nodes that need up to 120.
        // Holding a skill and holding it AT ITS CEILING are different facts,
        // and only a trainer selling the next tier tells them apart. So a
        // character who already has this skill is not finished here - it falls
        // through to the same trainer lookup a first-time learner uses, and
        // TrainerSpellForSkill (by way of Trainer::CanTeachSpell and
        // GetSpellState's chain check, Trainer.cpp:187-189) is what already
        // knows whether Journeyman/Expert/Artisan/Master is the spell this
        // trainer would sell this character next - or that none is, either
        // because the ceiling has not actually been reached yet or because
        // this trainer's highest tier is the one already held.
        bool const alreadyHasSkill = bot->HasSkill(skill);

        // BOTH SLOTS STILL FULL. Only an ACQUISITION needs one -
        // Trainer::CanTeachSpell checks GetFreePrimaryProfessionPoints solely
        // for a spell whose effect is SPELL_EFFECT_LEARN_SPELL triggering a
        // first-rank spell (Trainer.cpp:141-149), which is what STARTING a
        // profession looks like. A tier-up spell (Journeyman Herbalism and
        // the like) sets the skill directly and carries no such effect, so it
        // costs no slot and must not wait on one - a character raising a
        // skill it already holds would otherwise wait here forever behind two
        // professions it has no intention of dropping. Not an error and not a
        // refusal for the acquisition case either - it is the errand arriving
        // in the wrong order, and the unlearn drive runs on its own poll.
        // Staying put is the right answer: walking away from the trainer and
        // coming back is a five-minute round trip for a condition that clears
        // in thirty seconds.
        if (!alreadyHasSkill && !bot->GetFreePrimaryProfessionPoints())
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

        // Read before the purchase too, so the read-back below has something
        // to compare against - see the comment on `succeeded`.
        uint32 const maxBefore = static_cast<uint32>(bot->GetPureMaxSkillValue(skill));

        trainer->TeachSpell(npc, bot, spellId);  // Trainer.h:73

        // THE READ-BACK, and it is the whole point of the exercise. TeachSpell
        // returns void and reports its failures to the CLIENT - a packet no bot
        // has anybody to show it to (Trainer.cpp:88-108). So the only way to
        // know whether a character learned a trade is to ask the character. A
        // `delivered` here would mean nothing at all.
        //
        // HasSkill ALONE ANSWERS THE WRONG QUESTION for a character raising a
        // skill it already holds: HasSkill is true before this purchase and
        // still true after a purchase that failed, so it would report success
        // on a sale that changed nothing. The fact that actually moves when
        // this succeeds is the ceiling, so that is what gets compared.
        bool const succeeded = alreadyHasSkill
            ? static_cast<uint32>(bot->GetPureMaxSkillValue(skill)) > maxBefore
            : bot->HasSkill(skill);
        if (!succeeded)
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
                 "overseer: '{}' {} {} ({}) at {}/{} from '{}' - taught by a trainer it "
                 "was sent to, not granted out of thin air",
                 name, alreadyHasSkill ? "RAISED THE CAP ON" : "LEARNED", skillName, skill,
                 static_cast<uint32>(bot->GetPureSkillValue(skill)),
                 static_cast<uint32>(bot->GetPureMaxSkillValue(skill)), npc->GetName());
        RecordEvent(bot, "learn", skill, skillName,
                    alreadyHasSkill
                        ? "trained the next tier of this skill from a trainer it was sent to"
                        : "learned this profession from a trainer it was sent to");
        ClearLearnAim(name);
        return true;
    }

    // FLIGHT POINT DISCOVERY ON ARRIVAL (#68). Discovering a flight point is
    // not a proximity effect - it is answered entirely by the packet the
    // CLIENT sends when it opens a flight master's taxi window
    // (WorldSession::HandleTaxiQueryAvailableNodes, which calls
    // SendLearnNewTaxiNode when the node is not yet known - TaxiHandler.cpp).
    // A bot has no client to send that packet, so walking a character up to a
    // flight master and leaving it there - which is all an ordinary travel
    // errand already does - discovers nothing on its own. This is the same
    // shape as Trainer::TeachSpell above: the thing that would normally
    // answer a client packet is called directly, and the result is read back
    // rather than trusted, because SetTaximaskNode reports nothing anywhere a
    // bot could see it either.
    //
    // WHY THIS RUNS FOR EVERY CREATURE ARRIVAL, NOT ONLY A `flight master`
    // AIM. TravelRoles already resolves the `flight master` keyword to the
    // nearest UNIT_NPC_FLAG_FLIGHTMASTER spawn, but nothing yet SENDS a
    // character there on purpose - that decision (which zone, which
    // character, when) belongs to whatever plans travel, not to the arrival
    // handler, and is deliberately not made here. What this drive DOES have
    // is every character that ends up standing next to a flight master
    // anyway, on the way to a trainer, a vendor, or a quest turn-in.
    // Discovering it there is "cheap while already there" in the literal
    // sense infra#68 asks for, and it costs nothing extra: a creature that is
    // not a flight master simply fails the HasNpcFlag check below and this is
    // a no-op.
    void DiscoverFlightPointOnArrival(std::string const& name, Player* bot, uint32 entry)
    {
        if (!entry)
            return;   // an `at:`/`trigger:` aim names no creature to check

        // Same radius DriveTravel already measured "arrived" against, and the
        // same call TrainOnArrival uses above to turn a spawn point back into
        // the live creature standing near it.
        Creature* npc = bot->FindNearestCreature(entry, TRAVEL_ARRIVED_YARDS);
        if (!npc || !npc->IsAlive())
            return;

        // Unit.h:764 - the same flag TravelRoles keys the `flight master`
        // keyword on (UNIT_NPC_FLAG_FLIGHTMASTER, 8192) and the same one
        // travel.ROLES maps on the Python side.
        if (!npc->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
            return;

        // ObjectMgr.h:817 - the (x, y, z, mapid, teamId) overload, called
        // with the bot's own position rather than the WorldLocation overload
        // so no implicit WorldObject-to-WorldLocation conversion has to be
        // trusted. Player.h:2142 - GetTeamId(true) is the same call
        // WorldSession::SendTaxiStatus makes (TaxiHandler.cpp) to answer the
        // identical "is this the character's own team's node" question.
        uint32 const node = sObjectMgr->GetNearestTaxiNode(
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            bot->GetMapId(), bot->GetTeamId(true));
        if (!node)
            return;   // no taxi node registered this close - nothing to learn

        // Player.h:1160 (m_taxi is a public member), PlayerTaxi.h:35
        // (IsTaximaskNodeKnown).
        if (bot->m_taxi.IsTaximaskNodeKnown(node))
            return;   // already known - the ordinary case after the first visit

        // PlayerTaxi.h:42 (SetTaximaskNode) - the same function
        // SendLearnNewTaxiNode calls from the packet handler a real client
        // would have triggered. Its own return value is read rather than
        // trusted, and the mask is asked again afterward for the same reason
        // TrainOnArrival asks HasSkill: this call has no client to report
        // failure to, so the world is the only source of truth about whether
        // it worked.
        bot->m_taxi.SetTaximaskNode(node);
        if (!bot->m_taxi.IsTaximaskNodeKnown(node))
        {
            LOG_WARN("module.overseer",
                     "overseer: '{}' stood at flight master '{}' (creature {}) but taxi "
                     "node {} did not register as known afterward", name, npc->GetName(),
                     entry, node);
            return;
        }

        LOG_INFO("module.overseer",
                 "overseer: '{}' discovered flight point {} at '{}' (creature {})",
                 name, node, npc->GetName(), entry);
    }

    // FLY INSTEAD OF WALKING, WHERE FLYING IS SHORTER (#68).
    //
    // WHAT THIS IS FOR. #93 made the family GATHER flight points and they now
    // hold several each; nothing has ever made them USE one. Every errand this
    // module issues is a walk, at any distance, and the measured cost of that
    // is whole evenings spent crossing Westfall, Duskwood, Elwynn and Redridge
    // on foot - including the trek this issue was opened about, which is a
    // SINGLE direct taxi hop the leader already holds both ends of.
    //
    // NOTHING NEW IS BUILT. Everything below decides; upstream executes.
    // mod-playerbots already has the whole flight machine and this module was
    // simply never reaching for it:
    //
    //   * NewRpgInfo::ChangeToTravelFlight(entry, pos, path) puts the bot in
    //     RPG_TRAVEL_FLIGHT with a route (NewRpgInfo.cpp:44).
    //   * NewRpgTravelFlightAction, wired to the `travel flight status`
    //     trigger by the same NewRpgStrategy that owns the `wander npc status`
    //     trigger this module already drives (NewRpgStrategy.cpp), walks the
    //     bot to the flight master, dismounts it, calls SendLearnNewTaxiNode
    //     and then Player::ActivateTaxiPathTo (NewRpgAction.cpp:631).
    //   * NewRpgStatusUpdateAction returns the bot to RPG_IDLE when the flight
    //     lands (NewRpgAction.cpp:295), which is what lets DriveTravel's own
    //     re-issue guard pick the walk back up on the next poll.
    //
    // So the module needs no flight code of its own, and deliberately has
    // none: what it adds is the one thing upstream cannot supply, which is a
    // DESTINATION. Upstream only ever flies somewhere random -
    // TravelMgr::GetOptimalFlightDestinations picks a random zone in the bot's
    // level bracket, and only when the bot is already within 500 yards of a
    // flight master (TravelMgr.cpp:4404) - and it is only ever rolled as one
    // of eight idle behaviours, which an aimed character never rolls. That is
    // the whole reason `TravelFlight: 0` has been true every time it was read.
    //
    // WHY THE ROUTE HAS TO BE FULLY KNOWN. WorldSession::HandleActivateTaxi-
    // ExpressOpcode checks EVERY node of a multi-hop route against the
    // player's taximask before it will fly it (TaxiHandler.cpp:185).
    // Player::ActivateTaxiPathTo itself checks none of them, so a bot could be
    // flown down a route a player could not - which is exactly the kind of
    // shortcut this module does not take. The same check is made here instead,
    // and a route with an undiscovered hop is refused and SAID, because an
    // unflown flight that explains itself is the only way the missing node
    // ever gets noticed.
    //
    // WHAT IT CANNOT DO, STATED SO IT IS NOT MISTAKEN FOR AN OVERSIGHT. Only
    // the character carrying the errand flies. An errand moves the leadership
    // (see the professions notes above), so that character is the group
    // leader and the other four are on `follow`. Flying the party as a group
    // needs every member to hold every node on the route, which measurement
    // says they do not, and is its own ticket.
    //
    // AND SINCE #138 THE LEADER DOES NOT FLY AWAY FROM THEM EITHER. The
    // assumption that used to sit here - that the followers "keep walking the
    // overland route and regroup at the far end" - was measured false, with
    // deaths; see the refusal inside, after the node checks. In practice this
    // means a family leader flies only when every follower is already off the
    // ground or off the map, which is rare, and that is the correct trade:
    // #130's saving was minutes for one character, and the cost was the other
    // four.
    //
    // Returns true when a flight was issued, in which case the caller must NOT
    // issue the walk this poll.
    bool ConsiderFlight(std::string const& name, Player* bot, PlayerbotAI* botAI,
                        WorldPosition const& dest, float walkYards,
                        TravelAimBook::TravelState& state)
    {
        // The cheap refusals first, in the order that keeps the expensive ones
        // off the common path: most errands are short, and a short errand is
        // never worth a flight.
        if (walkYards < TRAVEL_FLIGHT_MIN_YARDS)
            return false;
        if (state.flights >= TRAVEL_FLIGHT_MAX_PER_ERRAND)
            return false;
        // Only `new rpg` runs NewRpgTravelFlightAction, the same way only
        // `new rpg` runs NewRpgWanderNpcAction - asked through the same shared
        // predicate DriveTravel's own refusal above uses, so the two can never
        // disagree about who can be sent anywhere.
        if (!CanBeSentToNpc(botAI))
            return false;
        // Player::ActivateTaxiPathTo refuses a bot that is logging out, in
        // combat, stunned or rooted (Player.cpp:10415), and refuses a MOUNTED
        // one at a flight master (Player.cpp:10428) - which a character
        // already on a taxi is. A dead one has no business walking to a
        // flight master at all. Refusing to DECIDE here
        // is better than deciding and having the activation fail at the far
        // end, because a failed activation spends one of the errand's two
        // flights on nothing.
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight())
            return false;

        // A CATCH-UP WALK DECIDES ABOUT FLYING ONCE (#138).
        //
        // The catch-up aim IS THE LEADER'S POSITION, and DriveCatchUp re-issues
        // it every time the leader walks FOLLOW_CATCH_UP_REAIM_YARDS on. Each
        // re-issue is a new value in `travel_npc`, which DriveTravel reads as a
        // new errand and gives a fresh state - including `flights`, the budget
        // whose entire job is to stop one journey buying two tickets. Measured
        // 2026-09-02 between 16:12 and 16:21: one follower was "sent to ... and
        // flies instead" FOUR TIMES in nine minutes, four different
        // destinations, four fares and four walks back to a flight master,
        // while she was on the ground and closing the gap the whole time.
        //
        // So the decision is SPENT WHEN IT IS MADE. `mayFly` is set by
        // CatchUpToward when the walk starts and cleared here the first time
        // this function gets far enough for flying to be a real option,
        // whichever way it then goes. A re-aim while walking can never board a
        // taxi. Placed after the cheap refusals above rather than at the top of
        // the function so "once" means the first poll on which a flight was
        // actually available, not the first poll on which one was impossible.
        auto const chasing = _dungeonEscorts.find(name);
        if (chasing != _dungeonEscorts.end() && chasing->second.catchUp)
        {
            if (!chasing->second.mayFly)
                return false;
            chasing->second.mayFly = false;
        }

        // ObjectMgr.h:817, the same (x, y, z, mapid, teamId) overload and the
        // same GetTeamId(true) that DiscoverFlightPointOnArrival above uses to
        // answer the identical "whose node is this" question.
        uint32 const team = bot->GetTeamId(true);
        uint32 const fromNode = sObjectMgr->GetNearestTaxiNode(
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            bot->GetMapId(), team);
        if (!fromNode)
            return false;
        // DBCStores.h - the same store Player::ActivateTaxiPathTo looks the
        // start node up in (Player.cpp:10469).
        TaxiNodesEntry const* const departure = sTaxiNodesStore.LookupEntry(fromNode);
        if (!departure)
            return false;

        // The creature that has to be walked to and talked to. Resolved
        // through the keyword TravelRoles already maps onto
        // UNIT_NPC_FLAG_FLIGHTMASTER, so "nearest flight master" means exactly
        // what it means everywhere else in this file, and the whole errand
        // vocabulary stays one vocabulary.
        uint32 fmEntry = 0;
        WorldPosition fmPos;
        if (!ResolveTravelTarget(bot, "flight master", fmEntry, fmPos))
            return false;

        // ...and it must be the creature that answers for the departure node.
        // See TRAVEL_FLIGHT_NODE_MATCH_YARDS: in a contested zone the nearest
        // flight master and the nearest node of this character's own team can
        // be two different places, and a flight issued across that gap sends a
        // character to stand next to somebody who cannot sell it a ticket.
        // Spelled out rather than asked of GetExactDist2d because
        // WorldPosition declares its own overloads of that name.
        float const nodeDx = fmPos.GetPositionX() - departure->x;
        float const nodeDy = fmPos.GetPositionY() - departure->y;
        if (nodeDx * nodeDx + nodeDy * nodeDy >
            TRAVEL_FLIGHT_NODE_MATCH_YARDS * TRAVEL_FLIGHT_NODE_MATCH_YARDS)
            return false;

        // THE LEADER DOES NOT BOARD WHILE A FOLLOWER IS ON FOOT (#138).
        //
        // The paragraph above this function still says the other four "keep
        // walking the overland route and regroup at the far end". Measured
        // 2026-09-01, they do not: the leader flew 4333 yards, landed, and
        // within three minutes three followers had killed themselves at one
        // coordinate in a zone with nothing hostile in it. Following works by
        // stepping straight at the master (see FOLLOW_CATCH_UP_YARDS), and a
        // master who has just crossed a mountain range by air is on the far
        // side of terrain his followers now walk straight into. The flight
        // was a saving of a few minutes for one character, paid for with the
        // other four.
        //
        // So a flight is refused while anyone who would try to follow it is
        // still on the ground: a live groupmate on the same map who is not
        // himself in the air. A dead one is a ghost walking to its corpse and
        // follows nobody (FollowActions.cpp:296-306); one on another map
        // cannot follow across it (FollowActions.cpp:285); one already on a
        // taxi is not on foot. ONLY THE GROUP LEADER IS HELD TO THIS. A
        // follower on a catch-up walk carries `new rpg` too and reaches this
        // same function; nobody follows a follower, so nothing is behind it
        // to be dragged, and a follower that holds the nodes flying to where
        // the leader stands is the closest thing to "everyone flies" the
        // issue asks for that this module can do without every member
        // holding every node.
        //
        // Said once per errand through `flightSaid`, like every other refusal
        // here, and BEFORE the candidate search rather than after it: the
        // search is the expensive half of this function and its answer would
        // not be acted on. GetGroup Player.h; GetLeaderGUID Group.h:228;
        // GetFirstMember Group.h:252, GetGroup Player.h:2520 - the same walk
        // this file's other member loops use.
        if (Group* group = bot->GetGroup())
        {
            if (group->GetLeaderGUID() == bot->GetGUID())
            {
                std::string onFoot;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (!member || member == bot || !member->IsInWorld() ||
                        !member->IsAlive() || member->IsInFlight() ||
                        member->GetMapId() != bot->GetMapId())
                        continue;
                    if (!onFoot.empty())
                        onFoot += ", ";
                    onFoot += '\'' + member->GetName() + '\'';
                }
                if (!onFoot.empty())
                {
                    if (!state.flightSaid)
                    {
                        state.flightSaid = true;
                        LOG_INFO("module.overseer",
                                 "overseer: '{}' is sent to '{}' {} yards away and has a "
                                 "departure node ({}) in reach, but {} would be left on foot "
                                 "behind it - walking, because a party that follows a "
                                 "leader across the sky arrives one cliff at a time",
                                 name, state.target, static_cast<uint32>(walkYards),
                                 fromNode, onFoot);
                    }
                    return false;
                }
            }
        }

        // How far the flight costs before it has flown anywhere. Half of the
        // flight-adjusted comparison below, and constant across every
        // candidate arrival node, so it is measured once out here.
        float const toMaster =
            bot->GetDistance2d(fmPos.GetPositionX(), fmPos.GetPositionY());

        // WHERE TO LAND, AND WHY IT IS NOT SIMPLY THE NEAREST NODE. The
        // nearest taxi node to an errand is the obvious answer and it is
        // regularly the wrong one, in two different ways that both end as a
        // character walking:
        //
        //   * IT MAY BE ONE NOBODY HAS DISCOVERED. Landing is only possible at
        //     a node already in the taximask, and the nearest node to a
        //     destination is frequently in a zone the family has never walked
        //     to. Taking it and giving up loses every flight to a node that IS
        //     known and only slightly further out.
        //   * IT MAY BE A NODE NOBODY CAN EVER HOLD. TaxiNodes.dbc carries
        //     rows that pass every mechanical test and have no flight master
        //     standing at them. Node 168, "Filming", sits in Elwynn Forest at
        //     (-9441, 65) - nearer than Stormwind to a good deal of that zone
        //     - carries an alliance mount id, and has a real taxi path to
        //     Stormwind. The nearest creature with UNIT_NPC_FLAG_FLIGHTMASTER
        //     is 740 yards away, so nothing can ever discover it, and taking
        //     it as the arrival node throws away the flight to Stormwind that
        //     was genuinely available.
        //
        // So the candidates are the character's OWN known nodes on the
        // destination's map, nearest to the errand first. That is both the
        // player's question - "which of my flight points is closest to where I
        // am going" - and the only set any of these can actually land at.
        std::vector<std::pair<float, uint32>> candidates;
        for (uint32 id = 1; id < sTaxiNodesStore.GetNumRows(); ++id)
        {
            // PlayerTaxi.h:35. Asked before the store lookup because it is a
            // bit test against an array and this loop runs over every node in
            // the DBC.
            if (id == fromNode || !bot->m_taxi.IsTaximaskNodeKnown(id))
                continue;
            TaxiNodesEntry const* const node = sTaxiNodesStore.LookupEntry(id);
            if (!node || node->map_id != dest.GetMapId())
                continue;
            float const dx = node->x - dest.GetPositionX();
            float const dy = node->y - dest.GetPositionY();
            candidates.emplace_back(std::sqrt(dx * dx + dy * dy), id);
        }
        // Nearest to the errand first, so the first candidate that survives
        // every check below is also the best one - and so the loop can stop
        // the moment a candidate is too far to be worth flying to, because
        // every candidate after it is further still.
        std::sort(candidates.begin(), candidates.end());

        for (auto const& candidate : candidates)
        {
            float const fromLanding = candidate.first;
            uint32 const toNode = candidate.second;

            // THE FLIGHT-ADJUSTED DISTANCE, which is the decision this issue
            // actually asks for. Not "is there a flight" - there nearly always
            // is - but "does flying put me nearer than walking would",
            // counting both walks the flight adds: to the flight master here,
            // and from the arrival node to the errand there. `break` and not
            // `continue`: the candidates only get further from the errand from
            // here on, so none of them can beat a walk either.
            if (toMaster + fromLanding + TRAVEL_FLIGHT_SAVING_YARDS >= walkYards)
                break;

            // TravelNode.h:585, public. A precomputed BFS over the whole taxi
            // graph, built once at startup by TravelMgr (TravelMgr.cpp:4361),
            // so this is a map lookup rather than a search. Fewer than two
            // nodes is no route: ActivateTaxiPathTo refuses that outright.
            std::vector<uint32> const path = sTravelNodeMap.FindTaxiPath(fromNode, toNode);
            if (path.size() < 2)
                continue;

            // EVERY HOP AFTER THE FIRST HAS TO BE KNOWN, and the first one
            // deliberately does not. WorldSession::HandleActivateTaxiExpress-
            // Opcode checks every node of a route against the taximask
            // (TaxiHandler.cpp:185), but a real client only ever sends that
            // packet after opening the flight master's window, which learns
            // the node it is standing at (SendLearnNewTaxiNode). Upstream's
            // NewRpgTravelFlightAction makes exactly that call before
            // activating, so the departure node is learned on arrival the same
            // way a player learns it - and requiring it in advance would be
            // stricter than the game, not safer than it.
            uint32 unknownHop = 0;
            for (size_t hop = 1; hop < path.size(); ++hop)
            {
                if (!bot->m_taxi.IsTaximaskNodeKnown(path[hop]))
                {
                    unknownHop = path[hop];
                    break;
                }
            }
            if (unknownHop)
            {
                // SAID ONCE PER ERRAND. This is the report #68 asks for in
                // place of a silent no-op, and it is the line that turns "they
                // never fly" into "they never fly BECAUSE nobody has walked to
                // node N yet" - which is a fixable sentence and the other one
                // is not.
                if (!state.flightSaid)
                {
                    state.flightSaid = true;
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is sent to '{}' {} yards away and could fly "
                             "node {} to node {}, but has not discovered node {} on that "
                             "route - trying the next node out, or walking",
                             name, state.target, static_cast<uint32>(walkYards),
                             fromNode, toNode, unknownHop);
                }
                continue;
            }

            // THE FARE, CHECKED HERE RATHER THAN DISCOVERED AT THE COUNTER.
            // Player::ActivateTaxiPathTo sums the same per-hop costs and
            // refuses the whole flight if the character cannot pay
            // (Player.cpp:10557) - but it refuses by sending
            // ERR_TAXINOTENOUGHMONEY to a client that does not exist, upstream
            // turns that into RPG_IDLE, and the visible result is a character
            // that walked to a flight master, stood there, and then walked on.
            // That is the silent no-op #68 asks not to have.
            //
            // ObjectMgr.h:819, the same GetTaxiPath the activation itself
            // uses. Deliberately the UNDISCOUNTED total: the reputation
            // discount Player::ActivateTaxiPathTo applies needs the flight
            // master creature in hand, which is a walk away, and it only ever
            // makes the fare SMALLER - so this can refuse a flight that would
            // in fact have been affordable, and can never let one through that
            // would not.
            uint32 fare = 0;
            bool flyable = true;
            for (size_t hop = 1; hop < path.size(); ++hop)
            {
                uint32 hopPath = 0;
                uint32 hopCost = 0;
                sObjectMgr->GetTaxiPath(path[hop - 1], path[hop], hopPath, hopCost);
                if (!hopPath)
                {
                    // The BFS graph and the DBC disagree about this hop. Not
                    // this route's fault to fix; try the next candidate.
                    flyable = false;
                    break;
                }
                fare += hopCost;
            }
            if (!flyable)
                continue;

            if (bot->GetMoney() < fare)   // Player.h, GetMoney
            {
                if (!state.flightSaid)
                {
                    state.flightSaid = true;
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is sent to '{}' {} yards away and could fly "
                             "node {} to node {}, but the fare is {} copper and it holds "
                             "{} - walking instead",
                             name, state.target, static_cast<uint32>(walkYards),
                             fromNode, toNode, fare, bot->GetMoney());
                }
                continue;
            }

            // NewRpgInfo.h:107. From here the errand belongs to upstream's
            // flight action until it lands or the leg's own backstop takes it
            // back.
            botAI->rpgInfo.ChangeToTravelFlight(fmEntry, fmPos, path);
            state.flights++;
            state.flightSince = std::time(nullptr);
            LOG_INFO("module.overseer",
                     "overseer: '{}' is sent to '{}' {} yards away and flies instead - "
                     "node {} to node {} in {} hops for {} copper, boarding at flight "
                     "master {} {} yards off, landing {} yards from the errand",
                     name, state.target, static_cast<uint32>(walkYards), fromNode, toNode,
                     static_cast<uint32>(path.size() - 1), fare, fmEntry,
                     static_cast<uint32>(toMaster), static_cast<uint32>(fromLanding));
            return true;
        }

        // NO CANDIDATE SURVIVED, and the character walks. Said once per errand
        // and only when a flight was genuinely in reach - a character with no
        // known node on this map at all, or none that beats walking, is not
        // interesting and would bury the cases that are.
        if (!state.flightSaid && !candidates.empty() &&
            toMaster + candidates.front().first + TRAVEL_FLIGHT_SAVING_YARDS < walkYards)
        {
            state.flightSaid = true;
            LOG_INFO("module.overseer",
                     "overseer: '{}' is sent to '{}' {} yards away and holds {} flight "
                     "points on this map, but none of them has a route from node {} it "
                     "can fly - walking instead",
                     name, state.target, static_cast<uint32>(walkYards),
                     static_cast<uint32>(candidates.size()), fromNode);
        }
        return false;
    }

    // ONE STEP FROM WHAT A CHARACTER HOLDS TOWARD WHAT THE ROSTER SAYS (#27).
    //
    // THE VERB THAT WAS MISSING, AND WHY ITS ABSENCE WAS INVISIBLE. The
    // decision has been in `overseer_roster.professions` and correct. Both
    // transactions have existed since the professions section above shipped:
    // TrainOnArrival buys a trade from a trainer, UnlearnProfession gives one
    // up. What was never written is the thing BETWEEN them - something that
    // reads what a character holds, compares it with what it was assigned, and
    // names the next move. Without that, every move had to be scripted from
    // outside one at a time, and five characters converging on ten professions
    // that way is a queue measured in weeks. It is not a queue that stands
    // still while it drains, either: the herbalism nobody assigned goes on
    // climbing the whole time, and every point it gains is a point that gets
    // destroyed later instead of now. That is the argument #27 makes for doing
    // this soon rather than correctly-but-later, and this is where it is acted
    // on.
    //
    // THE THREE THINGS IT WILL NOT DO, WHICH ARE THE POINT OF IT.
    //
    //   * IT NEVER GIVES UP A SKILL THE ROSTER ASKED FOR. That is rule 5 of
    //     NextProfessionStep, and it is also UnlearnProfession's own gate, so
    //     it is refused twice by two pieces of code that do not know about each
    //     other. What that protects today is the one trade anybody has actually
    //     worked - a herbalism at 117/150, an order of magnitude more than
    //     anything else on the roster - and it is protected without anyone
    //     having had to remember it was there.
    //
    //   * IT NEVER EMPTIES A SLOT WITH NOTHING TO PUT IN IT. A GiveUp is only
    //     ever returned when a slot is NEEDED, so the Take that follows it is
    //     the reason it happened. Stripping the family to nothing and leaving
    //     them that way while a queue of trainer trips drains would be a worse
    //     world than the one being fixed, and it is the obvious way to write
    //     this.
    //
    //   * IT GRANTS NOTHING. A Take writes the ERRAND, not the skill. Learning
    //     still happens at a trainer, through Trainer::TeachSpell, in
    //     TrainOnArrival, with the money taken and the free-slot rule enforced
    //     by the core. A profession appearing in `character_skills` without a
    //     trainer having been visited is the failure this epic is named after,
    //     and it is not being reintroduced from the other end by the drive
    //     meant to close it.
    //
    // AND WHEN THERE IS NOTHING TO DO IT DOES NOTHING - no write, no log line,
    // no query beyond the ones the poll was making anyway - every poll, for the
    // rest of the character's life. See NextProfessionStep rule 2. That is the
    // acceptance criterion about re-running, and it is a property of the
    // decision rather than of a guard bolted on beside it.
    void StepTowardAssignment(std::string const& name, Player* bot,
                              ProfessionPlan const& plan)
    {
        std::vector<OverseerDecisions::ProfessionHolding> const held = HeldPrimaries(bot);

        // THE CEILING IS ASKED OF THE CHARACTER RATHER THAN WRITTEN DOWN HERE.
        // Two is what upstream reads, from CONFIG_MAX_PRIMARY_TRADE_SKILL, but
        // the number that actually binds is whatever the core is willing to
        // hand out - and the core will say: the points still free plus the ones
        // already spent IS the maximum, by construction. So a realm configured
        // differently moves this rather than making it quietly wrong, and there
        // is no second copy of a configured value here to drift from the first.
        unsigned const maxPrimary = static_cast<unsigned>(held.size()) +
                                    bot->GetFreePrimaryProfessionPoints();

        std::vector<unsigned> const wanted(plan.wanted.begin(), plan.wanted.end());
        OverseerDecisions::ProfessionStep const step =
            OverseerDecisions::NextProfessionStep(wanted, held, maxPrimary);

        switch (step.kind)
        {
            case OverseerDecisions::ProfessionStepKind::Nothing:
                // Two different silences arrive here and only one of them is
                // fine. The ordinary one is "already holds what it was
                // assigned", which must stay silent or it becomes the loudest
                // line in the log while being the least interesting. The other
                // is an assignment asking for more primaries than a character
                // may hold, which nothing downstream can fix and which this
                // drive deliberately does not resolve by picking a victim - so
                // it is said once, to whoever can edit the roster.
                if (wanted.size() > maxPrimary && _assignmentTooBig.insert(name).second)
                    LOG_WARN("module.overseer",
                             "overseer: '{}' is assigned {} primary professions and may "
                             "hold {}. Nothing has been given up and nothing will be - "
                             "this is the roster being asked to choose, not a drive",
                             name, static_cast<uint32>(wanted.size()), maxPrimary);
                return;

            case OverseerDecisions::ProfessionStepKind::Take:
                // A STANDING ERRAND IS LEFT ALONE, whatever it names. Partly
                // for the reason a written unlearn outranks a derived one
                // below, and partly for one that belongs to this column
                // alone: an errand is identified in flight by the skill it is
                // for as well as by the target keyword, so changing
                // learn_skill under a character that is already walking drops
                // the pinned spawn and re-resolves the trainer mid-trip. Two
                // writers that agree about the SET but not about the ORDER
                // would do that to each other on every poll, and the
                // character would oscillate between two trainers and reach
                // neither. Writing only into an empty column removes the
                // possibility rather than relying on the two orders happening
                // to match.
                //
                // AN ERRAND NAMING SOMETHING THE ROSTER DOES NOT WANT IS NOT
                // CORRECTED HERE EITHER, and does not need to be:
                // TrainOnArrival already refuses one and clears the column, at
                // which point the next poll of this finds it empty and fills
                // it with something the roster actually asked for.
                if (plan.learnSkill)
                    return;
                LOG_INFO("module.overseer",
                         "overseer: '{}' is assigned {} ({}), does not hold it, and has a "
                         "free slot to put it in - standing errand set. It gets bought "
                         "from a trainer like anything else, not granted",
                         name, SkillName(step.skill), step.skill);
                AimLearnAt(name, step.skill);
                return;

            case OverseerDecisions::ProfessionStepKind::GiveUp:
            {
                ProfessionPlan derived = plan;
                derived.unlearnSkill = step.skill;

                // THE PRICE IS THE VALUE READ ONE STATEMENT AGO, AND THAT IS
                // NOT THE PRICE CHECK GOING SOFT. `unlearn_max` exists to
                // refuse a request whose author's picture of the world went
                // stale while the row sat in a queue - a real hazard for an
                // instruction written minutes or days ago by another process,
                // and the reason that column is a compare-and-swap rather than
                // a flag. A request derived from THIS Player on THIS line has
                // no staleness window for the check to catch, so pricing it at
                // anything but the live value would be inventing a
                // disagreement in order to then resolve it. What protects the
                // character here is what protects it from an external request
                // too, and it is strictly stronger than a number: the roster's
                // declared end state, which neither NextProfessionStep nor
                // UnlearnProfession will ever unlearn out of.
                derived.unlearnMax = static_cast<uint32>(step.cost);

                LOG_WARN("module.overseer",
                         "overseer: '{}' holds every primary slot it has and is assigned "
                         "one it does not hold, so {} ({}) at {} points is what goes - the "
                         "cheapest thing the roster did not ask for. This is the family's "
                         "own decision being carried out and the points do not come back; "
                         "they are also the fewest points it will ever cost, which is why "
                         "it happens now",
                         name, SkillName(step.skill), step.skill, step.cost);
                UnlearnProfession(name, bot, derived);
                return;
            }
        }
    }

    // The profession drive. Its own poll, because giving a trade up needs no
    // NPC and no journey: it is the prerequisite that makes the journey worth
    // taking, and making it wait for one would deadlock the pair - the
    // character cannot learn until a slot is free, and the slot would not free
    // until it arrived.
    //
    // IT NO LONGER ONLY EXECUTES INSTRUCTIONS. It used to leave the loop
    // immediately for any character with no `unlearn_skill` standing, which
    // meant the whole drive was dead for four of the five at any given moment
    // and the roster's declared end state was a permission that nothing ever
    // acted on. Now a character with no standing instruction gets one derived
    // from that end state instead - see StepTowardAssignment.
    void DriveProfessions()
    {
        std::map<std::string, ProfessionPlan> const plans = LoadProfessionPlans();
        for (auto const& [name, plan] : plans)
        {
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

            // A WRITTEN INSTRUCTION OUTRANKS A DERIVED ONE, and that ordering
            // is the whole of how the two coexist. Anything that fills
            // `unlearn_skill` has said something specific about this character
            // and has priced it; the derivation has only read a column that was
            // already true a moment ago. So the instruction is executed first,
            // and the derivation gets the polls where there is nothing else to
            // do - which, once the roster and the world agree, is all of them,
            // and it does nothing on every one.
            if (plan.unlearnSkill)
                UnlearnProfession(name, bot, plan);
            else
                StepTowardAssignment(name, bot, plan);
        }
    }

    // ------------------------------------------------------ wear what you won --
    //
    // THE FAMILY WINS GEAR AND NEVER PUTS IT ON. Measured on the live world:
    // a level 22 priest wearing item level 20 sandals and item level 22 gloves
    // while carrying, in her bags, four spare pairs of boots, three spare
    // pairs of gloves, three bracers and a second copy of a ring she is
    // already wearing. A level 27 warrior tank wearing a CLOTH robe and
    // LEATHER boots alongside four pieces of mail. The first half of the loop
    // - decide an item is worth rolling for - exists upstream. The second half
    // - put the thing on - is what nothing was ever driving, and its absence
    // is why the worn item stays bad, why the next similar drop still looks
    // like an upgrade, and why the bags are full.
    //
    // WHY THIS IS A MODULE JOB WHEN mod-playerbots HAS EQUIP ACTIONS. It has
    // three, and they are all real code: EquipUpgradesPacketAction fires off
    // the `loot roll won` packet trigger, EquipUpgradeAction is a chat command,
    // and EquipAction::SelectInventoryItemsToEquip walks the bags exactly the
    // way this drive does (mod-playerbots EquipAction.cpp:333-367,369-409).
    // What they all ask is `AI_VALUE2(ItemUsage, "item upgrade", ...)`, and
    // that judgement is the thing this issue is about: its stat calculator
    // multiplies the whole weight of an item through by its item level
    // (StatsWeightCalculator.cpp:120-128) and carries no armour-class term at
    // all - the penalty for wearing the wrong armour class is written, and
    // commented out, at :631-636, with the helper it would have called still
    // compiled and reachable from nowhere at :724. So the actions are not the
    // gap; the score they act on is. This drive supplies a score
    // (OverseerDecisions::GearScore) and then does the move itself rather than
    // trying to talk the upstream actions into using a different opinion.
    //
    // AND IT DOES THE MOVE THROUGH THE CORE, NOT THROUGH A PACKET. Player::
    // SwapItem (PlayerStorage.cpp:3632) is the same call a player's own
    // inventory drag ends in; it re-checks CanUnequipItem on the source and
    // CanEquipItem on the destination, so nothing here can put a character in
    // something the server would not have let it wear. Then the world is read
    // BACK - see this repo's AGENTS.md on why `delivered` is not `done` - and
    // a refusal is said out loud instead of assumed away.
    //
    // WHAT IT DELIBERATELY DOES NOT DO. It does not sell, destroy or hand over
    // anything, so a bag full of items nobody can wear stays full: the hand-off
    // is #14 and the vendor run is #146. What it does for the bags is the part
    // that belongs to gear - the copy you are carrying because you never put it
    // on stops being carried, because you are wearing it.
    static char const* GearSlotName(uint8 slot)
    {
        switch (slot)
        {
            case EQUIPMENT_SLOT_HEAD: return "head";
            case EQUIPMENT_SLOT_NECK: return "neck";
            case EQUIPMENT_SLOT_SHOULDERS: return "shoulders";
            case EQUIPMENT_SLOT_BODY: return "shirt";
            case EQUIPMENT_SLOT_CHEST: return "chest";
            case EQUIPMENT_SLOT_WAIST: return "waist";
            case EQUIPMENT_SLOT_LEGS: return "legs";
            case EQUIPMENT_SLOT_FEET: return "feet";
            case EQUIPMENT_SLOT_WRISTS: return "wrists";
            case EQUIPMENT_SLOT_HANDS: return "hands";
            case EQUIPMENT_SLOT_FINGER1: return "first finger";
            case EQUIPMENT_SLOT_FINGER2: return "second finger";
            case EQUIPMENT_SLOT_TRINKET1: return "first trinket";
            case EQUIPMENT_SLOT_TRINKET2: return "second trinket";
            case EQUIPMENT_SLOT_BACK: return "back";
            case EQUIPMENT_SLOT_MAINHAND: return "main hand";
            case EQUIPMENT_SLOT_OFFHAND: return "off hand";
            case EQUIPMENT_SLOT_RANGED: return "ranged";
            case EQUIPMENT_SLOT_TABARD: return "tabard";
            default: return "an unknown slot";
        }
    }

    // What this character's gear is FOR, from its class and the talent tree the
    // roster already names for it (overseer_roster.spec_tab, the same column
    // TrainRoster spends talent points from). Two facts and no guessing: a
    // class that has only one answer - a rogue is melee whatever it specs - is
    // answered from the class alone, and a class whose trees really do want
    // different gear is answered only when the roster has said which tree.
    // Everything else is GearRole::Unknown, which the scorer treats as "no
    // opinion" and refuses to act on rather than averaging into a guess.
    //
    // TAB NUMBERS ARE THE DBC's OWN tabpage order, the same ones spec_tab
    // already carries: warrior 0/1/2 = arms/fury/protection, paladin =
    // holy/protection/retribution, priest = discipline/holy/shadow, shaman =
    // elemental/enhancement/restoration, druid = balance/feral/restoration,
    // death knight = blood/frost/unholy. 255 means "leave talents alone" and
    // therefore also means "no tree has been chosen".
    static OverseerDecisions::GearRole GearRoleFor(uint8 cls, uint8 specTab)
    {
        using OverseerDecisions::GearRole;
        switch (cls)
        {
            case CLASS_WARRIOR:
                if (specTab == 2) return GearRole::Tank;
                if (specTab == 0 || specTab == 1) return GearRole::Melee;
                return GearRole::Unknown;
            case CLASS_PALADIN:
                if (specTab == 0) return GearRole::Healer;
                if (specTab == 1) return GearRole::Tank;
                if (specTab == 2) return GearRole::Melee;
                return GearRole::Unknown;
            case CLASS_HUNTER: return GearRole::Ranged;
            case CLASS_ROGUE: return GearRole::Melee;
            case CLASS_PRIEST:
                if (specTab == 0 || specTab == 1) return GearRole::Healer;
                if (specTab == 2) return GearRole::Caster;
                return GearRole::Unknown;
            case CLASS_DEATH_KNIGHT:
                // All three trees can hold a boss in 3.3.5 and only one of
                // them is ever asked to here; blood is the roster's tanking
                // answer and the other two are treated as melee.
                if (specTab == 0) return GearRole::Tank;
                if (specTab == 1 || specTab == 2) return GearRole::Melee;
                return GearRole::Unknown;
            case CLASS_SHAMAN:
                if (specTab == 0) return GearRole::Caster;
                if (specTab == 1) return GearRole::Melee;
                if (specTab == 2) return GearRole::Healer;
                return GearRole::Unknown;
            case CLASS_MAGE:
            case CLASS_WARLOCK: return GearRole::Caster;
            case CLASS_DRUID:
                if (specTab == 0) return GearRole::Caster;
                if (specTab == 1) return GearRole::Melee;
                if (specTab == 2) return GearRole::Healer;
                return GearRole::Unknown;
            default: return GearRole::Unknown;
        }
    }

    // The character's own armour proficiencies, off its own skills.
    //
    // NEVER INFERRED FROM CLASS, which is the whole of the first defect. The
    // tank measured here is a level 27 warrior and his character_skills rows
    // are mail, leather, cloth and shield - no plate row, because a warrior
    // learns plate at 40. Upstream derives the answer from class and level
    // instead (mod-playerbots RandomItemMgr.cpp:1069-1078), which happens to
    // agree in this one case and would not for a heirloom, a class this party
    // has not had yet, or any future roster. Player::HasSkill is the same
    // lookup the core's own equip check makes (PlayerStorage.cpp:2344-2366).
    static OverseerDecisions::GearWearer GearWearerFor(Player* bot, uint8 specTab)
    {
        OverseerDecisions::GearWearer who;
        who.name = bot->GetName();
        who.level = static_cast<int>(bot->GetLevel());
        who.role = GearRoleFor(bot->getClass(), specTab);
        who.cloth = bot->HasSkill(SKILL_CLOTH);
        who.leather = bot->HasSkill(SKILL_LEATHER);
        who.mail = bot->HasSkill(SKILL_MAIL);
        who.plate = bot->HasSkill(SKILL_PLATE_MAIL);
        who.shield = bot->HasSkill(SKILL_SHIELD);
        return who;
    }

    // An item template flattened into the plain facts the scorer reads. The
    // scorer includes no core header, so this is where a core type stops.
    static OverseerDecisions::GearItem GearItemFor(ItemTemplate const* proto,
                                                   bool unresolvedRandomProperty)
    {
        OverseerDecisions::GearItem item;
        item.name = proto->Name1;
        item.itemClass = static_cast<int>(proto->Class);
        item.subClass = static_cast<int>(proto->SubClass);
        item.inventoryType = static_cast<int>(proto->InventoryType);
        item.itemLevel = static_cast<int>(proto->ItemLevel);
        item.requiredLevel = static_cast<int>(proto->RequiredLevel);
        item.quality = static_cast<int>(proto->Quality);
        item.armour = static_cast<int>(proto->Armor);

        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            if (!proto->ItemStat[i].ItemStatValue)
                continue;
            OverseerDecisions::GearStat stat;
            stat.type = static_cast<int>(proto->ItemStat[i].ItemStatType);
            stat.value = static_cast<int>(proto->ItemStat[i].ItemStatValue);
            item.stats.push_back(stat);
        }

        // Damage per second, the honest way: the template's own damage lines
        // over its own swing timer. ItemTemplate::Delay is in milliseconds.
        if (proto->Delay)
        {
            float damage = 0.f;
            for (uint32 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
                damage += (proto->Damage[i].DamageMin + proto->Damage[i].DamageMax) / 2.f;
            item.dps = damage * 1000.f / static_cast<float>(proto->Delay);
        }

        for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            if (proto->Spells[i].SpellId > 0)
                item.hasEffect = true;

        item.unresolvedRandomProperty = unresolvedRandomProperty;
        return item;
    }

    // One item, scored for one character.
    //
    // THE TWO PER-ITEM FACTS THE SCORER CANNOT WORK OUT FOR ITSELF are gathered
    // here, from the core, and handed in: whether the character is allowed the
    // item at all, and whether it holds the weapon skill this particular weapon
    // needs. Player::CanUseItem(ItemTemplate const*) answers the first
    // (PlayerStorage.cpp:2377-2421: faction, class mask, race, RequiredSkill,
    // RequiredSpell, level) and notably does NOT answer armour proficiency -
    // that check lives only in the Item* overload, which is exactly why a
    // priest sails through it holding a pair of leather boots. The scorer makes
    // that test itself, from the five booleans above.
    static OverseerDecisions::GearVerdict GearScoreFor(Player* bot,
                                                       OverseerDecisions::GearWearer who,
                                                       ItemTemplate const* proto,
                                                       bool unresolvedRandomProperty)
    {
        who.classAllowed = bot->CanUseItem(proto) == EQUIP_ERR_OK;
        who.weaponProficient = proto->Class != ITEM_CLASS_WEAPON || proto->GetSkill() == 0 ||
                               bot->HasSkill(proto->GetSkill());
        return OverseerDecisions::GearScore(GearItemFor(proto, unresolvedRandomProperty), who);
    }

    // What is worn in one slot, scored. An empty slot is zero, which is what
    // makes the first item into an empty slot an upgrade by construction.
    static float GearWornScore(Player* bot, OverseerDecisions::GearWearer const& who,
                               uint8 slot)
    {
        Item* const item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            return 0.f;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return 0.f;
        OverseerDecisions::GearVerdict const verdict =
            GearScoreFor(bot, who, proto, item->GetItemRandomPropertyId() != 0);
        return verdict.score;
    }

    // Everything a character is wearing, scored, which is what decides who
    // Needs when two of them want the same drop.
    static float GearTotalWorn(Player* bot, OverseerDecisions::GearWearer const& who)
    {
        float total = 0.f;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            total += GearWornScore(bot, who, slot);
        return total;
    }

    // Everything in the backpack and the equipped bags. Same walk ProbeBags
    // makes, and for the same reason: a character's carried items are in two
    // different places and only one of them is obvious.
    static std::vector<Item*> GearCarried(Player* bot)
    {
        std::vector<Item*> carried;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                carried.push_back(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        {
            Bag* bag = bot->GetBagByPos(bagSlot);
            if (!bag)
                continue;
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                if (Item* item = bag->GetItemByPos(i))
                    carried.push_back(item);
        }
        return carried;
    }

    // Say a thing about one character and one item exactly once. The sweep runs
    // on a five-second timer, so anything it says about an item it is NOT going
    // to move would otherwise be said seventeen thousand times a day. A swap
    // that WORKS needs no such guard - it changes the world, so the next pass
    // has nothing left to say about it - and is deliberately not routed through
    // here, because a character putting something on is the one line in this
    // drive worth having in the log every time it happens.
    //
    // Bounded by the number of distinct item entries the family ever carries,
    // unguarded because DriveGear runs only from OnUpdate on the world thread,
    // and lost on a restart, which costs one repeated line and no correctness -
    // the same terms every other per-character memory in this file is kept on.
    bool SayGearOnce(std::string const& name, uint32 entry)
    {
        return _gearSaid[name].insert(entry).second;
    }
    std::map<std::string, std::set<uint32>> _gearSaid;

    // One character's bags, against one character's worn gear.
    void SweepGear(Player* bot, OverseerDecisions::GearWearer const& who)
    {
        // A character rummaging through its bags mid-fight is not a character
        // playing the way a person does, and Player::SwapItem would refuse a
        // weapon swap in combat anyway (CanUnequipItem). Dead is the same
        // answer for a plainer reason: SwapItem's first test is IsAlive.
        if (!bot->IsAlive() || bot->IsInCombat())
            return;

        for (Item* item : GearCarried(bot))
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                continue;
            if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
                continue;
            if (proto->InventoryType == INVTYPE_NON_EQUIP)
                continue;

            // Where would it go? The core's own answer, so a shield lands in
            // the off hand and a two-hander does not land anywhere a shield is.
            uint8 const found = bot->FindEquipSlot(proto, NULL_SLOT, true);
            if (found == NULL_SLOT || found >= EQUIPMENT_SLOT_END)
                continue;

            OverseerDecisions::GearVerdict const candidate =
                GearScoreFor(bot, who, proto, item->GetItemRandomPropertyId() != 0);

            // Not wearable is not news. It is the ordinary state of most of
            // what a party carries out of a dungeon, and saying so once per
            // item per character would bury the lines that matter.
            if (!candidate.wearable)
                continue;

            uint8 target = found;
            float incumbent = GearWornScore(bot, who, found);

            // A TWO-HANDER HAS TO BEAT BOTH HANDS (#14, the Severing Axe): the
            // off hand is emptied to make room for it, so what it is really
            // being compared against is the pair.
            if (proto->InventoryType == INVTYPE_2HWEAPON && found == EQUIPMENT_SLOT_MAINHAND)
            {
                incumbent = OverseerDecisions::GearIncumbent(
                    incumbent, GearWornScore(bot, who, EQUIPMENT_SLOT_OFFHAND), true);
            }
            // A RING OR A TRINKET HAS TWO HOMES, and the one worth taking is
            // the worse of them. FindEquipSlot names the first; this picks.
            else if (found == EQUIPMENT_SLOT_FINGER1 || found == EQUIPMENT_SLOT_TRINKET1)
            {
                uint8 const other = static_cast<uint8>(found + 1);
                float const second = GearWornScore(bot, who, other);
                if (second < incumbent)
                {
                    target = other;
                    incumbent = second;
                }
            }

            if (!OverseerDecisions::GearIsUpgrade(candidate, incumbent))
                continue;

            // IT SCORES HIGHER AND THE SCORE IS NOT THE WHOLE STORY. Said, and
            // then left alone: the number that would justify the swap is one
            // this file has already admitted is incomplete, and swapping on it
            // anyway is exactly the confident-but-wrong move that put a cloth
            // robe on the tank. Checked here rather than before the comparison
            // so it is only ever said about an item that would OTHERWISE have
            // been put on - the bags are full of things nobody needs told
            // about.
            if (!candidate.judged)
            {
                if (SayGearOnce(who.name, item->GetEntry()))
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is carrying {}, which scores above the {} it is "
                             "wearing - {} - and it is left in the bags because that score is "
                             "not the whole story",
                             who.name, proto->Name1, GearSlotName(target), candidate.why);
                continue;
            }

            // EVERYTHING THE LOG LINE NEEDS IS READ BEFORE ANYTHING MOVES.
            // Player::SwapItem re-homes both items, so `item` is not where it
            // was and the displaced Item* is not where it was either - the same
            // discipline the give command already documents at length.
            uint32 const entry = item->GetEntry();
            std::string const itemName = proto->Name1;
            Item* const wornItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, target);
            ItemTemplate const* wornProto = wornItem ? wornItem->GetTemplate() : nullptr;
            std::string const replaced = wornProto ? wornProto->Name1 : "nothing";
            uint16 const src = static_cast<uint16>(
                (static_cast<uint16>(item->GetBagSlot()) << 8) | item->GetSlot());
            uint16 const dst = static_cast<uint16>(
                (static_cast<uint16>(INVENTORY_SLOT_BAG_0) << 8) | target);

            bot->SwapItem(src, dst);

            // DELIVERED IS NOT DONE. SwapItem reports a refusal to a client
            // through SendEquipError and returns void, which for a character
            // nobody is watching is the same as silence - so the world is asked
            // rather than the call believed.
            Item* const nowWorn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, target);
            if (nowWorn && nowWorn->GetEntry() == entry)
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' puts on {} in the {} slot over {} - {} against {} "
                         "for what it was wearing",
                         who.name, itemName, GearSlotName(target), replaced, candidate.why,
                         static_cast<int>(incumbent));
            }
            else if (SayGearOnce(who.name, entry))
            {
                LOG_INFO("module.overseer",
                         "overseer: '{}' should be wearing {} in the {} slot over {} - {} "
                         "against {} - and the server refused the swap, so it stays in the "
                         "bags",
                         who.name, itemName, GearSlotName(target), replaced, candidate.why,
                         static_cast<int>(incumbent));
            }
        }
    }

    // Everything one member needs to be voted for.
    struct GearMember
    {
        Player* bot{nullptr};
        OverseerDecisions::GearWearer who;
    };

    // NEED ON A REAL UPGRADE, GREED ON EVERYTHING ELSE (#10, #145).
    //
    // WHAT THIS ADDS TO WHAT IS ALREADY DEPLOYED, AND WHAT IT LEAVES ALONE.
    // The voting itself works: LootNeedRollLevel = 2 and LootGreedRollLevel = 1
    // are set on this realm, so a NEED reaches the tally as a NEED and a GREED
    // as a GREED rather than being downgraded to a pass, and patch 0008 already
    // stopped BAD_EQUIP - the value that means "I have decided not to wear
    // this" - from voting NEED. None of that is touched. What is added is the
    // judgement in front of it, from the same scorer the sweep above uses, so
    // that "upgrade" means the same thing to the roll as it does to the
    // wardrobe.
    //
    // IT ONLY EVER FILLS IN A VOTE NOBODY HAS CAST. mod-playerbots'
    // LootRollAction votes on the same rolls from the bot's own AI tick and
    // guards itself the same way (LootRollAction.cpp:26-28), and whichever of
    // the two reaches a still-unvoted roll first is the one that counts. This
    // does not try to overrule a vote already cast, because it cannot:
    // Group::CountRollVote has no idempotence at all (Group.cpp:1501-1548
    // increments totalNeed/totalGreed/totalPass unconditionally and calls
    // CountTheRoll the moment they add up), so voting twice for one member
    // corrupts the tally and can end the roll early. A race that is only ever
    // resolved in favour of "somebody has an opinion" is safe; one resolved by
    // arithmetic on a shared counter is not.
    //
    // MASTER LOOT AND FREE FOR ALL ARE LEFT ENTIRELY ALONE. Under those methods
    // the roll is not how the item is distributed and upstream votes PASS
    // (LootRollAction.cpp:96-105); there is nothing here to improve and a Need
    // cast into one would be noise.
    void VoteOnOpenRolls(Group* group, std::vector<GearMember> const& members)
    {
        LootMethod const method = group->GetLootMethod();
        if (method == MASTER_LOOT || method == FREE_FOR_ALL)
            return;

        // GetRolls returns a COPY of the vector, which is what makes this safe
        // to iterate while voting: CountRollVote can finish and delete the roll
        // it was given, and the pointers to the OTHER rolls stay good. The one
        // rule is that nothing may read `roll` after the first vote is cast on
        // it, so everything needed is taken off it up front.
        std::vector<Roll*> const rolls = group->GetRolls();
        for (Roll* roll : rolls)
        {
            if (!roll)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(roll->itemid);
            if (!proto)
                continue;

            // GEAR ONLY. This scorer knows what a piece of armour and a weapon
            // are worth to a character and knows nothing whatever about a
            // recipe it could learn, a bigger bag, a reagent a profession
            // wants, or an armour token. Upstream's ItemUsage does know about
            // all four (ItemUsageValue.cpp:32-95 for the skill and consumable
            // cases, LootRollAction.cpp:51-58 for tokens and :72-79 for
            // recipes), and every one of those is a NEED it would cast and
            // this file would turn into a Greed. So the vote is left entirely
            // alone for anything that is not worn.
            if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
                continue;

            ObjectGuid const rollGuid = roll->itemGUID;
            bool const randomProperty = roll->itemRandomPropId != 0 || roll->itemRandomSuffix != 0;
            bool const needAllowed = (roll->rollVoteMask & ROLL_FLAG_TYPE_NEED) != 0;

            struct GearBallot
            {
                Player* bot{nullptr};
                std::string name;
                OverseerDecisions::GearVerdict verdict;
                float incumbent{0.f};
                bool wants{false};
            };
            std::vector<GearBallot> ballots;
            std::vector<OverseerDecisions::GearContender> contenders;

            for (GearMember const& member : members)
            {
                Roll::PlayerVote::const_iterator const cast =
                    roll->playerVote.find(member.bot->GetGUID());
                // Not in the roll at all (out of reward range), or already
                // voted - either way this drive has nothing to add.
                if (cast == roll->playerVote.end() || cast->second != NOT_EMITED_YET)
                    continue;

                GearBallot ballot;
                ballot.bot = member.bot;
                ballot.name = member.who.name;
                ballot.verdict = GearScoreFor(member.bot, member.who, proto, randomProperty);

                // AN UNJUDGED VERDICT CASTS NO VOTE AT ALL. Not a Greed: a
                // Greed is an opinion, and the whole content of `judged ==
                // false` is that this file does not have one - the item's
                // worth is partly in a proc it does not read, or in a random
                // suffix that could not be resolved, or the character's role
                // was never named. Leaving the vote un-emitted hands the roll
                // back to mod-playerbots' LootRollAction on the bot's next
                // tick, which is exactly what happened before this drive
                // existed. Silence is the only answer here that cannot be a
                // regression.
                if (!ballot.verdict.judged)
                    continue;

                if (ballot.verdict.wearable)
                {
                    uint8 const found = member.bot->FindEquipSlot(proto, NULL_SLOT, true);
                    if (found != NULL_SLOT && found < EQUIPMENT_SLOT_END)
                    {
                        ballot.incumbent = GearWornScore(member.bot, member.who, found);
                        if (proto->InventoryType == INVTYPE_2HWEAPON &&
                            found == EQUIPMENT_SLOT_MAINHAND)
                        {
                            ballot.incumbent = OverseerDecisions::GearIncumbent(
                                ballot.incumbent,
                                GearWornScore(member.bot, member.who, EQUIPMENT_SLOT_OFFHAND),
                                true);
                        }
                        else if (found == EQUIPMENT_SLOT_FINGER1 ||
                                 found == EQUIPMENT_SLOT_TRINKET1)
                        {
                            float const second = GearWornScore(
                                member.bot, member.who, static_cast<uint8>(found + 1));
                            if (second < ballot.incumbent)
                                ballot.incumbent = second;
                        }
                        ballot.wants =
                            OverseerDecisions::GearIsUpgrade(ballot.verdict, ballot.incumbent);
                    }
                }

                if (ballot.wants)
                {
                    OverseerDecisions::GearContender contender;
                    contender.name = ballot.name;
                    contender.gain = ballot.verdict.score - ballot.incumbent;
                    contender.totalWorn = GearTotalWorn(member.bot, member.who);
                    contenders.push_back(contender);
                }
                ballots.push_back(ballot);
            }

            if (ballots.empty())
                continue;

            // Two members wanting the same drop is settled here rather than by
            // the dice: the one with the lower total equipped score Needs and
            // the other Greeds, so the run raises the party's floor instead of
            // handing the item to whoever rolled higher. See GearNeedWinner.
            std::string const winner =
                needAllowed ? OverseerDecisions::GearNeedWinner(contenders) : std::string();

            for (GearBallot const& ballot : ballots)
            {
                bool const needs = !winner.empty() && ballot.name == winner;
                if (needs)
                {
                    std::string const contested =
                        contenders.size() > 1
                            ? std::string(", and is the worst geared of those who want it")
                            : std::string();
                    LOG_INFO("module.overseer",
                             "overseer: '{}' needs on {} - {} against {} for the {} it is "
                             "wearing{}",
                             ballot.name, proto->Name1, ballot.verdict.why,
                             static_cast<int>(ballot.incumbent),
                             GearSlotName(ballot.bot->FindEquipSlot(proto, NULL_SLOT, true)),
                             contested);
                }
                else
                {
                    std::string why = ballot.verdict.why;
                    if (ballot.verdict.wearable)
                        why = ballot.wants ? "another member wants it more"
                                           : "not an upgrade for it";
                    LOG_INFO("module.overseer", "overseer: '{}' greeds on {} - {}",
                             ballot.name, proto->Name1, why);
                }
                group->CountRollVote(ballot.bot->GetGUID(), rollGuid,
                                     needs ? uint8(ROLL_NEED) : uint8(ROLL_GREED));
            }
        }
    }

    // Every enabled character's talent tree, which is the only thing this drive
    // needs out of the roster table. Read on its own, on the same terms as
    // LoadJobs and LoadProfessionPlans: a schema without the column is the same
    // answer as a roster that has chosen no tree.
    std::map<std::string, uint8> LoadGearSpecs()
    {
        std::map<std::string, uint8> specs;
        QueryResult result = CharacterDatabase.Query(
            "SELECT name, spec_tab FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return specs;
        do
        {
            Field* fields = result->Fetch();
            specs[fields[0].Get<std::string>()] = fields[1].Get<uint8>();
        } while (result->NextRow());
        return specs;
    }

    void DriveGear()
    {
        std::map<std::string, uint8> const specs = LoadGearSpecs();
        if (specs.empty())
            return;

        std::vector<GearMember> members;
        for (std::pair<std::string const, uint8> const& row : specs)
        {
            // SteerableAI, not a bare lookup, for the reason that function
            // spells out at length: a name resolves to a Player through a
            // window in which touching it is a segfault.
            Player* bot = ObjectAccessor::FindPlayerByName(row.first);
            if (!SteerableAI(bot))
                continue;

            GearMember member;
            member.bot = bot;
            member.who = GearWearerFor(bot, row.second);
            members.push_back(member);
        }
        if (members.empty())
            return;

        // THE SWEEP FIRST, THE VOTE SECOND, on purpose: a character that puts
        // something on this pass is voting on the next drop against what it is
        // NOW wearing, which is the loop this issue exists to close. Doing it
        // the other way round would have every roll judged against the gear the
        // character had already decided to stop wearing.
        for (GearMember& member : members)
            SweepGear(member.bot, member.who);

        // One vote per group, not one per member: the whole family shares a
        // party, and asking each of them for its group would walk the same roll
        // list five times.
        std::set<Group*> groups;
        for (GearMember const& member : members)
            if (Group* group = member.bot->GetGroup())
                groups.insert(group);
        for (Group* group : groups)
            VoteOnOpenRolls(group, members);
    }

    // Only `new rpg` walks a character to an NPC: it owns the `wander npc
    // status` trigger (NewRpgStrategy.cpp), and NewRpgWanderNpcAction is
    // reachable through nothing else. The family carry it on the LEADER alone,
    // because five characters each free-roaming is the 937-yard scatter that
    // taking it off the followers cured - so a follower cannot be sent
    // anywhere, and travels by following. HasStrategy is PlayerbotAI.h:416.
    //
    // WITH ONE BOUNDED EXCEPTION, WHICH IS #122. Following is enough to cross
    // open ground and is not enough to cross a doorway: an areatrigger has to
    // be stood in by each character individually, and a follower has nothing in
    // its AI that will take it the last few yards on its own. So the dungeon
    // run coordinator may ESCORT a member - see the escort section below - and
    // an escorted member is walked by this drive like any other. The invariant
    // is unchanged in substance: outside a crossing, exactly one character
    // carries `new rpg`, and it is the leader.
    //
    // Named once because two callers need the same answer: DriveTravel, which
    // refuses such an errand out loud, and the arbitration below, which must
    // NOT stand the quest drive down for an errand that will never move anyone.
    bool CanBeSentToNpc(PlayerbotAI* botAI)
    {
        return botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);
    }

    // ------------------------------------------------ the ground under an aim --
    //
    // WHY AN AIM IS NOT A ROUTE, AND WHY THE DIFFERENCE KILLS PEOPLE (#138).
    //
    // #139 closed the sky falls - a leader boarding a taxi while its followers
    // walked, and a follow strategy that snapped a stranded follower onto
    // whatever the leader was standing on - and six hours of open world and a
    // whole instance run went by with no self-kill at all. The falls did not
    // stop. Measured 2026-09-02 between 19:07 and 19:19: ten of twenty-six
    // deaths in one hour were SELF-KILLS - `overseer_death` with killer_name =
    // character_name, which is fall damage - and every one of the ten was in
    // Westfall, on map 0. Three of them are one event: 19:17:22, 19:17:22 and
    // 19:17:25, and the line in the log immediately before each is the same
    // line with a different name in it,
    //
    //     'Bork' takes the wheel from `follow` for its escort to
    //     'at:0:-11022.8,1038.78,38.507'
    //
    // all three sent to the one point. Earlier the same afternoon two more died
    // INSIDE an instance being walked onto areatrigger 119 at
    // 'at:36:-14.3628,-393.38,64.5605' - a doorway standing on a ledge above the
    // entrance ramp - one of them logged at "8y from the door" and found
    // afterwards at z -20 in the ravine under it. Earlier still, three followers
    // died at the Duskwood/Westfall border while the leader took the road, one
    // of them under a bridge at z -20.
    //
    // ONE MECHANISM UNDER ALL OF IT, AND IT IS NOT A BUG IN ANY OF THE AIMS.
    // An `at:` or `trigger:` aim is a POINT. This module writes it into
    // `travel_npc`, DriveTravel hands it to ChangeToWanderNpc, patch 0012's
    // aimed wander walks it with MoveFarTo. Inside pathFinderDis (70 yards,
    // NewRpgBaseAction.h:68) MoveFarTo stops routing and hands the raw point
    // to MoveTo with `exact_waypoint` set (NewRpgBaseAction.cpp:117-120), which
    // skips the path search entirely and calls DoMovePoint
    // (MovementActions.cpp:214-227), which is MotionMaster::MovePoint
    // (MovementActions.cpp:1815-1822). That asks the core for a path - and the
    // core's answer for a point it cannot reach is not "no":
    //
    //   * A destination whose navmesh polygon cannot be found - a doorway on a
    //     ledge, a coordinate over a gorge, a corner the mesh does not cover -
    //     takes PathGenerator's hole-in-the-mesh branch (PathGenerator.cpp:
    //     275-286). For a PLAYER that branch is unconditional: the flag it
    //     tests is `creature ? creature->CanFly() : true` and `_source` is a
    //     player, so it builds a shortcut and types it PATHFIND_NORMAL |
    //     PATHFIND_NOT_USING_PATH at :284. Not NOPATH. Normal.
    //   * BuildShortcut is a TWO POINT path: here, and the destination
    //     (PathGenerator.cpp:733-746).
    //   * PointMovementGenerator follows a computed path only when it is not
    //     NOPATH *and* has more than two points (PointMovementGenerator.cpp:66).
    //     A two-point shortcut fails that test, so it splines a straight line
    //     to the raw coordinates instead (:80).
    //
    // A straight line to a point on the far side of a drop is a walk off the
    // edge, and the character is dead long before this module polls again.
    // Nothing was wrong with any of those aims: each was a place somebody
    // wanted a character to be. What was missing was the ground in between.
    //
    // WHAT IS PROVEN AND WHAT IS INFERRED, KEPT APART. The three source
    // branches above are read out of the pinned core and are certain; so is
    // the fact that every walk this module issues ends in a raw point handed
    // to that generator once it is inside pathFinderDis. What is INFERRED is
    // that this is what killed each of those ten characters: what was
    // recorded is the aim, the coordinate, the self-kill and the timing, not a
    // stack. Beyond seventy yards MoveFarTo does route first and commits to a
    // waypoint the mesh gave it, so at long range the straight line is
    // possible rather than certain - it needs that committed waypoint to be
    // off the mesh in its turn, which PATHFIND_FARFROMPOLY explicitly allows
    // (NewRpgBaseAction.cpp:123). The fix is written to the mechanism that can
    // be read in the source, and the death table is what says whether it was
    // the whole of it.
    //
    // WHAT IS DONE ABOUT IT, IN ONE PLACE. Every place-aim this module issues
    // passes through GroundedStep on its way to the mover, and GroundedStep
    // answers one question: is the point the mover is about to be handed one it
    // can WALK to from where this character stands?
    //
    //   1. IF THE NAVMESH ROUTES THERE, hand the aim through untouched. The
    //      mover computes the same path and walks it point to point along the
    //      mesh - MovebyPath is linear unless SetSmooth is called
    //      (MoveSplineInit.h:98, :159) - and a mesh path is made of walkable
    //      polygons, so it does not leave the ground. This is the ordinary
    //      case and it is byte for byte what this module did before.
    //   2. IF IT DOES NOT, the mover would draw the straight line above, so it
    //      is handed a SHORT STEP toward the destination instead - and only
    //      after the ground under that step has been walked in software,
    //      sampled every TRAVEL_GROUND_SAMPLE_YARDS and refused the moment the
    //      surface falls further than TRAVEL_GROUND_DROP_YARDS below the last
    //      footing. The next poll steps again from wherever the character then
    //      stands. This is the "maximum straight-line step" and the "refusal to
    //      step where the ground falls away" the issue asks for, and they are
    //      the same mechanism.
    //   3. IF THE DIRECT LINE IS REFUSED, the same short step is tried either
    //      side of it, nearest bearing first, before giving up - the same shape
    //      as MoveFarTo's own forward cone (NewRpgBaseAction.cpp:156-193),
    //      except deterministic, ground-checked rather than pathfinder-checked,
    //      and required to make progress.
    //
    // WHAT IT COSTS TO ASK, AND WHY THE ANSWER IS NOT CACHED. One
    // PathGenerator query per place-aimed character per poll, which is one
    // Detour search a second at the fastest cadence this drive ever runs with a
    // family of five - against MoveFarTo, which runs one per bot tick for the
    // whole of every long walk (NewRpgBaseAction.cpp:136-138). The query is
    // deliberately repeated rather than remembered: its answer depends on where
    // the character is STANDING, which is the thing that changes between polls,
    // and a cached "the mesh has a route" from sixty yards back is exactly the
    // stale fact this whole section exists to stop trusting.
    //
    // WHAT A STEPPED WALK COSTS, SO IT IS A CHOICE AND NOT A SURPRISE. Patch
    // 0012 ends an arrived place-aim in ChangeToIdle, and RPG_IDLE becomes a
    // randomly chosen status on the bot's next tick - so a character that
    // reaches a STEPPING STONE is briefly loose in exactly the way a character
    // that reaches its destination already was, and is held only by the next
    // poll renewing the lease. That renewal is DUNGEON_RUN_POLL_MS (5 s) apart
    // for anything escorted or catching up, which is every follower this
    // stepping was built for, and TRAVEL_POLL_MS (15 s) for a leader walking
    // its own errand - see the travel-poll cadence in OnUpdate, which already
    // carries this argument for the arrival case. A stepped walk is also
    // slower than a routed one, because the character finishes each step
    // before the next poll gives it another. Both are paid only where the
    // alternative was a straight line off a cliff.
    //
    // AND A LEDGE IS APPROACHED FROM THE LOW SIDE, which falls out of 2 rather
    // than being a case of its own: the only steps the ground check allows are
    // ones where the surface holds continuously from the character's feet to
    // the step, and a surface that holds continuously up onto a ledge is a
    // ramp. There is no bearing over the edge that passes.
    //
    // WHAT THIS DOES NOT DO, SAID SO IT IS NOT MISTAKEN FOR AN OVERSIGHT. It
    // does not touch `follow`. A follower in formation is moved by
    // MovementAction::Follow, which this module cannot reach without a patch,
    // and its short step is straight too (MovementActions.cpp:791-796). What
    // DriveCatchUp does about that is bounded, and stated there.

    // The surface a character stands or swims on at (x, y), looked for
    // downward from `from`. False when there is none in reach, which is what
    // the edge of the world, a hole in the terrain and a drop of more than
    // fifty yards all look like from here.
    //
    // Map.h:392, and the WATER level is what comes back where there is water
    // to swim in, so a step into a river reads as a step onto its surface
    // rather than a fifteen-yard drop to its bed. Asked of the map rather than
    // of PathGenerator on purpose: this question is only ever reached because
    // the navmesh has already failed to answer it.
    static bool SurfaceAt(Player* bot, float x, float y, float from, float& out)
    {
        Map* map = bot->GetMap();
        if (!map)
            return false;
        float const surface = map->GetWaterOrGroundLevel(
            bot->GetPhaseMask(), x, y, from, nullptr, true, bot->GetCollisionHeight());
        // GridTerrainData.h:27. Both INVALID_HEIGHT and the vmap's own
        // "nothing here" value are far below any floor in the world, so one
        // comparison catches both.
        if (surface <= INVALID_HEIGHT)
            return false;
        out = surface;
        return true;
    }

    // The highest relevant surface above the character, looked for from one
    // bounded probe rather than from MAX_HEIGHT. Map::GetHeight considers both
    // map terrain and VMAP geometry, which is the distinction the incident
    // needs: the raw terrain held the party at z 60 while the city WMO floor
    // above it was around z 95.
    static bool SurfaceAbove(Player* bot, float& out)
    {
        Map* map = bot->GetMap();
        if (!map)
            return false;
        float const z = bot->GetPositionZ() + TERRAIN_RECOVERY_PROBE_YARDS;
        float const surface = map->GetHeight(
            bot->GetPhaseMask(), bot->GetPositionX(), bot->GetPositionY(), z,
            true, TERRAIN_RECOVERY_PROBE_YARDS);
        if (surface <= INVALID_HEIGHT)
            return false;
        out = surface;
        return true;
    }

    // Does this height belong to a real local navmesh polygon? A cave, cellar
    // or building can have another surface far above it and must be left
    // alone. The raw terrain hidden under a city WMO does not have a polygon
    // at the character's height. PATHFIND_NOT_USING_PATH is explicitly not an
    // answer: for a Player, PathGenerator uses that two-point shortcut when it
    // cannot find a polygon, which is the wall-hacking fallback this recovery
    // is meant to detect.
    static bool HasLocalNavmesh(Player* bot)
    {
        static constexpr float DIRECTIONS[] = {0.f, 1.5708f, 3.1416f, 4.7124f};
        for (float delta : DIRECTIONS)
        {
            float const angle = bot->GetOrientation() + delta;
            float const x = bot->GetPositionX() + std::cos(angle) * TERRAIN_RECOVERY_PATH_YARDS;
            float const y = bot->GetPositionY() + std::sin(angle) * TERRAIN_RECOVERY_PATH_YARDS;
            PathGenerator path(bot);
            if (!path.CalculatePath(x, y, bot->GetPositionZ()))
                continue;
            PathType const type = path.GetPathType();
            if (!(type & PATHFIND_NOPATH) && !(type & PATHFIND_NOT_USING_PATH) &&
                !(type & PATHFIND_SHORTCUT) && !(type & PATHFIND_FARFROMPOLY))
                return true;
        }
        return false;
    }

    // Does the ground hold, walking in a straight line from where `bot` stands
    // to (toX, toY)? `footing` comes back as the surface at the far end, which
    // is the Z the step should be aimed at: an aim's own Z is what put a
    // character in the air in the first place.
    static bool GroundHolds(Player* bot, float toX, float toY, float& footing)
    {
        float const fromX = bot->GetPositionX();
        float const fromY = bot->GetPositionY();
        float const dx = toX - fromX;
        float const dy = toY - fromY;
        float const span = std::sqrt(dx * dx + dy * dy);
        footing = bot->GetPositionZ();
        if (span < TRAVEL_GROUND_SAMPLE_YARDS)
            return true;   // one stride, with nothing between to fall into

        uint32 const samples =
            static_cast<uint32>(span / TRAVEL_GROUND_SAMPLE_YARDS) + 1;
        for (uint32 i = 1; i <= samples; ++i)
        {
            float const t = static_cast<float>(i) / static_cast<float>(samples);
            float next = 0.f;
            float const sampleX = fromX + dx * t;
            float const sampleY = fromY + dy * t;
            // Looked for from TRAVEL_GROUND_DROP_YARDS ABOVE the last footing
            // rather than from the footing itself, so a step UPHILL finds the
            // slope it is climbing rather than the valley floor beyond it. The
            // asymmetry is the point: this check is about falling.
            if (!SurfaceAt(bot, sampleX, sampleY,
                           footing + TRAVEL_GROUND_DROP_YARDS, next))
            {
                // NOT FINDING THE GROUND IS NOT THE SAME AS STANDING OVER A
                // CLIFF, and answering both with `false` is what held
                // characters still on open hillside. The search above starts
                // TRAVEL_GROUND_DROP_YARDS over the last footing and looks
                // DOWN, so a slope gaining more than that within one sample
                // puts its own surface ABOVE the origin: the search passes
                // under the hill and reports nothing. Read literally that says
                // "no ground here", when what happened is "the ground went up",
                // and nothing falls up. Ask again from over any single-sample
                // rise before believing the first answer.
                //
                // Measured: 'Bork' refused an aim SEVEN yards away in open
                // Duskwood, and two others were held on Westfall hillside in
                // the same minute, every one of them on ground a character can
                // simply walk up.
                if (!SurfaceAt(bot, sampleX, sampleY,
                               footing + TRAVEL_GROUND_UPHILL_YARDS, next))
                    return false;
                // Nothing is forgiven on the way down. Any surface BELOW the
                // first origin was already within that search's reach, so a
                // second answer lower than the footing is not a hill, and the
                // drop test immediately below still has to pass on it.
            }
            if (footing - next > TRAVEL_GROUND_DROP_YARDS)
                return false;
            footing = next;
        }
        return true;
    }

    // Is anything STANDING between where `p` is and the point it is about to be
    // stepped to? GroundHolds answers how high the ground is along that line
    // and never what is on it, so a wall passes every one of its samples:
    // solid ground on both sides of it, no drop between them, approved.
    //
    // That is exactly how characters were stepped through Stormwind's walls
    // and ended up under the world. Measured the night this was written: the
    // operator watched a character standing on a featureless plane beneath the
    // city while the server had all five on the street at z = 95.5, and three
    // died at z = -508, -516 and -526 where the terrain sits near 30 to 50.
    // Those were never falls off a ledge, they were characters leaving the
    // world through geometry this check could not see (#181, #174).
    //
    // ASKED AT BODY HEIGHT, NOT AT THE FEET, because the question is whether a
    // WALL is in the way and not whether a kerb is. GetCollisionHeight is the
    // character's own height, so this scales with race and shapeshift instead
    // of guessing a constant.
    //
    // LINEOFSIGHT_ALL_CHECKS with ModelIgnoreFlags::Nothing is what upstream
    // uses when it asks this same question of this same map
    // (FishingAction.cpp:109); the signature is Map.h:394.
    static bool NothingInTheWay(Player* p, float toX, float toY, float toFooting)
    {
        Map* map = p->GetMap();
        if (!map)
            return false;
        float const eye = p->GetCollisionHeight();
        return map->isInLineOfSight(
            p->GetPositionX(), p->GetPositionY(), p->GetPositionZ() + eye,
            toX, toY, toFooting + eye,
            p->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
            VMAP::ModelIgnoreFlags::Nothing);
    }

    // Will the mover follow the navmesh from where `bot` stands to (x, y, z),
    // or will it draw a straight line? The test is the mover's OWN
    // (PointMovementGenerator.cpp:66) rather than a judgement of this module's,
    // so the two can never disagree about what is about to happen.
    static bool NavmeshRoutes(Player* bot, float x, float y, float z)
    {
        PathGenerator path(bot);   // PathGenerator.h:61
        if (!path.CalculatePath(x, y, z))
            return false;
        return !(path.GetPathType() & PATHFIND_NOPATH) && path.GetPath().size() > 2;
    }

    // The point to hand the mover THIS POLL for a character wanted at `want`.
    // False when every direction was refused, which the caller must read as "do
    // not re-aim this poll" rather than as an error: standing still at the top
    // of a cliff is the correct thing to do.
    static bool GroundedStep(Player* bot, WorldPosition const& want, WorldPosition& step)
    {
        float wx = want.GetPositionX();
        float wy = want.GetPositionY();
        float wz = want.GetPositionZ();
        float const span = bot->GetExactDist2d(wx, wy);   // Position.h:170

        // THE AIM'S OWN Z IS CORRECTED ONTO THE SURFACE UNDER IT, and only
        // when the character is near enough for that to be cheap: sampling the
        // ground four thousand yards away would CREATE the map grid there
        // (Map::GetGridTerrainData calls EnsureGridCreated, Map.cpp:1100-1107),
        // which is a page of the world loaded to answer a question about a walk
        // that has not started. Near the aim it is worth doing, because the two
        // or three yards between a doorway's recorded coordinates and the floor
        // under it are exactly the difference between the navmesh finding a
        // polygon for it and not: GetPolyByLocation's first search reaches five
        // yards above and below the point before it widens to fifty
        // (PathGenerator.cpp:233, :247).
        if (span <= TRAVEL_STEP_YARDS)
        {
            float surface = 0.f;
            if (SurfaceAt(bot, wx, wy, wz + TRAVEL_GROUND_SNAP_YARDS, surface) &&
                std::fabs(surface - wz) <= TRAVEL_GROUND_SNAP_YARDS)
                wz = surface;
        }

        // 1. THE NAVMESH FIRST, AND USUALLY THE NAVMESH IS ENOUGH.
        if (NavmeshRoutes(bot, wx, wy, wz))
        {
            step = WorldPosition(want.GetMapId(), wx, wy, wz);
            return true;
        }

        // Standing on it already. Nothing to step toward and nothing to check;
        // the arrival branches in DriveTravel are what act on this.
        if (span < 1.0f)
        {
            step = WorldPosition(want.GetMapId(), wx, wy, wz);
            return true;
        }

        // 2. AND OTHERWISE A SHORT STEP, OVER GROUND THAT HOLDS.
        float const bearing = bot->GetAngle(wx, wy);   // Position.h:190
        // The direct line, then either side of it, nearest bearing first.
        // Five and no more: this runs once per stepping character per poll and
        // each candidate is a walk over the terrain. Thirty and sixty degrees,
        // written in radians because that is what GetAngle returns.
        //
        // NINETY DEGREES IS GONE, AND ITS ABSENCE IS PART OF THE FIX. A step at
        // a right angle to the aim lands sqrt(span^2 + reach^2) from it, which
        // is longer than span for every span and every reach there is, so the
        // forward-progress rule below rejected that bearing on arithmetic every
        // time it was offered. The fan read as seven candidates and was five.
        static constexpr float FAN[] = {
            0.f, 0.5236f, -0.5236f, 1.0472f, -1.0472f };
        for (float delta : FAN)
        {
            // AN ANGLED PROBE WALKS LESS THAN THE WAY HOME, or it cannot be a
            // step forward either. Stepping the whole remaining distance at an
            // angle lands 2*span*sin(delta/2) from the aim, which reaches span
            // exactly at sixty degrees, so while reach was span the wide
            // bearings failed the progress test on geometry alone. reach IS
            // span for every aim inside TRAVEL_STEP_YARDS, which is most of
            // them, so a character close to its errand was really being offered
            // three bearings rather than five. Shortening only the angled
            // probes restores them; the straight one still steps as far as it
            // may, so nothing about the ordinary case changes.
            float const reach = delta == 0.f
                ? std::min(span, TRAVEL_STEP_YARDS)
                : std::min(span * TRAVEL_STEP_SIDE_FRACTION, TRAVEL_STEP_YARDS);
            float const angle = bearing + delta;
            float const sx = bot->GetPositionX() + std::cos(angle) * reach;
            float const sy = bot->GetPositionY() + std::sin(angle) * reach;
            // A step to the side has to be a step FORWARD as well, or a
            // character at a dead end walks in a circle around it forever.
            float const ddx = wx - sx;
            float const ddy = wy - sy;
            if (delta != 0.f && std::sqrt(ddx * ddx + ddy * ddy) >= span)
                continue;
            float footing = 0.f;
            if (!GroundHolds(bot, sx, sy, footing))
                continue;
            // The ground holding is not enough on its own: it says nothing
            // about what stands on that ground. See NothingInTheWay.
            if (!NothingInTheWay(bot, sx, sy, footing))
                continue;
            step = WorldPosition(want.GetMapId(), sx, sy, footing);
            return true;
        }
        return false;
    }

    // Does the ground hold under the single straight step `follow` is about to
    // take from this follower toward its master (#138)?
    //
    // Past SightDistance (100, PlayerbotAIConfig.cpp:109) upstream's Follow()
    // stops chasing continuously and issues one MoveTo(target, followDistance)
    // per attempt (MovementActions.cpp:1224). That MoveTo is a single straight
    // step of at most SpellDistance (28.5, PlayerbotAIConfig.cpp:110) toward the
    // master, with its Z interpolated toward the MASTER'S Z before being clamped
    // to whatever is under the step (MovementActions.cpp:791-796). It consults
    // nothing about what lies between. So the step is walked here first, over
    // the same distance upstream is about to use.
    static bool FollowStepHolds(Player* p, Player* leader)
    {
        float const dx = leader->GetPositionX() - p->GetPositionX();
        float const dy = leader->GetPositionY() - p->GetPositionY();
        float const span = std::sqrt(dx * dx + dy * dy);
        if (span < 1.0f)
            return true;
        float const t = std::min(1.0f, sPlayerbotAIConfig.spellDistance / span);
        float footing = 0.f;
        float const toX = p->GetPositionX() + dx * t;
        float const toY = p->GetPositionY() + dy * t;
        // Both halves, for the same reason the stepping fan asks both: upstream's
        // Follow draws its short step straight, so a wall between the follower
        // and its master is walked through exactly as one on a travel step is.
        return GroundHolds(p, toX, toY, footing) && NothingInTheWay(p, toX, toY, footing);
    }

    // ------------------------------------------------------------- escort --
    //
    // WHAT AN ESCORT IS, AND WHY IT HAD TO EXIST (#122).
    //
    // A grouped follower has no way to move itself. `new rpg` is added in
    // exactly one place upstream (AiFactory.cpp:615) and that place is gated
    // twice over: on `IsRandomBot`, which a roster character on a named account
    // can never be, and on `!GetGroup() || GetGroup()->GetLeaderGUID() ==
    // GetGUID()`, which a follower can never be either. So a follower's whole
    // repertoire is `follow` - and `follow` cannot cross an instance portal,
    // because FollowAction only acts while the master is on the same map, and
    // the thing that actually crosses is a CMSG_AREATRIGGER sent by a character
    // standing inside the trigger's own radius. Measured live, twice: the
    // leader walks in under his own power and the other four stop at the door.
    //
    // AN ESCORT IS A LEASE ON THAT ONE STRATEGY, HELD BY A RUN. While the
    // coordinator wants a member at a particular point it escorts it there: the
    // aim goes in `travel_npc` like every other aim, and DriveTravel grants
    // `new rpg` at the instant it issues the walk - see the grant site, which
    // is deliberately adjacent to ChangeToWanderNpc rather than anywhere
    // earlier. A freshly granted `new rpg` starts at RPG_IDLE, and
    // NewRpgStatusUpdateAction turns RPG_IDLE into a RANDOMLY CHOSEN status on
    // its very next tick - GO_GRIND and GO_CAMP both pick a position out of the
    // world and walk to it. Granting the strategy anywhere other than
    // immediately before the aim is written into `rpgInfo` is therefore a race
    // whose losing side is a follower wandering off across Westfall. Granting
    // it one statement earlier leaves no tick in which the status is IDLE.
    //
    // AND IT IS A LEASE RATHER THAN A GRANT because `new rpg` outranks `follow`
    // in the same engine - "new rpg status update" is relevance 11 against
    // FollowMasterStrategy's 1 - so a follower that keeps it keeps ignoring its
    // master. It is handed back the moment the run stops wanting the member
    // where it asked for it.
    //
    // THE HAND-BACK IS A SWEEP, NOT TWELVE CALL SITES. DriveDungeonRun has a
    // dozen ways to leave a phase (crossed, gave up, job cleared, portal row
    // gone, leader vanished, adopted after a restart) and an escort that
    // outlives ONE of them is the scatter this module spent three fixes
    // removing. So the coordinator MARKS what it still wants at the point it
    // wants it, and the next poll ends everything unmarked - the same shape
    // TravelAimBook::PruneVanished already uses for aims, spread over two polls
    // because a `return` cannot be made to run an epilogue. Five seconds of
    // lag, and no way to forget.
    //
    // World-thread-only and unguarded, like the coordinator state it belongs
    // to. A restart loses it, and a restart also wipes every strategy in every
    // bot's engine, so the two are lost consistently.
    struct DungeonEscort
    {
        // Whether THIS module granted `new rpg` for the escort, so only what
        // was granted is taken back. The leader carries it in his own right and
        // must keep it; taking it off him at the end of a crossing would strand
        // the whole family, which is the bug this file already has three
        // comments about.
        bool granted{false};
        // Whether the poll now running still wants this member escorted. Set by
        // EscortToward, cleared and acted on by SweepDungeonEscorts.
        bool wanted{false};
        // What it was last asked to walk to, so "escorting X to Y" is said once
        // per destination rather than once per five-second poll - the same
        // log-once discipline every other drive in this file keeps.
        std::string aim;
        // WHOSE ESCORT THIS IS (#138). False: a dungeon run's, marked by
        // EscortToward and swept by SweepDungeonEscorts on the coordinator's
        // clock. True: the follow drive's catch-up, marked by CatchUpToward
        // and swept by SweepCatchUps on the party's clock. Two sweeps rather
        // than one because the two clocks differ by a factor of six, and a
        // catch-up marked every thirty seconds would be ended by a sweep that
        // runs every five. The run's escort outranks the catch-up: EscortToward
        // takes an entry over unconditionally, CatchUpToward never touches an
        // entry that is not its own.
        bool catchUp{false};
        // WHETHER THIS CATCH-UP MAY STILL DECIDE TO FLY (#138). Set by
        // CatchUpToward on the poll that STARTS the walk and spent by
        // ConsiderFlight the first time flying is genuinely on the table,
        // whichever way that decision then goes. Meaningful only while
        // `catchUp`; see ConsiderFlight for the four fares it is about.
        bool mayFly{false};
        // Where the catch-up aim was last pointed, so "has the leader moved
        // far enough from it to re-aim" is answered from memory rather than by
        // parsing the aim string back. Meaningful only while `catchUp`.
        float x{0.f};
        float y{0.f};
    };
    std::map<std::string, DungeonEscort> _dungeonEscorts;

    bool IsEscorted(std::string const& name) const
    {
        return _dungeonEscorts.find(name) != _dungeonEscorts.end();
    }

    bool IsCatchingUp(std::string const& name) const
    {
        auto const it = _dungeonEscorts.find(name);
        return it != _dungeonEscorts.end() && it->second.catchUp;
    }

    // Ask for a member to be walked to `aim`, and say so once. Idempotent: a
    // re-ask while the same aim is in flight is what every poll of a crossing
    // does, and TravelAimBook::Claim already refuses to disturb a walk it is
    // holding the memory for.
    void EscortToward(std::string const& name, std::string const& aim, char const* what)
    {
        DungeonEscort& escort = _dungeonEscorts[name];
        escort.wanted = true;
        // The run takes over a catch-up in progress rather than queueing
        // behind it (#138): both walk the member toward the family, the run
        // knows where the family is going, and the lease it inherits -
        // `granted` - is handed back by the run's own sweep exactly as if the
        // run had granted it.
        if (escort.catchUp)
        {
            escort.catchUp = false;
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} takes over '{}' from its catch-up walk - "
                     "the run's staging point is where the leader is going anyway",
                     what, name);
        }
        _travelAims.Claim(name, aim);
        if (escort.aim == aim)
            return;

        escort.aim = aim;
        LOG_INFO("module.overseer",
                 "overseer: dungeon run {} escorts '{}' to {} under its own power - "
                 "following gets a character across open ground, it does not get one "
                 "through a doorway", what, name, aim);
    }

    // THE CATCH-UP WALK (#138): a follower too far behind to be following is
    // given the leader's position as its own aim, through the same lease a run
    // uses, until `follow` can reach it again. Returns true when this call
    // STARTED the walk, so the caller can say so once; a re-aim over a walk in
    // progress is silent here and shows up as DriveTravel's own re-issue.
    //
    // Refuses to touch an escort a run holds: the run's staging point is the
    // authority on where this member should be, and the run's sweep is the
    // authority on when it stops. See DungeonEscort::catchUp.
    bool CatchUpToward(std::string const& name, Player* leader)
    {
        DungeonEscort& escort = _dungeonEscorts[name];
        bool const started = escort.aim.empty();
        if (!started && !escort.catchUp)
            return false;

        std::ostringstream aim;
        aim << "at:" << leader->GetMapId() << ':' << leader->GetPositionX() << ','
            << leader->GetPositionY() << ',' << leader->GetPositionZ();

        escort.catchUp = true;
        escort.wanted = true;
        // THE FLIGHT DECISION BELONGS TO THE POLL THAT STARTS THE WALK (#138).
        // Every re-aim below is a new `travel_npc` value and so a new errand
        // with a fresh flight budget; without this the same journey would keep
        // buying tickets. See ConsiderFlight.
        if (started)
            escort.mayFly = true;
        escort.x = leader->GetPositionX();
        escort.y = leader->GetPositionY();
        escort.aim = aim.str();
        _travelAims.Claim(name, escort.aim);
        return started;
    }

    // Ends a catch-up the party's clock has stopped marking - the same shape
    // as SweepDungeonEscorts, for the entries that sweep leaves alone. The
    // ordinary end is DriveCatchUp's own, on the poll that measures the gap
    // closed; this is the backstop for the polls that never reach it. Runs first,
    // every poll of KeepRosterGrouped, before any `return` in that function
    // or in KeepRosterFollowing can forget one: a party that dissolves, a
    // leader that logs out, a roster that shrinks to one, all reach here on
    // the next poll with nothing marked, and the walk ends and the wheel goes
    // back to `follow`.
    void SweepCatchUps()
    {
        for (auto it = _dungeonEscorts.begin(); it != _dungeonEscorts.end(); )
        {
            if (!it->second.catchUp)
            {
                ++it;
                continue;
            }
            if (it->second.wanted)
            {
                it->second.wanted = false;
                ++it;
                continue;
            }
            EndOneEscort(it->first, it->second.granted);
            it = _dungeonEscorts.erase(it);
        }
    }

    // Is the leader standing somewhere a follower can be sent? A position in
    // the air is not an aim - it is the exact landing point the issue is
    // about. IsInFlight is Unit.h:1709 and IsFalling Unit.h:1718, both public;
    // IsFalling reads the movement flags AND the spline (Unit.cpp:15957-15961),
    // so a leader mid-drop off a ledge is caught as well as one on a taxi.
    // Asked of the WORLD rather than of this module's own flight bookkeeping,
    // for the reason DriveTravel's in-flight guard gives: a taxi upstream
    // rolled on its own is still a taxi.
    static bool OnTheGround(Player* p)
    {
        return p && p->IsInWorld() && !p->IsInFlight() && !p->IsFalling();
    }

    // ONE FOLLOWER, ONE POLL: should it be walking to the leader on its own,
    // and if it already is, should it still be (#138). Called from
    // KeepRosterFollowing for every follower in the group, after the master
    // and `follow` are known to be correct - this is about whether following
    // can WORK from where the follower stands, which is the question nothing
    // above it asks. See FOLLOW_CATCH_UP_YARDS for the measurement.
    void DriveCatchUp(Player* p, Player* leader)
    {
        std::string const name = p->GetName();
        auto const it = _dungeonEscorts.find(name);
        bool const escorted = it != _dungeonEscorts.end();
        // A run holds this member. Its staging point is where the leader is
        // going, so the run's walk IS the catch-up, and the run's sweep is the
        // authority on when it stops.
        if (escorted && !it->second.catchUp)
            return;

        // GetMapId  Position.h:281; GetDistance2d  Object.h:537-538. Negative
        // means "not on the leader's map", which an `at:` aim cannot cross -
        // ResolveTravelTarget refuses a spawn on another map - and which
        // following cannot cross either (FollowActions.cpp:285).
        float const gap =
            p->GetMapId() == leader->GetMapId() ? p->GetDistance2d(leader) : -1.f;

        if (escorted)
        {
            // THE WAYS IT ENDS, each said once because each is a decision,
            // and each ended HERE rather than left for the sweep: the sweep is
            // the backstop for a poll that never got this far, and thirty
            // seconds of a follower holding `new rpg` beside a leader it could
            // simply follow is thirty seconds it might roll a status of its
            // own. The read-back of the hand-back is EndOneEscort's line.
            //
            // ...AND IT ONLY ENDS ONTO GROUND `follow` CAN ACTUALLY CROSS
            // (#138). Being close enough to be followed is half the answer;
            // the other half is whether the step `follow` would take from here
            // goes over anything. Handing a follower back at the lip of a
            // gorge is handing it back to the straight step that put the
            // Duskwood three at the bottom of one. See FollowStepHolds.
            //
            // THIS CAN HOLD A FOLLOWER ON ITS OWN AIM INDEFINITELY, and that
            // is the right trade rather than an oversight. For it to happen
            // the straight line to the leader has to keep crossing a drop
            // while the follower keeps walking toward him under a checked
            // aim - which means the follower cannot reach the leader at all,
            // which is precisely the case where handing it back to `follow`
            // is handing it a fall. It stays loud: DriveTravel's own
            // twenty-minute backstop releases the aim as unreachable and says
            // so, and the next party poll claims it again from wherever the
            // follower now stands.
            if (gap < 0.f ||
                (gap <= FOLLOW_CATCH_UP_DONE_YARDS && FollowStepHolds(p, leader)))
            {
                if (gap < 0.f)
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is no longer on the map '{}' is on - its "
                             "catch-up walk ends, there is nowhere on this map to walk to",
                             name, leader->GetName());
                else
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is back within {} yards of '{}' on ground it "
                             "can cross - its catch-up walk ends and `follow` takes it "
                             "from here",
                             name, static_cast<uint32>(gap), leader->GetName());
                EndOneEscort(name, it->second.granted);
                _dungeonEscorts.erase(it);
                return;
            }

            it->second.wanted = true;
            // THE AIM FOLLOWS THE LEADER, BUT ONLY ACROSS SOLID GROUND. A
            // leader who has walked on is re-aimed at, measured from memory of
            // where the last aim pointed, at FOLLOW_CATCH_UP_REAIM_YARDS - see
            // that constant for why the figure is what it is. A leader in the
            // air is simply not re-aimed at: the follower keeps walking to the
            // last place he stood, which is a place a character can stand.
            //
            // RE-CLAIMED EVERY POLL EITHER WAY, exactly as the run's
            // EscortToward is. TravelAimBook::Claim is idempotent against the
            // walk it remembers and writes only when that memory is gone -
            // which is what DriveTravel's twenty-minute backstop does when it
            // releases a walk as unreachable. Without this the follower would
            // keep `new rpg` with no aim under it, and a `new rpg` with no aim
            // goes IDLE and then wherever NewRpgStatusUpdateAction rolls.
            if (OnTheGround(leader) &&
                leader->GetDistance2d(it->second.x, it->second.y) > FOLLOW_CATCH_UP_REAIM_YARDS)
                CatchUpToward(name, leader);
            else
                _travelAims.Claim(name, it->second.aim);
            return;
        }

        // NOT WALKING YET. Should it be?
        //
        // TWO REASONS, AND THE SECOND ONE IS NEW (#138). The first is
        // distance: past FOLLOW_CATCH_UP_YARDS a follower is stranded rather
        // than following. The second is the ground: measured 2026-09-02 at
        // 17:09, the leader walked the Duskwood road from (-10234,-362) west
        // to (-10523,137), and three followers took `follow`'s straight step
        // across the gorge beside it instead of the road over it. All three
        // died to themselves inside nine seconds, one of them found at z -20
        // under the bridge. So a follower whose next step off the formation is
        // into thin air is taken off `follow` and walked under its own aim,
        // which goes through GroundedStep like every other aim this module
        // writes.
        //
        // AND ONLY WHILE THE PARTY IS ALREADY STRUNG OUT, which is the bound
        // that makes this safe to add at all. Inside FOLLOW_STALL_GAP_YARDS a
        // follower is in formation beside the people it heals and tanks for,
        // and taking `follow` off it there to walk it around a rock would cost
        // the cohesion this file has three separate comments about. Past that
        // line it is already depending on one discrete straight step per
        // attempt landing (MovementActions.cpp:1224), and this check has just
        // found that the step does not land - so there is nothing to preserve.
        // A follower in formation - or on another map, which is what a
        // negative gap means - is nothing this drive has an opinion about, and
        // answering that first keeps every reading below off the common path.
        if (gap <= FOLLOW_STALL_GAP_YARDS)
            return;
        // A dead follower is a ghost walking to its corpse, which is
        // DriveStuckRevival's business and not a distance from anybody; a
        // follower already on a taxi is closing the gap faster than a walk
        // would; a follower inside a run is the run's. And the leader has to
        // be standing on something, for the reason OnTheGround gives.
        if (!p->IsAlive() || p->IsInFlight() || InDungeonRun(p))
            return;
        // And a follower held after a revival is standing still on purpose,
        // for a few seconds - see HoldAfterRevival.
        if (HeldAfterRevival(name))
            return;
        if (!leader->IsAlive() || !OnTheGround(leader))
            return;

        // Asked LAST, because it is the only reading here that walks terrain,
        // and by this point every cheaper reason to do nothing has been ruled
        // out.
        bool const stranded = gap > FOLLOW_CATCH_UP_YARDS;
        if (!stranded && FollowStepHolds(p, leader))
            return;

        if (CatchUpToward(name, leader))
            LOG_INFO("module.overseer",
                     "overseer: '{}' is {} yards behind '{}' and {} - so it is released "
                     "to walk to where the leader stands under its own aim, and handed "
                     "back within {} yards on ground it can cross",
                     name, static_cast<uint32>(gap), leader->GetName(),
                     stranded ? "past anything `follow` will do for it"
                              : "the step `follow` would take from there goes over a drop",
                     static_cast<uint32>(FOLLOW_CATCH_UP_DONE_YARDS));
    }

    // Recorded by DriveTravel at the instant it grants the strategy, so the
    // hand-back takes back exactly what was given and nothing else.
    void NoteEscortGranted(std::string const& name)
    {
        auto it = _dungeonEscorts.find(name);
        if (it != _dungeonEscorts.end())
            it->second.granted = true;
    }

    void EndOneEscort(std::string const& name, bool granted)
    {
        _travelAims.Release(name);
        if (!granted)
            return;

        Player* bot = ObjectAccessor::FindPlayerByName(name);
        PlayerbotAI* botAI = SteerableAI(bot);
        if (!botAI)
            // Logged out, mid-teardown, or the worldserver bounced - either way
            // the engine that held the strategy is gone with it. Nothing to
            // hand back and nothing wrong.
            return;

        botAI->ChangeStrategy("-new rpg", BOT_STATE_NON_COMBAT);
        // READ BACK, because `delivered` is not `done` anywhere in this module
        // and a strategy that failed to come off is a follower that never
        // follows again.
        if (botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            LOG_ERROR("module.overseer",
                      "overseer: '{}' still carries `new rpg` after the escort ended - it "
                      "will keep travelling on its own instead of following the leader",
                      name);
        else
            LOG_INFO("module.overseer",
                     "overseer: '{}' hands the wheel back to `follow` - its escort is over",
                     name);
    }

    // Runs first, every poll of DriveDungeonRun, so no `return` in that
    // function can leak an escort. See the section comment above.
    void SweepDungeonEscorts()
    {
        for (auto it = _dungeonEscorts.begin(); it != _dungeonEscorts.end(); )
        {
            // A catch-up is marked on the party's clock and swept by
            // SweepCatchUps; this sweep would end it five seconds after it
            // started (#138).
            if (it->second.catchUp)
            {
                ++it;
                continue;
            }
            if (it->second.wanted)
            {
                it->second.wanted = false;
                ++it;
                continue;
            }
            EndOneEscort(it->first, it->second.granted);
            it = _dungeonEscorts.erase(it);
        }
    }

    // ------------------------------------------------------- escort focus --
    //
    // AN ESCORT TRAVELS AND NOTHING ELSE (#164).
    //
    // WHAT WAS WATCHED, TWICE, TWELVE HOURS APART. A staging party of five: four
    // at or inside the instance door and the fifth 500 yards back in Westfall
    // with a Herb Gathering cast bar, indefinitely, so the run could not start
    // because BARRIER holds for everybody to reach the staging point. The module
    // had just handed him `new rpg` itself, which is the escort working as
    // designed - a character 500 yards back cannot be moved by `follow`, so it
    // is given the one strategy that travels under its own aim - and `gather`,
    // `loot` and `quest` were still on the engine beside it, so the walk to the
    // dungeon became ordinary open-world time.
    //
    // The same night the same shape killed the LEADER. Under a staging aim, he
    // was held on the spot by GroundedStep, which had found that every direction
    // out of where he stood was a drop and said so; `grind`, granted seconds
    // earlier by an upkeep cycle with no idea a run was staging, then walked him
    // off that drop. He was found at z = -511 - through the floor, not off a
    // ledge - killed by himself.
    //
    // SO THE RULE IS NOT ABOUT POLITENESS. Every walk this module issues goes
    // through GroundedStep, and GroundedStep is only ever as strong as the
    // guarantee that nothing else is steering the character. While a character
    // is travelling, everything that could steer it stands down; see
    // ESCORT_DIVERT_STRATEGIES for what that set is and how each name in it was
    // arrived at.
    //
    // WHAT COUNTS AS TRAVELLING HERE. Any character with a live errand in
    // `travel_npc` that it can actually act on. The dungeon run's BARRIER
    // escort, DriveCatchUp's catch-up walk and an operator's own travel aim are
    // three names for one row in one column, and from this angle they are the
    // same thing. THE LEADER IS COVERED BY THE SAME RULE AS A FOLLOWER, which
    // the death above makes non-negotiable: an implementation that stripped
    // diverters only from escorted followers would leave that death reachable,
    // unchanged.
    //
    // IT IS ASSERTED EVERY POLL, NOT ONCE. The grants that killed the leader
    // arrived mid-escort from an upkeep cycle that does not know a run exists,
    // and the window between the grant and the death was seconds. A set taken
    // down once at the start of the walk would have been taken down before the
    // grant that mattered. Re-asserting is cheap - it reads the live list and
    // writes nothing when there is nothing to write - and every re-take is
    // logged, so the second-order bug (something granting `+grind` to a
    // character mid-escort) is visible in the log every time it happens rather
    // than only when it kills somebody.
    //
    // AND IT IS SYMMETRIC, WHICH IS THE PART THAT HAD TO BE PROVABLE. Only
    // strategies that were PRESENT are taken; only strategies OBSERVED TO HAVE
    // COME OFF are recorded, because `delivered` is not `done` anywhere in this
    // module and a restore list containing something that never came off would
    // ADD a strategy the character did not have; and only what was recorded is
    // put back. A character that had `gather` before its escort has it after,
    // and one that did not, does not.
    struct TravelFocus
    {
        // Exactly what this module took off this character's non-combat engine,
        // and nothing else. This list IS the restore, so anything that is not in
        // it is never given back and anything in it always is.
        std::vector<std::string> stoodDown;
        // Which refusals have already been reported. A strategy that will not
        // come off is retried on every poll, on purpose - it may come off on the
        // next one - and an ERROR repeated every five seconds for a whole
        // staging window is exactly the noise that buries the one line worth
        // reading, which this file complains about in four other places. Said
        // once per strategy per errand instead.
        std::vector<std::string> refusedSaid;
        // What the errand was, for the log lines that name it. Kept here
        // rather than read back out of TravelAimBook so that a caller holding
        // only a name can still say where the character was going.
        std::string aim;
    };
    // World-thread-only and unguarded, like the escort book above it and for the
    // same reason: DriveTravel and DriveDungeonRun both run from OnUpdate. A
    // restart loses it, and a restart also rebuilds every bot's engine from the
    // config it was going to be restored to, so the two are lost consistently.
    std::map<std::string, TravelFocus> _travelFocus;

    // Take down every diverter that is on this character's non-combat engine and
    // is not already recorded as taken, and record what actually came off.
    // Returns what it took THIS call, which is empty on every poll after the
    // first unless something put a strategy back.
    std::string TakeDownDiverters(std::string const& name, PlayerbotAI* botAI)
    {
        TravelFocus& focus = _travelFocus[name];

        std::string change;
        std::vector<std::string> taking;
        for (char const* strategy : ESCORT_DIVERT_STRATEGIES)
        {
            // Already ours. Asked before the engine is, because a strategy this
            // module took is one the engine will say is absent, and re-taking it
            // would be a second entry in the restore list.
            if (std::find(focus.stoodDown.begin(), focus.stoodDown.end(), strategy) !=
                focus.stoodDown.end())
                continue;
            // StrategyPresent, the same predicate the command surface reads its
            // verdicts with, so "is it on" can never mean two different things
            // in one file. `false` is the non-combat engine.
            if (!StrategyPresent(botAI, StrategyItem{strategy, false}))
                continue;
            if (!change.empty())
                change += ',';
            change += '-';
            change += strategy;
            taking.push_back(strategy);
        }

        if (change.empty())
            return std::string();

        // .c_str() for the reason DriveEngagementSafety's own built strip gives:
        // every other ChangeStrategy in this file passes a literal, so a built
        // string is the one call that has to work whichever parameter type
        // upstream picked.
        botAI->ChangeStrategy(change.c_str(), BOT_STATE_NON_COMBAT);

        std::string took;
        std::string refused;
        for (std::string const& strategy : taking)
        {
            if (StrategyPresent(botAI, StrategyItem{strategy, false}))
            {
                if (std::find(focus.refusedSaid.begin(), focus.refusedSaid.end(),
                              strategy) != focus.refusedSaid.end())
                    continue;
                focus.refusedSaid.push_back(strategy);
                if (!refused.empty())
                    refused += ", ";
                refused += strategy;
                continue;
            }
            focus.stoodDown.push_back(strategy);
            if (!took.empty())
                took += ", ";
            took += strategy;
        }

        if (!refused.empty())
            // ERROR, because this is the failure that looks like success from
            // the outside: the module decided to take the wheel, the log says it
            // did, and the character can still be steered off ground the
            // grounded step refused. Said once per strategy per errand - see
            // TravelFocus::refusedSaid.
            LOG_ERROR("module.overseer",
                      "overseer: '{}' is travelling and {} would not come off its "
                      "non-combat engine - it can still be steered away from the walk, "
                      "including off ground this module has already refused to walk it "
                      "off", name, refused);
        return took;
    }

    // ONE TRAVELLING CHARACTER, ONE POLL. Idempotent and cheap: it reads the
    // live strategy list and writes only when something is actually there to
    // take. Returns what it took, for a caller that wants to say so in its own
    // words.
    std::string AssertTravelFocus(std::string const& name, PlayerbotAI* botAI,
                                  std::string const& aim)
    {
        // ASKED BEFORE THE TAKE, because TakeDownDiverters creates the record.
        // "Is this the first poll of this focus" is the whole difference between
        // the two lines below: anything taken on the first poll is what the
        // character was carrying when it set off, and anything taken after that
        // is something that arrived while it was already walking.
        bool const fresh = _travelFocus.find(name) == _travelFocus.end();

        std::string const took = TakeDownDiverters(name, botAI);
        TravelFocus& focus = _travelFocus[name];
        focus.aim = aim;
        if (took.empty())
            return took;

        if (fresh)
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' is travelling to '{}' and stands down {} for the "
                     "trip - an escort travels and nothing else, and every one of them "
                     "goes back exactly as it was when the errand ends",
                     name, aim, took);
            return took;
        }

        // A DIVERTER THAT CAME BACK MID-ESCORT, which is the second-order bug
        // this line exists to make visible: something granted it to a character
        // that was already being walked somewhere, knowing nothing about the
        // walk. WARN rather than INFO because the last time it happened
        // unobserved it ended in a death.
        LOG_WARN("module.overseer",
                 "overseer: '{}' had {} put back on its non-combat engine while it was "
                 "travelling to '{}' - taken off again. Something is granting "
                 "strategies to a character that is mid-escort",
                 name, took, aim);
        return took;
    }

    // The same, for a caller that has only a name - the dungeon run coordinator,
    // which re-asserts on its own five-second clock rather than waiting for the
    // travel poll. DELIBERATELY DOES NOT CREATE A FOCUS: the errand loop is the
    // one place that knows a character has an aim it can act on, and a focus
    // created here for a row that loop has not seen yet would be swept back off
    // on its next poll and taken again on the one after.
    std::string AssertTravelFocus(std::string const& name)
    {
        auto const it = _travelFocus.find(name);
        if (it == _travelFocus.end())
            return std::string();
        std::string const aim = it->second.aim;

        Player* bot = ObjectAccessor::FindPlayerByName(name);
        PlayerbotAI* botAI = SteerableAI(bot);
        if (!botAI)
            return std::string();
        return AssertTravelFocus(name, botAI, aim);
    }

    // GIVE BACK EXACTLY WHAT WAS TAKEN, and nothing else. The one terminal path,
    // the same way TravelAimBook::Release is the one terminal path for an
    // errand: the record is erased here whatever happens next, so a character
    // whose engine has gone cannot leave a restore list behind to be applied to
    // some later engine.
    void RestoreTravelFocus(std::string const& name)
    {
        auto it = _travelFocus.find(name);
        if (it == _travelFocus.end())
            return;
        std::vector<std::string> const stoodDown = it->second.stoodDown;
        std::string const aim = it->second.aim;
        _travelFocus.erase(it);
        if (stoodDown.empty())
            return;

        Player* bot = ObjectAccessor::FindPlayerByName(name);
        PlayerbotAI* botAI = SteerableAI(bot);
        if (!botAI)
            // Logged out, mid-teardown, or the worldserver bounced - the engine
            // that held these is gone with it, and AiFactory builds a fresh one
            // from the config when the character comes back. Nothing to give
            // back and nothing wrong; the same argument EndOneEscort makes about
            // the strategy it hands back.
            return;

        std::string change;
        for (std::string const& strategy : stoodDown)
        {
            if (!change.empty())
                change += ',';
            change += '+';
            change += strategy;
        }
        botAI->ChangeStrategy(change.c_str(), BOT_STATE_NON_COMBAT);

        // READ BACK, because `delivered` is not `done` anywhere in this module
        // and because the whole claim this section makes is that the character is
        // the character it was before the trip. A claim nobody checked is a
        // comment.
        std::string back;
        std::string missing;
        for (std::string const& strategy : stoodDown)
        {
            std::string& into =
                StrategyPresent(botAI, StrategyItem{strategy, false}) ? back : missing;
            if (!into.empty())
                into += ", ";
            into += strategy;
        }

        if (!missing.empty())
            LOG_ERROR("module.overseer",
                      "overseer: '{}' did not get {} back after its errand to '{}' ended "
                      "- it is not the character it was before the trip, and nothing else "
                      "will notice", name, missing, aim);
        if (!back.empty())
            LOG_INFO("module.overseer",
                     "overseer: '{}' has {} back after its errand to '{}' - restored "
                     "exactly as they were before it started travelling",
                     name, back, aim);
    }

    // END EVERY FOCUS WHOSE ERRAND IS GONE. The same shape, and for the same
    // reason, as TravelAimBook::PruneVanished which it runs beside: an errand can
    // end without this loop seeing it end - the bridge owns the column too, and
    // a `return` cannot be made to run an epilogue - so the only complete answer
    // is to compare what is remembered against the rows that actually came back.
    //
    // ONE POLL OF LAG ON THE PATHS THAT DO RELEASE INSIDE THE LOOP, and it is
    // the same trade the escort sweep above already makes for the same reason.
    // A character that arrives and is released this poll keeps its focus until
    // the next one, which is five seconds while anyone is escorted and fifteen
    // otherwise. Five seconds of not gathering is not a cost; a release path
    // somebody forgets to hand the focus back on is.
    //
    // AND THAT LAG CANNOT RACE THE ONE THING IT COULD HAVE RACED. The drive that
    // would otherwise pick a just-released character up is DriveQuests, and
    // TravelAimBook::Release stamps a hand-back grace of
    // TRAVEL_HANDBACK_SECONDS (45) that stands it down for exactly that window.
    // The longest this sweep can lag is one TRAVEL_POLL_MS (15), so the
    // strategies are always back on the engine before anything else is allowed
    // to steer the character that lost them.
    void SweepTravelFocus(std::set<std::string> const& stillAimed)
    {
        for (auto it = _travelFocus.begin(); it != _travelFocus.end(); )
        {
            if (stillAimed.count(it->first))
            {
                ++it;
                continue;
            }
            // Advanced BEFORE the restore, which erases the entry this iterator
            // is on. Erasing one element of a std::map invalidates only the
            // iterator to that element, so the advanced one stays good.
            std::string const name = it->first;
            ++it;
            RestoreTravelFocus(name);
        }
    }

    // THE END OF A TRAVEL POLL, BOTH HALVES OF IT. The errand memory and the
    // focus begin together and they end together: an aim that stopped coming
    // back from the roster has to take the errand record AND the stood-down
    // strategies with it, and a caller that does one of the two has done half
    // the job. That is the same argument TravelAimBook::Release makes about its
    // own three side effects, and the reason this is one verb: DriveTravel has
    // two ways out and both of them go through here.
    void EndTravelPoll(std::set<std::string> const& stillAimed)
    {
        _travelAims.PruneVanished(stillAimed);
        SweepTravelFocus(stillAimed);
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
        //
        //    AN ESCORTED MEMBER COUNTS AS AIMABLE BEFORE IT IS (#122). The lease
        //    on `new rpg` is granted by DriveTravel at the instant it issues the
        //    walk, so between the coordinator asking for a member and that walk
        //    being issued there is a poll in which the member holds an aim it
        //    cannot yet act on. A quest aim written over the top in that window
        //    would be a second drive steering a character a run is holding at a
        //    door.
        if (!travelTarget.empty() && (CanBeSentToNpc(botAI) || IsEscorted(name)))
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
        //    that outlives its reason is its own bug. The clock itself belongs
        //    to the errand column and is kept with it - every way an errand can
        //    end stamps it, and there is now exactly one place each of those
        //    lives. See TravelAimBook.
        return _travelAims.WithinHandbackGrace(name);
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

    // The release path, the prune and the errand memory all live on
    // TravelAimBook now - `_travelAims.Release()`, `_travelAims.PruneVanished()`
    // and `_travelAims.StateFor()`. They moved together because they always
    // WERE together: releasing an errand is a column write, an erase of that
    // errand's memory and a stamp of the hand-back clock, and a caller that does
    // one of the three has done none of the job. The dungeon run coordinator
    // used to do exactly that in eight places; see that class for what it cost.

    void DriveTravel()
    {
        // THE SAME READ THE QUEST DRIVE'S ARBITRATION USES (infra#2846), so the
        // two can never be looking at different answers to "who is on an
        // errand". The WHERE clause that used to live here lives in the loader:
        // an empty target never reaches this loop, exactly as before.
        std::map<std::string, std::string> const aims = _travelAims.Load();
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
            EndTravelPoll(std::set<std::string>());
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

            TravelAimBook::TravelState& state = _travelAims.StateFor(name);
            if (state.target != target || state.learn != wantSkill)
            {
                state.target = target;
                state.learn = wantSkill;
                state.entry = 0;
                state.pinned = false;
                state.arrived = false;
                // A new errand is a new flight budget too, and a new leg. The
                // old errand's flights are spent, its leg is over the moment
                // its aim stops being the aim, and its refusal to fly was
                // about a route nobody is taking any more (#68).
                state.flights = 0;
                state.flightSince = 0;
                state.flightSaid = false;
                state.stuckSaid = false;
                state.groundSaid = false;
                // A new errand is a new ratchet: nothing measured yet, and the
                // clock starts now rather than carrying the last errand's over.
                state.progress.best = 0.f;
                state.progress.since = std::time(nullptr);
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
            //
            // UNLESS A RUN IS ESCORTING IT (#122). An escorted member is one the
            // dungeon run coordinator has asked to be somewhere it cannot get to
            // by following - the last yards onto a portal, or the staging point a
            // stalled follower never closed on. It does not carry `new rpg` yet
            // and that is the point: it is granted below, at the instant the walk
            // is issued, and taken back when the escort ends. See the escort
            // section above for why those two things cannot be separated.
            bool const escorted = IsEscorted(name);
            if (!CanBeSentToNpc(botAI) && !escorted)
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

            // THE ESCORT'S FOCUS, TAKEN BEFORE ANYTHING BELOW CAN RETURN (#164).
            // This is the last point at which the answer is unconditionally
            // "this character is travelling": a character in the air, one held
            // on a cliff top by GroundedStep, one already standing at its
            // escort point and one whose walk simply is not re-issued this poll
            // are all still travelling, and every one of them reaches a
            // `continue` further down. It is also the point at which the LEADER
            // is covered on exactly the same terms as an escorted follower,
            // which is what the death in ESCORT_DIVERT_STRATEGIES' comment made
            // non-negotiable: he was on a staging aim, and a rule that only
            // covered followers would have left him on the cliff with `grind`.
            //
            // ASSERTED EVERY POLL, not taken once. See the escort focus section
            // for why: the grants that killed him arrived mid-walk, seconds
            // before it, from something that does not know a run exists.
            AssertTravelFocus(name, botAI, target);

            // A CHARACTER IN THE AIR IS NOT DECIDED ABOUT AT ALL (#68). It
            // cannot be walked anywhere - Player::ActivateTaxiPathTo has
            // already taken its movement, and NewRpgTravelFlightAction returns
            // without acting for exactly this reason (NewRpgAction.cpp:639) -
            // so every branch below is either meaningless or harmful here.
            // Deliberately asked of the WORLD rather than of this module's own
            // memory of the leg, so a flight upstream started on its own is
            // left alone too: the walk this drive would otherwise re-issue
            // over it is the bug, whoever put the character on the taxi.
            //
            // THE ERRAND'S BACKSTOP CLOCK IS HELD WHILE THIS IS TRUE. Distance
            // to the errand is not a reading about progress during a flight -
            // half a taxi route routinely goes the wrong way round a mountain
            // - and a leg that ends with the character much nearer would
            // otherwise be released as unreachable somewhere over the middle
            // of it. Same reasoning, and the same carry-the-clock shape, as
            // the quest drive's hand-back over a travel errand.
            if (bot->IsInFlight())
            {
                state.progress.since = std::time(nullptr);
                if (state.flightSince)
                    state.flightSince = std::time(nullptr);
                continue;
            }

            // UPSTREAM'S STUCK TELEPORT IS KEPT OUT OF REACH (#138). Every walk
            // this drive issues runs through NewRpgBaseAction::MoveFarTo
            // (NewRpgBaseAction.cpp:40), and MoveFarTo has a recovery of its
            // own: after five re-entries with no five-yard improvement and
            // `stuckTime` (90 s, NewRpgBaseAction.h:76) on the clock it stops
            // walking and does `bot->TeleportTo(dest)`
            // (NewRpgBaseAction.cpp:97-113). That is the "flying and falling
            // to move, like an exploit" the operator has been describing: a
            // character that cannot find a path is dropped onto its
            // destination from wherever it stood, and the destination this
            // module hands a catching-up follower is wherever the leader was
            // standing - which, on a cliff or a mountain, is a fall.
            //
            // THE FOLLOW STRATEGY'S OWN TELEPORT IS ALREADY GONE, and that
            // is worth writing down so nobody goes looking for it: Follow()'s
            // teleport-to-master sits inside a block comment at the pin
            // (MovementActions.cpp:1110-1161), and FollowAction::isUseful
            // stands the action down outright while the master is in flight
            // (FollowActions.cpp:267-268). MoveFarTo's is the one a roster
            // character on an errand can still reach, and it is reached only
            // through `new rpg`, which is to say only by the leader and by an
            // escorted follower - exactly the two this drive is walking.
            //
            // THE LEVER IS THE CLOCK. `stuckTs` and `stuckAttempts` are plain
            // public fields of the public `rpgInfo` struct (NewRpgInfo.h:20,
            // :81-82), reset by upstream itself whenever the walk gets nearer
            // (NewRpgBaseAction.cpp:91-95). Stamping them here on every poll
            // is the same reset a five-yard improvement would have earned,
            // and the poll is TRAVEL_POLL_MS (15 s) apart - DUNGEON_RUN_POLL_MS
            // (5 s) while anyone is escorted - against a 90 s fuse, so the fuse
            // can never burn down between two polls. A walk that genuinely
            // cannot land is still bounded: the twenty-minute backstop below
            // releases the errand as unreachable, on the ground, which is what
            // "unreachable" should mean. Said once per errand when it was
            // actually about to fire, because that is the moment worth having
            // in the log: five attempts with no progress is a path problem
            // somebody may want to look at, and the teleport used to hide it.
            //
            // Held BEFORE the stand-still branches below so an escorted member
            // holding at its point - which MoveFarTo is not being asked to
            // move, and so never counts against - is covered on the same terms
            // as one still walking.
            if (botAI->rpgInfo.stuckAttempts >= 5 && !state.stuckSaid)
            {
                state.stuckSaid = true;
                LOG_WARN("module.overseer",
                         "overseer: '{}' has made no progress toward '{}' in {} tries and "
                         "upstream would teleport it there after {} seconds - held on the "
                         "ground instead, the errand's own {}-minute backstop decides",
                         name, target, botAI->rpgInfo.stuckAttempts,
                         UPSTREAM_MOVE_FAR_STUCK_SECONDS,
                         static_cast<uint32>(TRAVEL_BACKSTOP_SECONDS / 60));
            }
            botAI->rpgInfo.stuckTs = getMSTime();   // Timer.h:103
            botAI->rpgInfo.stuckAttempts = 0;

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
                _travelAims.Release(name);
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
                    _travelAims.Release(name);
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
                else if (escorted)
                {
                    // AN ESCORT ENDS WHEN THE RUN SAYS SO, NOT WHEN THE WALK
                    // FINISHES, and this branch is the difference between a
                    // party assembling at a door and a party dissolving at one.
                    //
                    // Releasing here would be the ordinary thing and it would be
                    // wrong twice over. Patch 0012 has an arrived place-aim call
                    // ChangeToIdle, and NewRpgStatusUpdateAction turns RPG_IDLE
                    // into a randomly chosen status on the next tick - so the
                    // first follower to reach the doorstep would pick GO_GRIND
                    // and walk away while the coordinator was still waiting for
                    // the fifth. Four members oscillating in and out of the
                    // doorstep radius is a crossing that can never be ready.
                    //
                    // Falling through instead reaches the re-issue guard below,
                    // which renews the WANDER_NPC lease every poll, so an
                    // escorted member STANDS where it was sent until the
                    // coordinator stops wanting it there and the sweep hands the
                    // wheel back to `follow`. Exactly the reasoning the doorway
                    // branch above already carries, for the same reason.
                    if (!state.arrived)
                    {
                        state.arrived = true;
                        LOG_INFO("module.overseer",
                                 "overseer: '{}' has reached its escort point '{}' and holds "
                                 "there - the {} releases it, not the arrival",
                                 name, target,
                                 IsCatchingUp(name) ? "follow drive" : "run");
                    }
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
                    // Opportunistic, and deliberately BEFORE the errand is
                    // cleared rather than after: `entry` and `bot` are both
                    // already in hand here, and this is a no-op for anything
                    // that is not a flight master. See DiscoverFlightPointOnArrival.
                    DiscoverFlightPointOnArrival(name, bot, entry);

                    LOG_INFO("module.overseer",
                             "overseer: '{}' reached '{}' (creature {}) - errand done, "
                             "releasing", name, target, entry);
                    _travelAims.Release(name);
                    continue;
                }
            }

            // THE FLIGHT LEG, WHICH IS A WALK IN THE WRONG DIRECTION ON PURPOSE
            // (#68). Between issuing a flight and boarding it, the character
            // is walking to a flight master, which is usually AWAY from the
            // errand. The errand's own backstop reads distance-to-target and
            // would call that a stall and release the aim, so the leg gets its
            // own clock and holds the errand's while it runs.
            //
            // ONLY THIS MODULE'S OWN LEGS. `flightSince` is set nowhere but
            // ConsiderFlight, so a RPG_TRAVEL_FLIGHT this module did not issue
            // - upstream rolling one out of its eight idle behaviours - is not
            // held open here and is simply re-aimed below like any other
            // status that is not RPG_WANDER_NPC.
            if (state.flightSince)
            {
                if (botAI->rpgInfo.GetStatus() == RPG_TRAVEL_FLIGHT)  // NewRpgInfo.h:99
                {
                    state.progress.since = std::time(nullptr);
                    if (std::time(nullptr) - state.flightSince <=
                        TRAVEL_FLIGHT_BACKSTOP_SECONDS)
                        continue;

                    // THE LEG'S BACKSTOP, and the reason it has to exist here.
                    // Upstream leaves RPG_TRAVEL_FLIGHT only when a flight
                    // ENDS (NewRpgAction.cpp:295), so a character that cannot
                    // reach the flight master never leaves it, and the errand
                    // this drive is holding open would be held open forever
                    // by a leg that is going nowhere.
                    LOG_WARN("module.overseer",
                             "overseer: '{}' set off for a flight master on its way to "
                             "'{}' and has not left the ground in {} minutes - giving up "
                             "on the flight and walking",
                             name, target,
                             static_cast<uint32>(TRAVEL_FLIGHT_BACKSTOP_SECONDS / 60));
                    botAI->rpgInfo.ChangeToIdle();  // NewRpgInfo.h:110
                    state.flightSince = 0;
                    // Deliberately NOT `continue`: falling through re-issues
                    // the walk below on this same poll, so the errand loses
                    // nothing but the time the leg cost it.
                }
                else
                {
                    // THE LEG IS OVER. Either the flight landed - upstream
                    // returns the bot to RPG_IDLE on arrival - or the
                    // activation was refused at the flight master and it did
                    // the same. Both end the same way from here: the walk
                    // below picks the errand back up from wherever the
                    // character now stands, and `flights` is already spent so
                    // the next poll does not simply fly again.
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is off its flight leg and carries on to '{}' "
                             "on foot from {} yards",
                             name, target, static_cast<uint32>(distance));
                    state.flightSince = 0;
                    // The errand's clock has been held for the whole leg and
                    // the character has usually moved a long way; start it
                    // again from here rather than from a mark measured before
                    // the flight, which the ratchet below would otherwise
                    // never beat if the landing left it further out.
                    state.progress.best = 0.f;
                    state.progress.since = std::time(nullptr);
                }
            }

            // PREFER FLYING TO WALKING, WHERE FLYING IS SHORTER (#68). Asked
            // BEFORE the ratchet reads, because a flight that is issued this
            // poll makes this poll's distance reading meaningless - the
            // character is about to walk deliberately away from the errand -
            // and asked before the re-issue guard below, because a flight
            // REPLACES the walk rather than competing with it.
            if (ConsiderFlight(name, bot, botAI, pos, distance, state))
                continue;

            // PROGRESS RESTARTS THE BACKSTOP'S CLOCK (#63). This is the site the
            // lesson was measured on and the rule is now written down once, in
            // OverseerDecisions::Ratchet, which carries that measurement and the
            // whole argument with it. What is left here is the reading -
            // distance to the thing this character was sent to - and, below,
            // what this drive does about a stall, which is the only part that
            // was ever this site's own.
            OverseerDecisions::RatchetVerdict const progress = OverseerDecisions::Ratchet(
                state.progress, distance, std::time(nullptr), TRAVEL_RATCHET);

            // BACKSTOP. An errand that can never land must not pin a character
            // to it forever - that is a character standing still, which is the
            // state this whole epic exists to stop mistaking for a working one.
            // Read with the progress check above: a stall means the character
            // has not got nearer for TRAVEL_BACKSTOP_SECONDS, not that the
            // errand has simply been outstanding that long.
            if (progress.stalled)
            {
                LOG_INFO("module.overseer",
                         // "releasing the errand as unreachable" is kept on ONE
                         // source line on purpose: infra's guard test greps this
                         // function for the phrase to prove every release path
                         // says why it fired, and a phrase split across two
                         // string literals is invisible to it.
                         "overseer: '{}' was sent to '{}' and has not got any nearer than "
                         "{} yards for {} minutes - releasing the errand as unreachable",
                         name, target, static_cast<uint32>(state.progress.best),
                         static_cast<uint32>(TRAVEL_BACKSTOP_SECONDS / 60));
                _travelAims.Release(name);
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
            //   4. A DIFFERENT PLACE (#122). `npcEntry` is ZERO for every
            //      `at:<map>:<x>,<y>,<z>` aim - a place has no creature standing
            //      on it, which is exactly what patch 0012 exists to allow - so
            //      comparing entries alone makes every place look like every
            //      other place, and a re-aim from the staging point to the door
            //      would be skipped as "already walking there" until upstream's
            //      five-minute lease happened to lapse. The whole point of a
            //      crossing is moving the party from one place to a second one
            //      twenty yards on, so the position has to be part of the
            //      comparison. Compared against the errand's own resolved
            //      position rather than the character's, because "am I already
            //      walking to this" is a question about the aim and not about
            //      progress toward it.
            // THE POINT THE MOVER IS ACTUALLY HANDED (#138). `pos` is where
            // the errand wants this character and stays that throughout -
            // arrival, the ratchet and the backstop above all measure against
            // it. `aimAt` is where the character may be SENT this poll without
            // walking off something, which is the same point whenever the
            // navmesh has a route and a short checked step when it does not.
            // See the GroundedStep section for the whole argument.
            //
            // ONLY FOR A PLACE. `entry` is non-zero for a creature aim, and a
            // creature stands on ground by construction: a spawn point is
            // somewhere the world put something. Every death this answers was
            // at an `at:` or a `trigger:`.
            WorldPosition aimAt = pos;
            if (!entry && !GroundedStep(bot, pos, aimAt))
            {
                if (!state.groundSaid)
                {
                    state.groundSaid = true;
                    LOG_WARN("module.overseer",
                             "overseer: '{}' is sent to '{}', {} yards off, and there is no "
                             "direction out of where it stands that does not step off "
                             "something - holding it there rather than walking it off a "
                             "cliff. The errand's own {}-minute backstop still bounds this",
                             name, target, static_cast<uint32>(distance),
                             static_cast<uint32>(TRAVEL_BACKSTOP_SECONDS / 60));
                }
                continue;
            }

            // `!state.reissue` FIRST, because the staging watchdog's second
            // rung is precisely the case this guard is wrong about (#164): a
            // character whose `rpgInfo` still names a destination it has stopped
            // walking to reads here as already walking to it, and the guard
            // then refuses to hand it the walk again forever. The flag is spent
            // below, at the instant the walk is issued.
            if (!state.reissue && botAI->rpgInfo.GetStatus() == RPG_WANDER_NPC)  // NewRpgInfo.h:99
            {
                if (NewRpgInfo::WanderNpc const* wander =
                        std::get_if<NewRpgInfo::WanderNpc>(&botAI->rpgInfo.data))
                {
                    // GetPositionX/Y  Position.h:279,280 - spelled out rather
                    // than asked of GetExactDist2d, because WorldPosition
                    // (TravelMgr.h:82) declares its own overloads of that name
                    // and a hidden base overload is a compile error found at
                    // the wrong end of a six-minute build.
                    //
                    // COMPARED AGAINST THE STEP, NOT THE DESTINATION (#138).
                    // The question this guard asks is "am I already walking to
                    // the thing I am about to be given", and the thing about to
                    // be given is `aimAt`. While a stepped walk is running that
                    // point moves as the character does, so the walk is
                    // re-issued each poll on purpose - which is what stepping
                    // IS. MoveFarTo will not clobber the spline underneath it
                    // either: it lets a committed move with more than ten yards
                    // left run to its end before it acts on a new destination
                    // (NewRpgBaseAction.cpp:57-83).
                    float const ddx = wander->pos.GetPositionX() - aimAt.GetPositionX();
                    float const ddy = wander->pos.GetPositionY() - aimAt.GetPositionY();
                    bool const samePlace = entry || (ddx * ddx + ddy * ddy) <= 1.0f;
                    if (wander->npcEntry == entry && samePlace)
                        continue;
                }
            }

            // THE ESCORT'S GRANT, AND IT IS HERE FOR A REASON THAT WILL NOT
            // SURVIVE BEING MOVED (#122). A freshly granted `new rpg` starts at
            // RPG_IDLE, and NewRpgStatusUpdateAction turns RPG_IDLE into a
            // randomly chosen status on its very next tick - two of the eight
            // (GO_GRIND, GO_CAMP) pick a position out of the world and walk to
            // it. Granting the strategy anywhere earlier than the statement
            // before the aim is written leaves a window in which a follower
            // wanders off instead of walking to the door. Granting it here
            // leaves no such window: the status is never IDLE for a tick.
            if (escorted && !CanBeSentToNpc(botAI))
            {
                botAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);
                if (!CanBeSentToNpc(botAI))
                {
                    // Read back rather than assumed, and loudly: an escort that
                    // silently failed to take is a follower standing at a door
                    // it will never walk to, which is the state the whole run
                    // exists to stop mistaking for a working one.
                    LOG_ERROR("module.overseer",
                              "overseer: '{}' is being escorted to '{}' but `new rpg` would "
                              "not go on - nothing will walk it there",
                              name, target);
                    continue;
                }
                NoteEscortGranted(name);
                LOG_INFO("module.overseer",
                         "overseer: '{}' takes the wheel from `follow` for its escort to "
                         "'{}' - handed back when the run stops wanting it there",
                         name, target);
            }

            // patches/mod-playerbots/0005-wander-npc-can-be-aimed.patch adds this
            // overload. The no-argument ChangeToWanderNpc() upstream ships takes
            // no target, which is the entire reason this feature did not exist.
            botAI->rpgInfo.ChangeToWanderNpc(entry, aimAt);
            // Spent. One poll's exception, not a mode - see TravelState.
            state.reissue = false;

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
                // A CATCH-UP RE-AIM IS NOT NEWS (#138). The walk was announced
                // once by DriveCatchUp when it started; every re-aim after
                // that is the same decision with the leader fifty yards
                // further on, and at a leader's running pace that is one line
                // per follower per party poll for the whole of a long walk.
                // DEBUG keeps the trace for anyone reading at that level and
                // keeps the INFO log to decisions.
                if (IsCatchingUp(name))
                    LOG_DEBUG("module.overseer",
                              "overseer: '{}' re-aimed at '{}' for its catch-up walk, "
                              "{:.0f} yards off",
                              name, target,
                              bot->GetDistance2d(pos.GetPositionX(), pos.GetPositionY()));
                else
                    LOG_INFO("module.overseer",
                             "overseer: '{}' sent to '{}' - creature {} at {:.0f} yards",
                             name, target, entry,
                             bot->GetDistance2d(pos.GetPositionX(), pos.GetPositionY()));
            }
        }

        EndTravelPoll(stillAimed);
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
        std::map<std::string, std::string> const travelAims = _travelAims.Load();

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
    // THREE FACTS SHAPE THIS DRIVE, all learned by hand before or after it
    // existed.
    //
    // ONE: `dc on` IS REFUSED OUTSIDE AN INSTANCE (DcOnAction::Execute,
    // DungeonClearChatActions.cpp:247-251, "Not in a dungeon."). So this cannot
    // be done once at the start of a run; it has to happen after the character
    // is actually inside, which is what IsDungeon() gates.
    //
    // TWO: IT DOES NOT SURVIVE A RESTART. What `dc on` actually sets is the
    // run's `enabled` flag (DungeonClearChatActions.cpp:281), which lives on
    // the leader's value context in memory and dies with the worldserver. So
    // every bounce silently disarms the run and nothing re-applies it. That is
    // why this is a DRIVE and not a one-shot: it keeps asking, and its memory
    // of having asked (below) is in-process, so a fresh process asks again.
    //
    // THREE, AND THIS IS #140: THE STRATEGY IS NOT THE RUN. For its first
    // release this drive read `HasStrategy("dungeon clear")` as "already
    // armed" and skipped. But the dungeon module's own gate installs that
    // strategy on EVERY bot the moment it stands on a dungeon map, before
    // anyone has asked for anything (DcStrategyGate::Reconcile,
    // DcStrategyGate.cpp:60-112, invariant stated at DcStrategyGate.h:20-21;
    // driven from login, map change and a throttled sweep, DcStrategyGate.h:24-37).
    // So the gate said "armed" on a fresh process and again after every
    // bounce, `dc on` was never issued, and five characters stood at the
    // Deadmines entrance for six hours at full health with the strategy
    // installed and the run's `enabled` flag off. What the triggers actually
    // read is that flag (IsEnabled, DungeonClearTriggers.cpp:82-87), and the
    // strategy is just the ladder they hang on. The strategy check is gone;
    // nothing in this module reads the strategy as a proxy for anything.
    //
    // ONCE PER (RUN, CHARACTER, PROCESS, STAY INSIDE), NOT ONCE PER POLL.
    // An accepted `dc on` re-issued while the run is enabled resets the
    // run's transient state - approach FSM, cleared anchors, long path
    // (DungeonClearChatActions.cpp:281-322) - so issuing it every poll would
    // restart the run from the door eight times a minute. The record below
    // is what makes this once: keyed on the run id the row was opened under,
    // cleared when the character is seen OFF the dungeon map, because leaving
    // the map is where the module itself strips the strategy and disables the
    // run (TeardownOnStrip, DcStrategyGate.cpp:39-45 and :92-93), so a
    // character that walks back in after a wipe needs a fresh `dc on` even
    // though the run row is the same. A refusal is retried, slowly - see
    // DUNGEON_DC_ON_RETRY_SECONDS.
    //
    // WHY DoSpecificAction AND NOT HandleCommand. Both end in the same place,
    // DcOnAction::Execute, but only one of them says what happened.
    // HandleCommand is void (PlayerbotAI.h:395): it queues the text
    // (PlayerbotAI.cpp:1107), HandleCommands drains the queue later
    // (PlayerbotAI.cpp:553-589) through a trigger the strategy has to be
    // holding, and the action's result is thrown away at :580-585. So the
    // return value the issue asks this module to log does not exist on that
    // path. DoSpecificAction (PlayerbotAI.cpp:1773-1823) runs the action
    // synchronously by name and returns its bool - and it is how the dungeon
    // module's own `.dc on` command and addon button dispatch (RunDcCommand,
    // DungeonClearCommand.cpp:53-66, through DungeonClearDispatch.h:41:
    // `botAI->DoSpecificAction(action, Event("dc", param, issuer), true)`),
    // so it is the module's own front door, not a side entrance.
    // `silent` only suppresses the emote and the generic "failed" whisper
    // (:1806-1812); DcRefuse's own reason is written inside Execute
    // regardless.
    //
    // WHY NOT ChangeStrategy DIRECTLY. `dc on` does more than add two
    // strategies: it sets `enabled`, registers the tank with the status
    // publisher, seeds the pull setting and camp through the module's own
    // ApplyPullSetting (DungeonClearChatActions.cpp:281-296). Adding the
    // strategies by hand - which the gate already does anyway - arms nothing.
    //
    // WHAT A REFUSAL LOOKS LIKE. DcRefuse (DungeonClearChatActions.cpp:50-58)
    // whispers the bot's MASTER, and the master of the tank is the tank, so
    // that whisper goes nowhere - but the same function logs the reason at
    // WARN whenever the master is a bot (:55-57: "DC command refused for X:
    // why"), which covers the selfbot leader. This module logs that the call
    // returned false, once per refusal streak, and points there for the why.
    //
    // NOT DISARMED ON THE WAY OUT, deliberately. Leaving an instance is not the
    // same as ending a run - a wipe puts the party outside at a graveyard with
    // the run still meaningfully in progress - and the module's own `dc` verbs
    // own that lifecycle. This drive only ever turns the brain ON.
    // Would the dungeon module accept a `dc` command from this character?
    //
    // Two tests, and both are needed. The first is the dungeon module's own:
    // IsAuthorized (DungeonClearChatActions.cpp:60-72) rejects an owner that
    // has a PlayerbotAI unless it is a selfbot, so the issuer must be a
    // person with no AI (IsRealPlayer, PlayerbotAI.cpp:4389-4394) or a
    // selfbot (IsSelfBot, PlayerbotAI.cpp:4397-4402; both are free functions
    // declared at PlayerbotAI.h:79-80). Client attachment ALONE is not
    // enough, even though it is the discriminator this module trusts
    // everywhere else since #131: a follower playing through a real client
    // has its master pointed at the leader by KeepRosterFollowing, fails
    // IsSelfBot, and would be refused. The second test is this module's:
    // ClientAttached, so a character whose client has dropped is never the
    // one whose name a command goes out under - #131 has it out of the world
    // within a poll anyway. Group membership, IsAuthorized's third
    // condition, is the caller's to check.
    static bool MayIssueDcCommands(Player* member)
    {
        return ClientAttached(member) && (IsRealPlayer(member) || IsSelfBot(member));
    }

    // A groupmate the dungeon module will accept a command FROM.
    //
    // THE LEADER FIRST (#135). KeepRosterFollowing keeps the leader its own
    // master, which is what makes it a selfbot, precisely so that the tank
    // is the one who issues: the dungeon module was written around a run
    // being commanded by the character at its head. Any other qualifying
    // member is the fallback - a person at a keyboard in the party, or a
    // follower that is momentarily still a selfbot because it logged in
    // after the last follow poll - so the brain is not left off for a poll
    // when the leader is mid-relog.
    //
    // Returns null rather than falling back to the bot itself, because the bot
    // itself is precisely the issuer that is always rejected, and retrying with
    // it is how this went unnoticed for so long.
    static Player* AuthorizedDcIssuer(Player* bot)
    {
        if (!bot)
            return nullptr;
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (leader && MayIssueDcCommands(leader))
            return leader;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != leader && MayIssueDcCommands(member))
                return member;
        }
        return nullptr;
    }

    // WHAT THIS PROCESS HAS SAID TO WHOM, per roster character (#140). This is
    // the memory that turns a drive that re-issued every poll into one that
    // issues once: the run id the last `dc on` was for, whether DcOnAction
    // accepted it, when, and whether a refusal has already been said. Read by
    // the run coordinator's CLEARING verdict as well, which is why it is not a
    // local. World-thread-only, unguarded, lost on restart - the same
    // discipline as DungeonRunCoordinatorState, and lost on restart is the
    // point: a fresh process must issue again, because the flag it set died
    // with the old one.
    struct DcOnRecord
    {
        uint32 runId{0};
        bool accepted{false};
        time_t issuedAt{0};
        bool loggedRefused{false};
    };
    std::map<std::string, DcOnRecord> _dcOnIssued;

    // Has this process had `dc on` ACCEPTED for this character, for this run?
    // Accepted means DcOnAction::Execute returned true - and for a character
    // that is not the run's leader tank that is trivially true
    // (DungeonClearChatActions.cpp:239-246), so "accepted for everyone inside"
    // says the command took on whichever of them the module elected leader,
    // and nothing more. See the CLEARING state for what more is required.
    bool DcOnAccepted(std::string const& name, uint32 runId) const
    {
        auto const it = _dcOnIssued.find(name);
        return it != _dcOnIssued.end() && it->second.runId == runId && it->second.accepted;
    }

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

            // GEOGRAPHY, NOT THE RUN. THIS DRIVE CANNOT WAIT FOR ITS OWN
            // PRECONDITION.
            //
            // This read `InDungeonRun(bot)` for one release, and that was a
            // deadlock. InDungeonRun requires an open run row; this drive is
            // the only thing that opens one. So no run existed, the drive
            // skipped, the run was never created, and it skipped again forever.
            // Measured live: four characters standing inside an instance and
            // zero rows in overseer_dungeon_run, indefinitely.
            //
            // It is the same failure class the run record was introduced to end
            // - a guard whose condition can never become true - reintroduced
            // while refactoring the guards that came before it. The lesson is
            // narrow and worth stating: a component that CREATES a state may
            // never gate itself on that state existing.
            //
            // So this one drive keeps asking the map. Being on an instance map
            // is what makes a run possible, and this is the drive whose job is
            // to turn possible into real. Every OTHER drive still asks
            // InDungeonRun, because for them the run genuinely is the
            // precondition.
            Map* map = bot->GetMap();
            if (!map || !map->IsDungeon())
            {
                // OFF THE MAP FORGETS THE ISSUE. This is where the dungeon
                // module itself strips the strategy and disables the run
                // (DcStrategyGate.cpp:39-45), so whatever this process said
                // to this character no longer holds, and the next time it is
                // seen inside it is asked again - same run row or not.
                _dcOnIssued.erase(name);
                continue;
            }

            // A corpse cannot start a dungeon run, and the dead strategy set
            // does not carry the dungeon contexts anyway. BELOW the map check
            // on purpose (#140): a character that died, released to a
            // graveyard outside and is walking back in as a ghost must have
            // its record forgotten on the way out, whether or not it was
            // alive when this drive looked - the module disabled the run at
            // the map change either way.
            if (!bot->IsAlive())
                continue;

            // THE HEARTBEAT IS TOUCHED FOR EVERY CHARACTER SEEN INSIDE,
            // BEFORE ANY OTHER DECISION, AND THAT ORDER IS THE WHOLE POINT.
            //
            // This call used to sit BELOW the already-armed check, and that was
            // a bug the world found in fourteen minutes. Arming happens at most
            // once per character per run - the check right below exists to make
            // sure of it - so a heartbeat written only on the arming path is
            // written once and then never again. It went cold 120 seconds
            // later, the run closed as abandoned with three characters standing
            // inside it, and every drive resumed steering people in a dungeon:
            //
            //     id 1  map 36  ended  "heartbeat cold - nobody from the
            //                           roster seen on the map"
            //     Bork map 36  Grug map 36  Ugga map 36
            //
            // The lesson is about what the signal MEANS. `last_progress_at`
            // answers "when was somebody last seen in here", so it belongs to
            // being seen, not to being armed. Anything conditional on work
            // still being needed is the wrong place for a liveness signal,
            // because liveness outlives the work.
            //
            // Opening also stays here rather than below: by the time anything
            // is steering, the thing that owns the steering already exists.
            uint32 const runId = OpenOrTouchRun(name, bot->GetMapId());
            if (!runId)
            {
                // The row was closed between the touch and the read-back. Not
                // a state that lasts: the next pass touches it open again.
                LOG_WARN("module.overseer",
                         "overseer: '{}' is inside map {} but no active run row answered "
                         "right after it was touched - trying again next pass",
                         name, static_cast<uint32>(bot->GetMapId()));
                continue;
            }

            // WHO ISSUES THE COMMAND DECIDES WHETHER IT IS OBEYED, and for a
            // year of runs the answer has been "nobody may".
            //
            // mod-dungeon-clear refuses dc commands from a true bot
            // (DungeonClearChatActions.cpp:60, IsAuthorized): if the ISSUER has
            // a PlayerbotAI and is not a selfbot, it returns false before
            // looking at anything else. This module issued `dc on` with the
            // receiving bot as its own issuer, so every single one was rejected
            // and the module sat idle inside every run. Its own log said so the
            // whole time:
            //
            //     DC command refused for Grog: Not authorized to enable dungeon clear
            //
            // Nothing upstream of that noticed, because the command surface
            // reports `delivered` - which means the bot RECEIVED it - and a
            // strategy probe afterwards shows "dungeon clear" present, which
            // comes from the contexts the module registers at STARTUP and not
            // from the command taking. Two separate checks both looked like
            // confirmation and neither was.
            //
            // The rule accepts an issuer who is a selfbot AND a member of the
            // target's group. The roster is one permanent party and its
            // leader is kept a selfbot by KeepRosterFollowing (#135), so the
            // tank is normally the issuer; see AuthorizedDcIssuer for the
            // fallback and for why "has a client" is not by itself enough.
            Player* issuer = AuthorizedDcIssuer(bot);
            if (!issuer)
            {
                // SAID, NOT SKIPPED. A run whose brain could not be switched on
                // is the single most consequential thing this module can fail
                // at, and it has failed at it silently for its whole life.
                LOG_WARN("module.overseer",
                         "overseer: '{}' is inside map {} but no groupmate may issue a "
                         "dungeon-clear command - the module refuses a true bot as issuer "
                         "and no client-attached member of the party is a selfbot or a "
                         "person, so the dungeon brain stays OFF",
                         name, static_cast<uint32>(bot->GetMapId()));
                continue;
            }

            // ALREADY ISSUED FOR THIS RUN, BY THIS PROCESS, AND ACCEPTED: done
            // for as long as this character stays inside. This is the line
            // that makes the arming once rather than every poll, and it reads
            // this module's own memory of what it did - not the strategy list,
            // which the dungeon module fills in on its own (see THREE above).
            // It must stay below the heartbeat, for the reason above.
            DcOnRecord& record = _dcOnIssued[name];
            time_t const now = std::time(nullptr);
            if (record.runId == runId)
            {
                if (record.accepted)
                    continue;
                // Refused last time. Retried, but not every poll: a refusal
                // that heals (a dead member revived) is caught within a
                // minute, and one that does not is one WARN below and then
                // silence rather than a line every eight seconds.
                if (now - record.issuedAt < DUNGEON_DC_ON_RETRY_SECONDS)
                    continue;
            }

            bool const wasRefused = record.runId == runId && !record.accepted;
            // Event(source, param, owner)  Event.h:21-24; the owner is what
            // IsAuthorized reads (DungeonClearChatActions.cpp:62).
            bool const accepted =
                botAI->DoSpecificAction("dc on", Event("dc", "", issuer), true);
            record.runId = runId;
            record.accepted = accepted;
            record.issuedAt = now;

            if (accepted)
            {
                // SAID ONCE PER ISSUE, and an issue happens once per stay. This
                // is the line the issue asks for by name. "Accepted" is all it
                // may claim: DcOnAction returned true, which for the elected
                // leader tank means `enabled` is now set and for anyone else
                // means nothing was refused. Whether the run then MOVES is the
                // coordinator's verdict, not this one's.
                LOG_INFO("module.overseer",
                         "overseer: '{}' is inside map {} (run {}) - issuing 'dc on' as "
                         "'{}': accepted{}",
                         name, static_cast<uint32>(bot->GetMapId()), runId,
                         issuer->GetName(),
                         wasRefused ? " (after an earlier refusal)" : "");
                record.loggedRefused = false;
                continue;
            }

            // REFUSED, SAID ONCE PER STREAK. The reason is not available to
            // this module - DcRefuse writes it to the dungeon module's own log
            // (DungeonClearChatActions.cpp:55-57, "DC command refused for X:
            // why") and whispers it to a master that, for the tank, is the
            // tank - so this says that it was refused, by whom, and where to
            // look. A false here can also mean the action is not registered
            // at all (DoSpecificAction returns false through every engine
            // answering UNKNOWN, PlayerbotAI.cpp:1816-1822), which is the
            // "module not loaded" case and reads the same from here.
            if (!record.loggedRefused)
            {
                record.loggedRefused = true;
                LOG_WARN("module.overseer",
                         "overseer: '{}' is inside map {} (run {}) - issuing 'dc on' as "
                         "'{}': REFUSED (DcOnAction returned false). The reason is in "
                         "the dungeon module's own log under 'DC command refused for "
                         "{}'; retrying every {}s and staying quiet until it takes",
                         name, static_cast<uint32>(bot->GetMapId()), runId,
                         issuer->GetName(), name,
                         static_cast<uint32>(DUNGEON_DC_ON_RETRY_SECONDS));
            }
        } while (result->NextRow());
    }

    void DriveBelowTerrainRecovery()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT name FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        do
        {
            std::string const name = result->Fetch()[0].Get<std::string>();
            Player* bot = ObjectAccessor::FindPlayerByName(name);
            if (!bot)
                continue;
            // A taxi, flying mount, boat, vehicle or swimmer is deliberately
            // away from a land navmesh. Elevated geometry above one of those
            // states is not proof that it fell through the world.
            if (!OverseerDecisions::TerrainRecoveryMayInspect(
                    bot->IsAlive(), bot->IsBeingTeleported(), bot->IsInFlight(),
                    bot->IsFlying(), bot->IsInWater(), bot->GetTransport() != nullptr,
                    bot->GetVehicle() != nullptr))
                continue;

            float surface = 0.f;
            bool const surfaceValid = SurfaceAbove(bot, surface);
            // Most characters see their own footing again and stop here. Ask
            // the pure rule first with the conservative no-navmesh answer so
            // four Detour probes are paid only where the vertical gap itself
            // could possibly require recovery.
            if (!OverseerDecisions::BelowTerrainNeedsRecovery(
                    bot->GetPositionZ(), surface, surfaceValid, false,
                    TERRAIN_RECOVERY_GAP_YARDS))
                continue;
            bool const hasLocalNavmesh = HasLocalNavmesh(bot);
            if (!OverseerDecisions::BelowTerrainNeedsRecovery(
                    bot->GetPositionZ(), surface, surfaceValid, hasLocalNavmesh,
                    TERRAIN_RECOVERY_GAP_YARDS))
                continue;

            // The leader's bind point keeps a grouped family together. This is
            // the same destination DriveStuckRevival uses for a repeated death
            // trap, but this character is alive and must not be resurrected.
            Player* home = bot;
            if (Group* group = bot->GetGroup())
                if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
                    home = leader;

            uint16 const fromMap = static_cast<uint16>(bot->GetMapId());
            float const fromX = bot->GetPositionX();
            float const fromY = bot->GetPositionY();
            float const fromZ = bot->GetPositionZ();
            std::string travelTarget;
            std::string job;
            uint32 questAim = 0;
            uint16 aimedMap = 0;
            float aimedX = 0.f, aimedY = 0.f, aimedZ = 0.f;
            {
                std::lock_guard<std::mutex> guard(g_aimMutex);
                auto const it = g_aimSnapshot.find(LowerName(name));
                if (it != g_aimSnapshot.end())
                {
                    travelTarget = it->second.travelTarget;
                    job = it->second.job;
                    questAim = it->second.questAim;
                    aimedMap = it->second.mapId;
                    aimedX = it->second.x;
                    aimedY = it->second.y;
                    aimedZ = it->second.z;
                }
            }
            bot->TeleportTo(home->m_homebindMapId, home->m_homebindX,
                            home->m_homebindY, home->m_homebindZ, 0.f);
            LOG_WARN("module.overseer",
                     "overseer: '{}' recovered at map {} position ({:.1f}, {:.1f}, {:.1f}), "
                     "surface z {:.1f}, no local navmesh; aim job='{}' quest={} travel='{}' "
                     "last aimed position map {} ({:.1f}, {:.1f}, {:.1f}); sent to '{}'s "
                     "bind point; it was not resurrected",
                     name, static_cast<uint32>(fromMap), fromX, fromY, fromZ,
                     surface, job, questAim, travelTarget, static_cast<uint32>(aimedMap),
                     aimedX, aimedY, aimedZ, home->GetName());
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
            {
                // AN OFFER NEVER OUTLIVES THE DEATH THAT CAUSED IT.
                //
                // Nothing upstream clears a resurrect offer when the character
                // comes back by some OTHER route. The core clears the request
                // data in exactly two places - an explicit decline
                // (MiscHandler.cpp:681) and the NEXT death (setDeathState,
                // Player.cpp:1099) - and the accept path does not clear it at
                // all (ResurectUsingRequestData, Player.cpp:13117-13143, and
                // its delayed half at Player.cpp:1668-1686). Worse, the
                // response handler refuses to run at all once the character is
                // alive:
                //
                //     if (GetPlayer()->IsAlive() || ...)
                //         return;                    // MiscHandler.cpp:676
                //
                // That test sits ABOVE the decline branch, so from the moment
                // the character is alive even a DECLINE is unreachable and the
                // offer is stuck until it next dies. Upstream's accept action
                // bails on the same condition (AcceptResurrectAction.cpp:13)
                // and is wired only under the dead strategy
                // (DeadStrategy.cpp:22-23), which leaves the AI tree the
                // instant the character is alive. So at exactly the moment an
                // offer goes stale, everything that could answer it stops being
                // able to.
                //
                // Clearing it here is cheap - whatever set it has already taken
                // what it needed - and it restores the property the branch
                // below relies on: that a pending offer means a REAL offer,
                // still worth standing down for.
                //
                // DELIBERATELY NOT CALLED "ORPHANED" IN THE LOG. A pending
                // offer on a living character is the ordinary tail of an
                // ACCEPTED resurrect just as often as it is a stranded one,
                // because the accept path leaves it set either way, and this
                // drive cannot tell the two apart from state alone. It does not
                // need to; the action is the same.
                if (bot->isResurrectRequested())
                {
                    bot->clearResurrectRequestData();
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is alive with a resurrect offer still recorded "
                             "against it - cleared, because upstream clears one only on a "
                             "decline or the next death, and an offer that outlives its death "
                             "is one that nothing can ever answer",
                             name);
                }

                _healerExpected.erase(name);
                ReleaseRevivalHold(botAI, name);
                continue;
            }

            // AN OFFER ON THE TABLE IS ANSWERED BEFORE ANYTHING STEPS OVER IT.
            //
            // Every recovery below moves the character away from its corpse,
            // and none of them asked whether a healer had already put an offer
            // up. Watched live in Duskwood (#177): a character died to a fall,
            // came back through upstream's own corpse path 54 seconds later,
            // and the healer's offer - cast while it was dead, and entirely
            // legitimate when it was cast - was still drawn across the middle
            // of that character's picture afterwards, with nothing left on
            // either side of the connection that could answer it.
            //
            // Answering is strictly better than recovering around it. It puts
            // the character back where the PARTY is standing rather than at a
            // graveyard, which is the same argument the healer-first stand-down
            // above already makes, and it is the only outcome that CONSUMES the
            // offer rather than stranding it.
            //
            // ANSWERED ON THE FIRST POLL THAT SEES IT, with no grace. The
            // graces elsewhere in this drive exist to let a better recovery
            // arrive; here the better recovery has already arrived and is
            // waiting to be taken. Every poll it is not taken is another poll
            // it sits on a picture somebody is watching.
            if (bot->isResurrectRequested() && AnswerResurrectOffer(bot, botAI, name))
                continue;

            // THE HEALER GETS THERE FIRST.
            //
            // Every recovery below - the release, the graveyard teleport, the
            // forced resurrect - moves the character away from its corpse.
            // In the open world that costs nothing, because nothing else was
            // ever going to revive it (see STUCK_REVIVAL_DEAD_SECONDS). Inside
            // an instance with a run open it costs the run: the dungeon
            // module's post-combat rez is already walking a healer to this
            // corpse, and a corpse that is teleported out from under it is a
            // tank standing in a field outside while the party waits inside.
            // Measured, once, on the first tank-driven run - see
            // STUCK_REVIVAL_HEALER_SECONDS.
            //
            // So while all three hold - the character is dead ON a dungeon
            // map (a ghost that has released out of the instance is on the
            // outdoor map, and the dungeon module no longer counts it), a
            // run is open for that map (InDungeonRun; without a run the
            // dungeon module is not armed and nobody is coming), and a living
            // resurrector shares its map - this drive stands down for
            // STUCK_REVIVAL_HEALER_SECONDS from the death. It does not stand
            // down forever: the dungeon module gives up after its own timeout
            // and this drive is the only recovery a named-account roster has
            // (see the IsRandomBot note above), so when the window passes, or
            // the last resurrector dies, the recovery below runs unchanged.
            //
            // Said once per death, in each direction. _healerExpected
            // remembers which deaths were announced and is cleared the moment
            // the character is alive again, so a second death is announced
            // again and the first is not repeated every poll.
            auto const healerExpected = [&](int64 deadSeconds) -> bool
            {
                bool const inRun = InDungeonRun(bot);
                Player* rezzer = inRun ? LivingResurrectorFor(bot) : nullptr;
                if (rezzer && deadSeconds < STUCK_REVIVAL_HEALER_SECONDS)
                {
                    if (_healerExpected.insert(name).second)
                        LOG_INFO("module.overseer",
                                 "overseer: '{}' is dead on map {} with '{}' alive in the "
                                 "party and a dungeon run open - the dungeon module's "
                                 "post-combat rez is expected to walk the healer to the "
                                 "corpse, so this drive stands down for up to {}s before "
                                 "recovering it itself",
                                 name, static_cast<uint32>(bot->GetMapId()),
                                 rezzer->GetName(), STUCK_REVIVAL_HEALER_SECONDS);
                    return true;
                }

                if (_healerExpected.erase(name))
                    LOG_WARN("module.overseer",
                             "overseer: '{}' is still dead after {}s and {} - the healer "
                             "is no longer waited for; recovering as in the open world",
                             name, deadSeconds,
                             rezzer ? "the dungeon module's rez budget has passed"
                             : inRun ? "no living resurrector is left on its map"
                                     : "no dungeon run is open for its map any more");
                return false;
            };

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
            float corpseZ = 0.f;

            if (corpse)
            {
                ghostTime = corpse->GetGhostTime();
                corpseMap = corpse->GetMapId();
                corpseX = corpse->GetPositionX();
                corpseY = corpse->GetPositionY();
                corpseZ = corpse->GetPositionZ();
            }
            else if (QueryResult row = CharacterDatabase.Query(
                         "SELECT time, mapId, posX, posY, posZ FROM corpse WHERE guid = {}",
                         bot->GetGUID().GetCounter()))
            {
                Field* f = row->Fetch();
                ghostTime = int64(f[0].Get<uint32>());
                corpseMap = f[1].Get<uint16>();
                corpseX = f[2].Get<float>();
                corpseY = f[3].Get<float>();
                corpseZ = f[4].Get<float>();
            }

            // NOBODY IS THERE TO CLICK RELEASE.
            //
            // No ghost time and no corpse row used to mean "nothing to recover
            // to", and for a true bot it does. For a character with a CLIENT
            // attached it means the opposite: it is dead, sitting on the
            // "You have died. Release to the nearest graveyard?" prompt, and
            // waiting for a button press from a person who is not there.
            //
            // A corpse only exists AFTER the release. So the very state that
            // needs rescuing is the one that produces none of the evidence the
            // rest of this drive reads, and it was skipped on every pass -
            // forever, silently. Measured on the dev world 2026-08-30: one
            // character lay dead inside an instance for EIGHT MINUTES with this
            // drive logging nothing, and once five clients were running rather
            // than one, two of the five were sitting on that prompt at the same
            // moment.
            //
            // The button is not special. Its opcode handler does exactly two
            // things (MiscHandler.cpp:59-100, CMSG_REPOP_REQUEST):
            //
            //     GetPlayer()->BuildPlayerRepop();
            //     GetPlayer()->RepopAtGraveyard();
            //
            // Both are public (Player.h:2074-2075, public from Player.h:1091),
            // so the server can do for itself what it was waiting to be told.
            //
            // TIMED FROM OUR OWN FIRST SIGHTING, because there is no ghost
            // clock yet - that is the whole point. The map is keyed by name and
            // cleared the moment the character is alive again, so a character
            // that dies twice is timed from each death rather than the first.
            //
            // THE GRACE IS DELIBERATE AND NOT ZERO. A healer in the party may
            // resurrect a corpse, which is better than a graveyard run because
            // it keeps the body where the fight was. Releasing instantly would
            // take that option away from every death. So this waits the same
            // STUCK_REVIVAL_DEAD_SECONDS the ghost path waits, and only then
            // concludes that nobody is coming.
            if (!ghostTime)
            {
                bool const released = bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
                if (released)
                {
                    _awaitingRelease.erase(name);
                    continue;
                }

                int64 const now = time(nullptr);
                auto const seen = _awaitingRelease.find(name);
                if (seen == _awaitingRelease.end())
                {
                    _awaitingRelease[name] = now;
                    continue;
                }
                if (now - seen->second < STUCK_REVIVAL_DEAD_SECONDS)
                    continue;
                if (healerExpected(now - seen->second))
                    continue;

                LOG_WARN("module.overseer",
                         "overseer: '{}' has been dead {}s on the release prompt with nobody "
                         "at the keyboard to answer it - releasing on its behalf, because a "
                         "corpse only exists after the release and every other recovery path "
                         "here waits for one", name, now - seen->second);
                bot->BuildPlayerRepop();
                bot->RepopAtGraveyard();
                _awaitingRelease.erase(name);
                continue;
            }

            // Alive again, or released and now a ghost: either way this is no
            // longer a character waiting on a prompt.
            _awaitingRelease.erase(name);

            int64 const deadFor = time(nullptr) - ghostTime;
            if (deadFor < STUCK_REVIVAL_DEAD_SECONDS)
                continue;
            if (healerExpected(deadFor))
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
                _lastRevival.erase(name);
                HoldAfterRevival(bot, botAI, name, home->m_homebindMapId,
                                 home->m_homebindX, home->m_homebindY);
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
                _lastRevival.erase(name);
                HoldAfterRevival(bot, botAI, name, bot->m_homebindMapId,
                                 bot->m_homebindX, bot->m_homebindY);
                continue;
            }

            // NEAREST IS A GEOMETRY ANSWER; THE CHARACTER ASKED A SAFETY
            // QUESTION. See GRAVEYARD_THREAT_RADIUS for the measurement and
            // the two tests. When the nearest graveyard passes, everything
            // below is exactly what it was before.
            std::string refused;
            GraveyardStruct const* safe = SafeGraveyardNear(bot, grave, corpseMap,
                                                            corpseX, corpseY, corpseZ, refused);
            if (!safe)
            {
                bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX,
                                bot->m_homebindY, bot->m_homebindZ, 0.f);
                bot->ResurrectPlayer(0.5f);
                bot->SpawnCorpseBones();

                LOG_WARN("module.overseer",
                         "overseer: '{}' (level {}) has been dead for {}s and no graveyard "
                         "for its corpse is safe to stand at [{}] - sent to its own bind "
                         "point instead of the nearest graveyard",
                         name, static_cast<uint32>(bot->GetLevel()), deadFor, refused);
                _lastRevival.erase(name);
                HoldAfterRevival(bot, botAI, name, bot->m_homebindMapId,
                                 bot->m_homebindX, bot->m_homebindY);
                continue;
            }

            // THE SAME GRAVEYARD TWICE IN FIVE MINUTES IS A VERDICT, NOT A
            // COINCIDENCE. Whatever the scoring above thinks of the place it
            // picked, a character this drive already revived there and that
            // is dead again has shown it cannot live there. Home,
            // unconditionally - see GRAVEYARD_REPEAT_SECONDS.
            int64 const now = time(nullptr);
            auto const last = _lastRevival.find(name);
            if (last != _lastRevival.end() && last->second.first == safe->ID &&
                now - last->second.second < GRAVEYARD_REPEAT_SECONDS)
            {
                bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX,
                                bot->m_homebindY, bot->m_homebindZ, 0.f);
                bot->ResurrectPlayer(0.5f);
                bot->SpawnCorpseBones();

                LOG_WARN("module.overseer",
                         "overseer: '{}' has been dead for {}s and this drive already revived "
                         "it at '{}' {}s ago - twice at one graveyard inside {}s means it "
                         "cannot live there, so it was sent to its own bind point instead",
                         name, deadFor, safe->name, now - last->second.second,
                         GRAVEYARD_REPEAT_SECONDS);
                _lastRevival.erase(last);
                HoldAfterRevival(bot, botAI, name, bot->m_homebindMapId,
                                 bot->m_homebindX, bot->m_homebindY);
                continue;
            }

            // Mirrors exactly what SpiritHealerAction::Execute already does
            // on a successful revive (ResurrectPlayer + SpawnCorpseBones) -
            // this is not a new resurrection mechanism, it is the same one,
            // reached without depending on a path that never runs for this
            // roster at all.
            bot->TeleportTo(safe->Map, safe->x, safe->y, safe->z, 0.f);
            bot->ResurrectPlayer(0.5f);
            bot->SpawnCorpseBones();

            if (safe == grave)
                LOG_WARN("module.overseer",
                         "overseer: '{}' has been dead for {}s - mod-playerbots' own "
                         "auto-relocate-after-repeated-deaths (RandomTeleport, "
                         "RandomPlayerbotMgr.cpp) is gated behind IsRandomBot, which is false "
                         "for this roster's named accounts, so it would never reach a graveyard "
                         "on its own; resurrected at the nearest graveyard instead",
                         name, deadFor);
            else
                LOG_WARN("module.overseer",
                         "overseer: '{}' (level {}) has been dead for {}s - the nearest "
                         "graveyard was refused [{}], so it was resurrected at '{}' instead",
                         name, static_cast<uint32>(bot->GetLevel()), deadFor, refused,
                         safe->name);
            _lastRevival[name] = std::make_pair(safe->ID, now);
            HoldAfterRevival(bot, botAI, name, safe->Map, safe->x, safe->y);
        } while (result->NextRow());
    }

    // THE GRACE AFTER A REVIVAL - see REVIVAL_HOLD_SECONDS. `+stay` is
    // upstream's own hold: it is a sibling of `follow` in the movement
    // strategy context (StrategyContext.h:210-218), so adding it removes
    // `follow` (Engine.cpp:350-363) and StayAction stops the character where
    // it stands (StayActions.cpp:31-37). `new rpg` is not a sibling and its
    // status trigger outranks `stay`, so the leader's is taken off for the
    // hold as well. What this module would otherwise do to a held character
    // (re-grant `follow` or `new rpg`, send it to catch up) asks
    // HeldAfterRevival first.
    //
    // Decided from spawn data at the DESTINATION rather than the live grid
    // around the character, because a teleport is not complete when
    // TeleportTo returns: the relocation lands on the teleport ack, which
    // for a bot is upstream's HandleTeleportAck on its next AI tick. So at
    // this point the character is still standing where it died.
    void HoldAfterRevival(Player* bot, PlayerbotAI* botAI, std::string const& name,
                          uint32 mapId, float x, float y)
    {
        NearbyThreat const threat = HostileSpawnsNear(bot, mapId, x, y, REVIVAL_AGGRO_RADIUS, 0);
        if (threat.count)
        {
            _revivalHoldUntil.erase(name);
            LOG_INFO("module.overseer",
                     "overseer: '{}' revives with {} hostile spawn(s) within {} yards, up to "
                     "'{}' (level {}) - not held there; it is left free to move away",
                     name, threat.count, uint32(REVIVAL_AGGRO_RADIUS), threat.name, threat.level);
            return;
        }

        bool const led = botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("+stay", BOT_STATE_NON_COMBAT);
        if (led)
            botAI->ChangeStrategy("-new rpg", BOT_STATE_NON_COMBAT);
        _revivalHoldUntil[name] = std::make_pair(time(nullptr) + REVIVAL_HOLD_SECONDS, led);
        LOG_INFO("module.overseer",
                 "overseer: '{}' is held where it revives for {}s (`+stay`{}) before "
                 "anything moves it again - nothing hostile spawns within {} yards",
                 name, REVIVAL_HOLD_SECONDS, led ? ", `-new rpg`" : "",
                 uint32(REVIVAL_AGGRO_RADIUS));
    }

    // The hold's other end: `stay` comes off, and the mover it displaced
    // comes back - `follow` for a character with a master, `new rpg` for the
    // one that led. Called from the alive branch of DriveStuckRevival, so a
    // hold ends at most one poll late.
    void ReleaseRevivalHold(PlayerbotAI* botAI, std::string const& name)
    {
        auto const hold = _revivalHoldUntil.find(name);
        if (hold == _revivalHoldUntil.end() || time(nullptr) < hold->second.first)
            return;
        bool const led = hold->second.second;
        _revivalHoldUntil.erase(hold);

        botAI->ChangeStrategy("-stay", BOT_STATE_NON_COMBAT);
        if (led)
            botAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);
        else if (botAI->GetMaster())
            botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
        LOG_INFO("module.overseer",
                 "overseer: '{}' is released from its post-revival hold ({} restored)",
                 name, led ? "`new rpg`" : botAI->GetMaster() ? "`follow`" : "nothing to restore");
    }

    // Answer a resurrect offer this character is sitting on. True when the
    // offer was taken and the character is on its way back, false when it was
    // declined and the recovery below should carry on as if there had never
    // been one.
    //
    // ACCEPTED WHEN IT IS SAFE, DECLINED WITH A STATED REASON WHEN IT IS NOT.
    // Answering Accept blindly is its own bug: the accept teleports the
    // character to where the caster stood and revives it there at the health
    // the spell specified (Player.cpp:13119-13133), so accepting into a fight
    // puts a partial-health character straight back into the fight that killed
    // it, and accepting an offer whose caster has since died puts it exactly
    // where that caster went down.
    //
    // WHY ACCEPTING IS THE OUTCOME THAT ALSO CLEARS THE PICTURE. The prompt is
    // a client-side widget and the operator's client is stock, so nothing here
    // can reach in and close it; there is no "cancel resurrect" opcode in this
    // client's protocol to send either. What the client DOES watch is the
    // character's ghost state. Accepting runs ResurrectPlayer, which removes
    // SPELL_AURA_GHOST (spell 8326, Player.cpp:4561); that clears
    // PLAYER_FLAGS_GHOST, and the client hides its resurrect prompts on the
    // ghost-to-alive transition. Clearing the request data on its own sends the
    // client nothing at all. So a decline settles the SERVER's state and leaves
    // the recovery below to produce that same transition a moment later, while
    // an accept produces it immediately - which is the whole reason this
    // prefers to accept.
    //
    // THE REQUEST DATA IS NOT CLEARED ON THE ACCEPT PATH, ON PURPOSE. The
    // resurrect is usually DELAYED: TeleportTo leaves the character
    // mid-teleport, ResurectUsingRequestData schedules DELAYED_RESURRECT_PLAYER
    // and returns (Player.cpp:13122-13126), and the half that actually restores
    // health and mana runs later, reading m_resurrectHealth and m_resurrectMana
    // (Player.cpp:1668-1686). Clearing here would zero those fields before that
    // code reads them and the character would revive at zero health. The alive
    // branch above clears it one poll later instead, once the fields have been
    // consumed and the character is safely back. Both DECLINE paths below clear
    // it immediately, which is safe for exactly the opposite reason: nothing is
    // going to be resurrected off those fields at all.
    bool AnswerResurrectOffer(Player* bot, PlayerbotAI* botAI, std::string const& name)
    {
        // MID-TELEPORT IS MID-ANSWER, AND ANSWERING TWICE IS WORSE THAN
        // ANSWERING LATE. An accept teleports first and finishes on the ack
        // (Player.cpp:13122-13126), and because nothing on that path clears
        // the request data, a character still in flight one poll later still
        // reads as having an offer pending. Calling the accept again would
        // restart the teleport it is already serving, which on a client that
        // is slow to ack could hold it in flight indefinitely. True, not
        // false: the offer HAS been answered, and this poll has nothing left
        // to do for it.
        if (bot->IsBeingTeleported())
            return true;

        Player* offerer = ResurrectOfferedBy(bot);

        if (!offerer)
        {
            bot->clearResurrectRequestData();
            LOG_INFO("module.overseer",
                     "overseer: '{}' holds a resurrect offer with nobody alive in its party "
                     "behind it - declined, because taking it would put the character down "
                     "where the caster went down; the recovery below carries on instead",
                     name);
            return false;
        }

        if (bot->IsInCombat() || offerer->IsInCombat())
        {
            bot->clearResurrectRequestData();
            LOG_INFO("module.overseer",
                     "overseer: '{}' is offered a resurrect by '{}' while {} in combat - "
                     "declined, because it would arrive at partial health inside the fight "
                     "that killed it; the caster is free to offer again once the fight ends",
                     name, offerer->GetName(),
                     bot->IsInCombat() ? "it is" : "the caster is");
            return false;
        }

        // The caster's CURRENT position, not the offer's stored one, which is
        // private (Player.h:2910-2912). In the ordinary case they are the same
        // spot, the spell having landed a moment ago, and where the caster is
        // NOW is the better question anyway: that is where the party is.
        uint32 const mapId = offerer->GetMapId();
        float const x = offerer->GetPositionX();
        float const y = offerer->GetPositionY();
        std::string const offererName = offerer->GetName();

        bot->ResurectUsingRequestData();

        // This drive's own per-death bookkeeping ends with the death it belongs
        // to. The paths that normally erase these are the ones this accept just
        // skipped, and a stale entry would time the NEXT death from this one's
        // first sighting.
        _awaitingRelease.erase(name);
        _healerExpected.erase(name);

        LOG_INFO("module.overseer",
                 "overseer: '{}' takes the resurrect '{}' offered it - answered here because "
                 "a character with nobody at its keyboard never answers one itself, and an "
                 "offer left standing outlives the death that caused it",
                 name, offererName);

        HoldAfterRevival(bot, botAI, name, mapId, x, y);
        return true;
    }

    bool HeldAfterRevival(std::string const& name) const
    {
        auto const hold = _revivalHoldUntil.find(name);
        return hold != _revivalHoldUntil.end() && time(nullptr) < hold->second.first;
    }

    // ------------------------------------------------------ dungeon run: gather --
    //
    // mod-overseer#88 ("the party owns the run"): six independent model
    // reviews of this codebase, given the same brief with no sight of each
    // other's answers, converged unprompted on the same state machine -
    //
    //     IDLE -> GATHERING -> BARRIER -> ENTER -> STAGED_INSIDE -> CLEARING
    //          -> LOOT -> EXIT -> IDLE
    //
    // - and the same diagnosis of why one is needed: "drives are concurrent
    // controllers with no arbitration - like five drivers stepping on the same
    // pedals" (qwen3-coder, quoted in the epic). The measured failure this
    // closes: "Bork and Grog reached the tank's corpse first, pulled a Defias
    // Miner and an Evoker with no healer in range, and both died. Ugga was
    // still walking." Nobody owned the decision to wait.
    //
    // WHAT IS BUILT: RESET, GATHERING, BARRIER, ENTER, STAGED_INSIDE, CLEARING
    // and EXIT. LOOT and WIPE_RECOVER are not, and LOOT's absence is a
    // measurement rather than a deferral - see the end-of-run comment in
    // DriveDungeonRun for why "the dungeon is cleared" is not a fact available
    // to this module for map 36 today, and why a signal that reads as success
    // before anything has happened is worse than no signal.
    //
    // AND IT IS A LOOP NOW (#144), which is one new state and one new edge:
    //
    //     IDLE -> RESET -> GATHERING -> ... -> CLEARING -> EXIT -> RESET -> ...
    //                                                           \-> IDLE
    //
    // The measured fact that made the loop necessary is not about looping at
    // all. On 2026-09-02 the family cleared Deadmines, was sent back in with
    // `job = 'dungeon'`, and every part of this machine worked: staged, crossed
    // together, `dc on` issued and verified automatically for the first time
    // ("CLEARING verified - the tank has moved 31y"). Seconds later the dungeon
    // brain said `advance: no next boss (all dead/skipped) -> stalling`. The
    // `instance` row for map 36 held completedEncounters = 127 - all seven bits
    // - and the leader still had a `character_instance` row binding him to it.
    // He had walked into a mine where everything was already dead.
    //
    // So RESET is not a step BETWEEN runs, it is the first step OF one. The
    // state a rerun inherits is exactly the state the first run of the day
    // inherits, and a coordinator that only reset on the way round the loop
    // would still walk the very first run into an empty dungeon.
    //
    // WHAT BOUNDS THE LOOP, AND WHAT DOES NOT. Two numbers on the leader's
    // roster row - `dungeon_runs_wanted` (30 by default, the operator's own
    // figure) and `dungeon_runs_done` - plus a stop after
    // DUNGEON_CAMPAIGN_CONSECUTIVE_FAILURES resets that would not take. What
    // does NOT bound it is any notion of the run's GOAL being met: a run's real
    // goal is #143 and is not built, gear as a goal is #145, "until item X
    // drops" is #150. #144's own wording for the stand-in is used instead - the
    // run ended and the party is outside - and the PR that built this says so
    // rather than implying a completion test exists.
    //
    // IT STILL NEVER WRITES `dc on`. DriveDungeonClear (above) already owns
    // arming the dungeon brain, and it is the only thing that may: `dc on` is
    // REFUSED outside an instance (it reports `delivered` with a null result,
    // so the command surface cannot tell you it failed) and refused again
    // unless the issuer is a selfbot in the target's group. That drive gates on
    // IsDungeon(), issues once per (run, character, process, stay inside) and
    // remembers what DcOnAction returned (#140), and issues again after a
    // restart because that memory and the flag it set are both in-process.
    // Reproducing any of that here would be a second thing arming the same
    // characters, which is the exact shape this epic exists to remove.
    // STAGED_INSIDE therefore HANDS OVER rather than arming: it establishes
    // the precondition that drive has been waiting for - every roster member
    // on the instance map, together - and holds. CLEARING then reads the
    // drive's record and the leader's movement, not the strategy list.
    //
    // WHY THIS OWNS ONLY THE LEADER'S MOVEMENT, AND NOT A NEW WALK MECHANISM.
    // `new rpg` - the strategy that lets a character be aimed at all, via
    // NewRpgWanderNpcAction - is deliberately carried by the LEADER alone
    // (see CanBeSentToNpc and the 937-yard scatter it fixed). A follower
    // travels by FOLLOWING, not by being aimed. So GATHERING reuses the exact
    // same mechanism DriveTravel already owns - `overseer_roster.travel_npc`,
    // resolved by ResolveTravelTarget and walked by DriveTravel/rpgInfo - by
    // writing the leader an `at:<map>:<x>,<y>,<z>` aim at a staging point.
    // That is deliberately NOT a `trigger:` aim: `trigger:` makes DriveTravel
    // itself step the character through the areatrigger on arrival
    // (StepThroughAreaTrigger) via CMSG_AREATRIGGER, which IS entry - the one
    // thing this slice must not do. `at:` only ever walks; nothing here can
    // cross a map boundary.
    //
    // A second, independent walk mechanism (MoveTo/MotionMaster called
    // directly from this function) was considered and rejected: it would be a
    // second thing writing motion for the leader alongside DriveTravel, which
    // is exactly the "concurrent controllers with no arbitration" shape the
    // epic is about. Reusing DriveTravel means there is still only one thing
    // that ever moves a character - the coordinator only ever writes the
    // COLUMN DriveTravel already reads, the same way DriveQuests hands the
    // wheel to DriveTravel via TravelHoldsTheWheel instead of moving anyone
    // itself.
    //
    // WHY THE STAGING POINT IS A STANDOFF FROM THE PORTAL TRIGGER, NOT THE
    // TRIGGER ITSELF. TRAVEL_ARRIVED_POSITION_YARDS above already explains why
    // an `at:` aim on the trigger's own coordinates is dangerous even without
    // a `trigger:` step: a character walking to open ground stops improving
    // once it is "close enough" and may keep drifting the last few yards on
    // its own pathing noise, and the Deadmines portal (areatrigger 78, radius
    // 7) is small enough that noise alone could carry someone through it while
    // this coordinator still believes it is holding a barrier. Standing off by
    // DUNGEON_STAGING_STANDOFF_YARDS keeps the whole staging circle
    // (DUNGEON_BARRIER_RADIUS_YARDS, 10y) outside the trigger's own radius.
    //
    // WHICH DIRECTION IT STANDS OFF IN IS NOT A FREE CHOICE (#121). A doorway
    // has one walkable approach and three walls, so a standoff picked by
    // guesswork is three times more likely to be inside rock than on the
    // corridor. ResolveDungeonStagingPoint takes the bearing from the world
    // instead - see its comment - and resolves the ground height where the
    // point lands rather than carrying the doorway's own down a cliff.
    //
    // WHAT TRIGGERS A RUN. `job = 'dungeon'` has existed on `overseer_roster`
    // since 2026_08_26_01_overseer_roster_job.sql and been read by the job
    // gate in DriveQuests since it landed - but "nothing yet exists to
    // positively drive farm/dungeon/grind/etc" (that gate's own comment). This
    // is the first thing that actually acts on `job = 'dungeon'` rather than
    // merely standing another drive down for it. Keyed off the LEADER's job
    // column, because the leader is the only one that can be aimed at all and
    // the bridge (production/scripts/wow-overseer/jobs.py) sets the whole
    // roster's job together.
    //
    // WHY ENTER WALKS EVERY MEMBER AND THEN KNOCKS FOR EVERY MEMBER (#122).
    // Everything above is about the coordinator only ever writing the COLUMN
    // DriveTravel reads, and ENTER keeps exactly that - it just writes it for
    // the whole party instead of for the leader alone. This used to aim the
    // leader and say that "a follower reaches the door by following". Measured
    // twice on the dev world, it does not: the leader crossed onto map 36 under
    // his own power while the other four stayed outside, strung out over
    // several hundred yards behind him, and he then fought the first pull alone
    // and died.
    //
    // The reason is structural rather than incidental. `follow` is the whole of
    // a follower's repertoire, because upstream adds `new rpg` in one place
    // (AiFactory.cpp:615) behind two gates a roster follower can never pass -
    // `IsRandomBot`, and "ungrouped or leading". And `follow` cannot cross a
    // doorway: FollowAction only acts while the master is on the same map, and
    // what actually crosses is a packet sent by a character standing inside the
    // trigger's own radius. So the last twenty yards are done by an ESCORT: the
    // coordinator leases `new rpg` to each member for the length of the
    // crossing and aims it at the door with the same ordinary `at:` aim the
    // leader gets. See the escort section beside CanBeSentToNpc for why the
    // lease is granted at the instant the walk is issued and taken back by a
    // sweep rather than by any one code path.
    //
    // So the knock is done here, once per member, and the knock is NOT
    // movement: StepThroughAreaTrigger sends the packet a game client would
    // have sent to the handler that would have received it, and that handler
    // re-checks IsInAreaTriggerRadius (MiscHandler.cpp:716) before it teleports
    // anybody. A member that has not walked to the door cannot be carried
    // through it by this, which is what keeps "one thing moves a character"
    // true: nothing here moves anyone, it only asks the server whether someone
    // has already arrived.
    //
    // AND THE LEADER GOES LAST, WHICH IS THE WHOLE POINT OF THE STATE. The
    // measured failure this epic opens with is the tank entering alone. The
    // leader is the anchor every follower is standing next to, so a leader that
    // crosses first takes the anchor away from whoever is still outside and
    // strands them at a door they cannot open. Crossing him last means either
    // the party goes through together or nobody's anchor moves.
    //
    // ONE COORDINATOR, NOT ONE ROW PER MAP. `overseer_dungeon_run` (opened by
    // DriveDungeonClear) is keyed by map because several characters share an
    // instance once they are inside it. Before entry there is exactly one
    // party and exactly one run it could be gathering for, so the state below
    // is a single in-process struct - unguarded, world-thread-only, following
    // the same pattern TravelAimBook's errand memory and _awaitingRelease
    // already document (see their comments) - rather than a table this slice
    // has no second reader for yet.
    struct DungeonPortal
    {
        char const* keyword;       // matched against the leader's job target (future: multiple dungeons)
        uint32 outsideMapId;       // the map the portal is approached FROM
        // THE DOORWAY, BY ID AND NOT BY COORDINATE, for the reason the
        // `trigger:` aim already gives in ResolveTravelTarget: the trigger
        // table is then the single source of both WHERE to walk and WHAT to
        // knock on, and the two cannot drift apart. ENTER reads the position
        // back out of sObjectMgr rather than carrying a second copy here.
        //
        // Both numbers read out of the pinned core's own base world DB rather
        // than remembered, and both are quoted here so a future reader can
        // check them without a running world:
        //
        //   data/sql/base/db_world/areatrigger.sql
        //     (78,0,-11208.5,1685.34,25.7612,7,0,0,0,0)
        //   data/sql/base/db_world/areatrigger_teleport.sql
        //     (78,'DeadMines Entrance',36,-16.4,-383.07,61.78,1.86)
        //
        // so trigger 78 stands on map 0 with radius 7, and going through it
        // lands on map 36. `insideMapId` is therefore not an independent fact
        // to be kept in step - it is the teleport row's target, written down.
        uint32 entryTriggerId;     // walked to, and knocked on, to get IN
        uint32 insideMapId;        // the map that trigger lands on
        // AND THE WAY BACK OUT, WHICH IS A DOOR AND NOT A BIND POINT. Read from
        // the same two tables and quoted for the same reason:
        //
        //   data/sql/base/db_world/areatrigger.sql
        //     (119,36,-14.3628,-393.38,64.5605,6,0,0,0,0)
        //   data/sql/base/db_world/areatrigger_teleport.sql
        //     (119,'DeadMines Instance Start',0,-11208.3,1672.52,24.66,4.55217)
        //
        // so trigger 119 stands inside on map 36 with radius 6, and going
        // through it lands back on map 0 a few yards from where the party went
        // in. That is the whole reason EXIT is a crossing rather than a
        // teleport home: a cross-map bind point would scatter five characters
        // across two continents and end the run somewhere nobody chose.
        uint32 exitTriggerId;      // walked to, and knocked on, to get OUT
        // THERE IS NO stageX/stageY/stageZ HERE ANY MORE, and #121 is why.
        // The staging point used to be written down as the entry trigger's own
        // coordinates stood off along +X, carrying the trigger's Z unchanged,
        // with a comment saying it was not independently confirmed to be clear
        // walkable ground. It was not: the approach to areatrigger 78 runs
        // along Y, +X goes into the wall of the Moonbrook shaft, and the
        // trigger's Z belongs to the shaft floor rather than to the clifftop
        // the offset landed on. See ResolveDungeonStagingPoint, which derives
        // it from these two triggers instead, so the table cannot drift from
        // the world the way a fourth and fifth hand-written float did.
    };

    // Standoff from the portal trigger's own coordinates, chosen so the whole
    // gather circle (DUNGEON_BARRIER_RADIUS_YARDS, 10) sits outside the
    // trigger's own radius (7 for Deadmines) with room to spare: 20 > 10 + 7.
    // That sum is the reason the standoff cannot simply BE the way-back-out
    // landing point, which for Deadmines is only 12.8 yards from the door.
    static constexpr float DUNGEON_STAGING_STANDOFF_YARDS = 20.0f;

    // HOW FAR ABOVE THE DOOR'S OWN Z THE GROUND PROBE STARTS. Map::GetHeight
    // searches DOWNWARD from the z it is given, so a probe started from the
    // terrain height would find the hillside above a tunnel rather than the
    // tunnel. Started just above the door, it finds the floor the door stands
    // on, which is the floor the party has to walk in along.
    static constexpr float DUNGEON_STAGING_Z_PROBE_LIFT_YARDS = 2.0f;

    // AND HOW FAR THE ANSWER MAY DIFFER FROM THE DOOR'S OWN Z BEFORE IT IS
    // DISBELIEVED. This is the #121 defect expressed as a test rather than as
    // prose: a staging point one short walk from a doorway is on the same
    // floor as that doorway, so a height reading tens of yards away from it is
    // either a different surface (the clifftop over the shaft, 27 yards up) or
    // one of the two sentinels GetHeight returns when it has nothing - both of
    // which are answers to refuse rather than to walk at. Doubling as the
    // invalid-height guard is deliberate: -100000 and -200000 fail this test
    // for the same reason a clifftop does, and needing no extra header to say
    // so keeps the check where the argument for it is.
    static constexpr float DUNGEON_STAGING_Z_SANITY_YARDS = 15.0f;

    // Only Deadmines for this slice. A second dungeon is a second row here,
    // not a second code path - deliberately not built until a second dungeon
    // is actually asked for (see jobs.py's IMPLEMENTED, which does not yet
    // name one either).
    static DungeonPortal const* FindDungeonPortal(std::string const& keyword)
    {
        for (DungeonPortal const& portal : DungeonPortals())
            if (keyword == portal.keyword)
                return &portal;
        return nullptr;
    }

    static std::vector<DungeonPortal> const& DungeonPortals()
    {
        static std::vector<DungeonPortal> const portals = {
            {"deadmines", 0, 78, 36, 119},
        };
        return portals;
    }

    // WHERE THE PARTY WAITS, DERIVED FROM THE DOOR INSTEAD OF WRITTEN DOWN
    // BESIDE IT (#121).
    //
    // WHAT WAS WRONG. The staging point shipped as the entry trigger's own
    // coordinates plus DUNGEON_STAGING_STANDOFF_YARDS along +X, with the
    // trigger's Z copied across unchanged. Areatrigger 78 stands at the bottom
    // of the Moonbrook shaft; the walkable approach to it runs along Y. So the
    // standoff was applied perpendicular to the only way in, into the rock, and
    // it took a floor-level Z to a place whose only surface is the clifftop
    // about 27 yards higher. A point with no navmesh under it cannot be
    // arrived at: the pathfinder returns the nearest polygon, the character
    // walks at it, never gets there, and upstream's stuck detector teleports it
    // - which is the repeating `Teleport ... as it stuck when moving far` the
    // issue was filed on, and from the outside looks like flying and falling.
    //
    // WHICH WAY IS "AWAY FROM THE DOOR", ASKED OF THE WORLD RATHER THAN GUESSED.
    // Every instance portal in this table already carries the answer, because
    // the way back out has to land a player OUTSIDE the door it came from and
    // facing away from it: `areatrigger_teleport` for the EXIT trigger is a
    // position on the outside map that the game itself picked as standable
    // ground in front of the entrance. The vector from the entry trigger to
    // that landing point is therefore the approach corridor, measured by the
    // people who built the corridor. For Deadmines it is (0.016, -0.9999) over
    // 12.82 yards - which is to say almost exactly -Y, and nothing like the +X
    // the old constant went.
    //
    // Doing it this way also means a second dungeon is still one more row in
    // DungeonPortals() and no new floats to get wrong, which was the point of
    // reading the door out of `areatrigger` rather than copying it in.
    //
    // AND THE Z IS RESOLVED WHERE THE POINT LANDS. Map::GetHeight searches down
    // from the z it is handed, so it is handed the door's own z lifted clear by
    // DUNGEON_STAGING_Z_PROBE_LIFT_YARDS - the shaft is a tunnel and a probe
    // started at terrain height would find the hill on top of it. An answer
    // further than DUNGEON_STAGING_Z_SANITY_YARDS from the door's own z is
    // refused, and so is an unloaded grid (patch 0010's lesson: not in memory
    // is not the same as not there), because the leader is usually a long way
    // off when a run is asked for. The fallback is the way-back-out landing
    // point's own z, which is real standable ground on this map by
    // construction - 24.66 against the door's 25.7612 for Deadmines.
    //
    // MEASURED AGAINST THE PINNED CORE'S OWN EXTRACTED MAP AND MMAP DATA, not
    // asserted. Reading the navmesh tile the bot pathfinder itself uses
    // (mmaps/0005328.mmtile, map 0 grid 53/28) and the terrain grid beside it:
    //
    //   point on map 0                          walkable surface under it
    //   areatrigger 78, the door                z 22.56 .. 25.23
    //   areatrigger 119's landing point         z 24.96 .. 26.56
    //   OLD staging point, +X 20y               z 52.98 and nothing lower
    //   NEW staging point, corridor 20y         z 24.96 .. 26.56
    //
    // and along the corridor axis the floor is continuous from the door out to
    // about 30 yards (z 23.9 rising to 27.1) and roughly 9 yards wide across.
    // The old point's nearest walkable surface is 27 yards above the z the aim
    // carried; the new one's is a quarter of a yard below it.
    static bool ResolveDungeonStagingPoint(DungeonPortal const& portal, Player* leader,
                                           float& outX, float& outY, float& outZ,
                                           std::string& why)
    {
        // AreaTrigger  ObjectMgr.h:425-428  map/x/y/z
        AreaTrigger const* door = sObjectMgr->GetAreaTrigger(portal.entryTriggerId);
        if (!door)
        {
            why = "the world database has no areatrigger " + std::to_string(portal.entryTriggerId);
            return false;
        }
        if (door->map != portal.outsideMapId)
        {
            why = "areatrigger " + std::to_string(portal.entryTriggerId) + " stands on map " +
                  std::to_string(door->map) + ", not the map this portal is approached from";
            return false;
        }

        // AreaTriggerTeleport  ObjectMgr.h  target_mapId/target_X/target_Y/target_Z
        AreaTriggerTeleport const* back = sObjectMgr->GetAreaTriggerTeleport(portal.exitTriggerId);
        if (!back)
        {
            why = "areatrigger " + std::to_string(portal.exitTriggerId) +
                  " has no teleport row, so there is no way back out to take a bearing from";
            return false;
        }
        if (back->target_mapId != portal.outsideMapId)
        {
            why = "the way out of areatrigger " + std::to_string(portal.exitTriggerId) +
                  " lands on map " + std::to_string(back->target_mapId) +
                  ", not outside this portal";
            return false;
        }

        float dx = back->target_X - door->x;
        float dy = back->target_Y - door->y;
        float const span = std::sqrt(dx * dx + dy * dy);
        // A landing point on top of the door names no direction at all, and a
        // normalise of it would be a divide by something near zero dressed up
        // as a bearing. Refusing is the honest answer; inventing an axis is how
        // the wall got walked into the first time.
        if (span < 1.0f)
        {
            why = "the way back out lands on the door itself, so it names no approach axis";
            return false;
        }
        dx /= span;
        dy /= span;

        outX = door->x + dx * DUNGEON_STAGING_STANDOFF_YARDS;
        outY = door->y + dy * DUNGEON_STAGING_STANDOFF_YARDS;
        outZ = back->target_Z;

        // GetMap  Object.h:631  Map* GetMap() const
        Map* map = leader ? leader->GetMap() : nullptr;
        // IsGridLoaded  Map.h:213  bool IsGridLoaded(float x, float y) const
        if (map && map->GetId() == portal.outsideMapId && map->IsGridLoaded(outX, outY))
        {
            // GetHeight  Map.h  float GetHeight(float x, float y, float z, ...) const
            float const ground =
                map->GetHeight(outX, outY, door->z + DUNGEON_STAGING_Z_PROBE_LIFT_YARDS);
            if (std::fabs(ground - door->z) <= DUNGEON_STAGING_Z_SANITY_YARDS)
                outZ = ground;
            else
                LOG_WARN("module.overseer",
                         "overseer: the map answers z={:.2f} at the '{}' staging point "
                         "({:.2f}, {:.2f}) while areatrigger {} stands at z={:.2f} - that is "
                         "another surface or no surface, so the way-back-out's own z={:.2f} "
                         "is used instead",
                         ground, portal.keyword, outX, outY, portal.entryTriggerId,
                         door->z, back->target_Z);
        }
        return true;
    }

    // WHICH RUN IS THIS, ASKED FROM THE INSIDE. A worldserver restart wipes the
    // coordinator's in-process state (see DungeonRunCoordinatorState) while the
    // party is left standing in an instance and the run row is left open in the
    // database, so "the leader is inside a dungeon I have a portal row for" is
    // how a run in progress is recognised again after a bounce. Without this the
    // coordinator would return to IDLE, see InDungeonRun, decide the run belongs
    // to somebody else, and never watch the arming or walk anyone out - which is
    // exactly the "notices a restart" obligation the epic asks for, unfulfilled.
    static DungeonPortal const* FindDungeonPortalByInsideMap(uint32 mapId)
    {
        for (DungeonPortal const& portal : DungeonPortals())
            if (mapId == portal.insideMapId)
                return &portal;
        return nullptr;
    }

    enum class DungeonRunPhase : uint8
    {
        Idle,          // nobody has asked for a run
        // A RUN BEGINS AT THE RESET, NOT AT THE DOOR (#144). Measured
        // 2026-09-02: the party was staged, crossed together, and `dc on` was
        // accepted and verified - and the dungeon brain said "advance: no next
        // boss (all dead/skipped) -> stalling" within seconds, because the
        // `instance` row for map 36 held completedEncounters = 127 and the
        // leader was still bound to it. Everything about the walking worked and
        // the run was still a walk through an empty mine. So the reset comes
        // FIRST, before anybody is aimed anywhere, and it comes first on every
        // run of a campaign rather than only between them: the state the first
        // run of the day inherits is exactly the state the second one would.
        Resetting,     // nobody is inside; the leader resets the instance the party is bound to
        Gathering,     // leader aimed at the staging point, still walking
        Barrier,       // leader has arrived; waiting for the whole roster
        Enter,         // the party closes the last yards to the door and crosses together
        StagedInside,  // every member is on the instance map; handed to the clearing drive
        Clearing,      // the run is under way; the coordinator watches that the brain is on
        Exiting        // the run is over; the party goes back out the door it came in
    };

    // World-thread-only, unguarded, lost on restart - same discipline as
    // TravelAimBook's errand memory and _awaitingRelease (see their comments):
    // OnUpdate is the only caller of DriveDungeonRun, and a restart costs one
    // re-gather, not correctness.
    struct DungeonRunCoordinatorState
    {
        DungeonRunPhase phase{DungeonRunPhase::Idle};
        std::string portalKeyword;   // which DungeonPortal this run is for
        // WHERE THIS RUN IS GATHERING, RESOLVED ONCE WHEN IT STARTS. It lives
        // with the run rather than with the portal because it is not a property
        // of the portal at all: ResolveDungeonStagingPoint asks the map for the
        // ground at the standoff, and the answer depends on whether that grid
        // was in memory at the time. Re-resolving it every poll would let the
        // barrier circle move under a party already standing in it the moment
        // the grid loads, which is a gather that gets harder the closer it
        // gets. Resolved once, walked to, held to.
        float stageX{0.f}, stageY{0.f}, stageZ{0.f};
        // Said once per phase entry rather than once per poll - the log-once
        // flags every other drive in this file already uses (`arrived` in
        // TravelState, `stuckLogged` in RepickMemory) for the same reason:
        // BARRIER can legitimately hold for minutes while a slow follower
        // catches up, and repeating "still waiting" every five seconds would
        // bury the one line that matters.
        bool loggedGathering{false};
        bool loggedBarrierWaiting{false};
        bool loggedCrossingAim{false};
        bool loggedCrossingWaiting{false};
        bool loggedStagedWaiting{false};
        bool loggedAccepted{false};
        bool loggedNotAccepted{false};
        bool loggedMoved{false};
        bool loggedNotMoved{false};
        // A crossing's progress, kept in the shared ratchet - see
        // DUNGEON_CROSSING_RATCHET. The reading is the count already through,
        // which only ratchets upward within one crossing: a number that cannot
        // be beaten by a party standing still is a number a backstop can
        // trust, which is the same reason the travel drive measures distance
        // the same way and now through the same function. Shared by ENTER and
        // EXIT because only one crossing is ever in progress.
        OverseerDecisions::RatchetState crossing;
        // THE STAGING WATCHDOG'S PER-MEMBER MEMORY (#164): whether each member
        // is actually closing on the staging point, and how far up the
        // correction ladder it has been taken. It lives on the RUN rather than
        // beside _followStall because that is its lifetime - it is meaningless
        // outside BARRIER, it is cleared on the way into that phase and on the
        // way out of it, and a run that ends takes it with the rest of this
        // struct. See RunStagingWatchdog and OverseerDecisions::StagingWatchdog.
        std::map<std::string, OverseerDecisions::StagingStallState> staging;
        // CLEARING'S TWO CLOCKS (#140). `awaitingSince` is when this state
        // last started waiting for `dc on` to be accepted on everyone inside -
        // set on entry, and again whenever that stops being true, so the
        // grace is measured from the last change and not from a phase entry
        // that may be an hour old. The anchor is where the leader stood when
        // acceptance was first seen complete, and `anchorAt` when; the verdict
        // that the run is actually MOVING is measured against those. Both are
        // in-process on purpose: a bounce wipes them along with the flag they
        // were verifying, and the adopted run starts both clocks again.
        time_t awaitingSince{0};
        // WHEN THIS RUN STARTED BEING ASSEMBLED (#165), across RESETTING,
        // GATHERING and BARRIER together rather than per phase - a run that
        // spends four minutes in each of three phases has spent twelve minutes
        // not starting, and three separate clocks would each say everything was
        // fine. Stamped lazily on the first staging poll, which makes it
        // independent of WHICH path entered the phase, and zeroed with the rest
        // of this struct when a run ends. See DUNGEON_STAGING_BACKSTOP_SECONDS.
        time_t stagingSince{0};
        // THE CLEARING WATCHDOG'S MEMORY (#171). The same shape as
        // FollowStallState - a mark the subject is measured FROM, the shared
        // ratchet's reading of how far it has got from that mark, and a count
        // of what has been tried about it - with the RUN's lifetime rather than
        // a character's, which is why it lives here and not beside
        // _followStall. See RunClearingWatchdog.
        bool clearMarked{false};
        float clearX{0.f}, clearY{0.f};
        OverseerDecisions::RatchetState clearProgress;
        // The completed-encounter mask as this run last saw it. A boss dying is
        // progress that no amount of standing still can fake, and it is the one
        // progress signal that does not depend on where anybody is standing.
        uint32 clearEncounters{0};
        unsigned clearSkips{0};
        // WHY THE PARTY IS BEING WALKED OUT, when it is not simply over. Set by
        // the CLEARING watchdog and read by EXIT, so a run that died inside ends
        // on its row as what it was rather than as an ordinary 'left'. Empty for
        // every other way out.
        std::string stalledReason;
        bool anchorSet{false};
        float anchorX{0.f}, anchorY{0.f};
        time_t anchorAt{0};

        // ---- the campaign this run belongs to (#144) ----
        //
        // WHY THESE FOUR ARE IN-PROCESS WHEN THE CAP ITSELF IS A COLUMN. The
        // campaign's PROGRESS has to be durable, which is why
        // overseer_roster.dungeon_runs_done is a column and this struct does not
        // count runs itself. What is here is only which campaign and which run
        // THIS process is currently driving - a cache of two numbers that are
        // written onto the run row as soon as there is a run row to write them
        // to, and re-read off that row when a bounced worldserver adopts a run
        // it did not start. Losing them costs a read, not a count.
        uint32 campaignId{0};   // 0 until the campaign is allocated or read back
        uint32 runNumber{0};    // which run of the campaign is being staged or run
        uint32 runId{0};        // the overseer_dungeon_run row this coordinator drives
        uint32 wentIn{0};       // how many members crossed, for the wipe test at the end

        // THE CAP AS IT READ WHEN THIS RUN STARTED, carried rather than re-read
        // at the end of the run. `dungeon_runs_done` is incremented through
        // CharacterDatabase.Execute, which is queued on the async worker, so a
        // SELECT issued in the same breath as that increment could read the old
        // value and start a thirty-first run. Deciding from the numbers this run
        // began with needs no read at all, and the roster row catches up on its
        // own well inside the five seconds before the next poll. `capKnown` is
        // false when the columns do not exist, which is the one case the loop
        // must not run at all - see CampaignColumnsPresent.
        uint32 runsWanted{0};
        bool capKnown{false};

        // ---- RESETTING's two clocks, the same shape CLEARING's two are ----
        //
        // `resetSince` is when the reset started waiting for its PRECONDITIONS
        // (everybody out, nobody in combat) and bounds only that wait;
        // `resetAttempts` counts actual refusals from the core, which is a
        // different failure and deserves a different, much smaller bound. See
        // DUNGEON_RESET_BACKSTOP_SECONDS and DUNGEON_RESET_ATTEMPTS.
        time_t resetSince{0};
        uint32 resetAttempts{0};
        bool loggedResetWaiting{false};
        bool loggedCampaignOver{false};
    };
    DungeonRunCoordinatorState _dungeonRunCoordinator;

    // THE BARRIER AND CROSSING PREDICATES NOW LIVE IN overseer_decisions.h,
    // unchanged. They were written free of every core type so that a unit test
    // could exercise them with no world, no bot and no database, and they said
    // so in their own comments - but as private members of this class, in a
    // .cpp with no header, there was nothing such a test could include and
    // nothing it could link. Moving them is what makes the seam they already
    // describe reachable; that header carries the whole argument and their
    // comments verbatim.
    //
    // WHAT STAYED HERE, AND WHY THE LINE IS WHERE IT IS: everything below
    // touches the world. DungeonRunCensus needs an AreaTrigger and a Player,
    // DungeonRunKnock sends a packet. They are the mechanical half of a
    // crossing and were never testable without a world, so moving them would
    // have moved the core types into the file whose whole value is not having
    // any.

    // THE RUN'S BOSS CREDIT, as the core counts it and not as any module
    // reports it (#171). InstanceSave::GetCompletedEncounterMask
    // (InstanceSaveMgr.h:72) is the mask the core itself sets on every kill that
    // a DungeonEncounter.dbc row credits: KillRewarder calls
    // Map::UpdateEncounterState (KillRewarder.cpp:313, Map.cpp:2933-2975), which
    // ORs the bit in through InstanceScript::SetCompletedEncountersMask and
    // writes it straight back to the save (InstanceScript.cpp:756-773). So it
    // rises live during a run, needs no header from the dungeon module, and
    // cannot be faked by a party standing still.
    //
    // ZERO ON A LEADER THIS MAP HAS NOT BOUND YET, and the caller treats a
    // reading that does not RISE as no news rather than as a loss - so a bind
    // that is not there yet costs nothing and cannot un-credit a boss.
    //
    // THIS IS NOT "IS THE DUNGEON FINISHED". That question is still not
    // answerable here for map 36 and the CLEARING state's own comment says why
    // at length: Deadmines' script never calls SetBossState, so its encounter
    // COUNT is zero and "all done" would read true on entry. A mask that RISES
    // is a different claim and a safe one.
    static uint32 CompletedEncounters(Player* leader)
    {
        if (!leader)
            return 0;
        // PlayerGetBoundInstance  InstanceSaveMgr.h:207; InstancePlayerBind::save
        // InstanceSaveMgr.h:39-41. Both already used by ResetGroupInstance.
        if (InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
                leader->GetGUID(), leader->GetMapId(), DUNGEON_DIFFICULTY_NORMAL))
            if (bind->save)
                return bind->save->GetCompletedEncounterMask();
        return 0;
    }

    // ---- the CLEARING watchdog: has the run gone anywhere (#171) ----
    //
    // A DIFFERENT STALL FROM THE STAGING ONE, AND THE WORST OF THE THREE. The
    // staging watchdog below bounds a member that will not reach the door and
    // the whole-run staging clock bounds a barrier that will not open; neither
    // of them is looking any more once the party is INSIDE. This one is, and the
    // thing it caught was a healthy party frozen at a cannon for twenty-six
    // minutes with a thirty-second timeout printed beside it every five seconds.
    //
    // THE CONDITION IS MEASURED, NOT READ OFF ANOTHER MODULE'S LOG. That is a
    // requirement rather than a preference: a log line is a format somebody else
    // owns and may change, and a watchdog that parses one is a watchdog that
    // fails silently the day they do. Three facts this module can take for
    // itself, all of them public core API:
    //
    //   - NO BOSS CREDIT. The completed-encounter mask has not risen. See
    //     CompletedEncounters above.
    //   - NO MOVEMENT. The tank has not got DUNGEON_CLEAR_STALL_YARDS from a
    //     mark this drive moves to wherever it last saw it going somewhere -
    //     the shared ratchet, on the reading the follower stall already uses.
    //   - NOT BUSY. Nobody inside is fighting, looting, sitting to eat or drink,
    //     dead, or waiting on a resurrect. Every one of those is a party doing
    //     something legitimately slow, and every one of them HOLDS the clock
    //     rather than being absorbed into the patience: a bound that has to be
    //     wide enough to cover the longest boss fight is a bound that cannot
    //     catch a stall shorter than one.
    //
    // THE REMEDY THE MODULE ALREADY SHIPS COMES FIRST. `dc skip` is what a
    // person types at this exact state, and it worked instantly when one did.
    // It goes out through the same door `dc on` does - DoSpecificAction with an
    // authorized issuer - for all the reasons the arming drive sets out, and it
    // goes to the LEADER because DcSkipAction returns early for anybody who is
    // not the dungeon module's elected leader
    // (DungeonClearChatActions.cpp:382-383).
    //
    // WHAT IT CANNOT SAY, HONESTLY. Which objective was abandoned. The name
    // ('Iron Clad Door (Defias Cannon)') exists only in the dungeon module's own
    // log and in an addon-channel packet sent to real clients; there is no table
    // and no accessor this module can read it from, and inventing one by parsing
    // that log is exactly the coupling the condition above avoids. So this line
    // says what it skipped FOR, where the party was, and how long it had stood
    // there, and points at the other module's log for the name.
    //
    // Returns true when it has taken the run out of CLEARING, so the caller
    // stops rather than falling through into the arming verdict for a phase that
    // is over.
    using DungeonRunEntryStates = std::vector<OverseerDecisions::DungeonRunEntryState>;
    bool RunClearingWatchdog(DungeonRunCoordinatorState& coord, Player* leader,
                             DungeonRunEntryStates const& states, uint32 insideMapId)
    {
        time_t const now = std::time(nullptr);

        // A BOSS DIED. Unambiguous progress, and it resets the whole ladder:
        // three skips spent getting past one obstacle should not count against
        // the next one. Only a RISE counts - see CompletedEncounters.
        uint32 const encounters = CompletedEncounters(leader);
        if (encounters > coord.clearEncounters)
        {
            coord.clearEncounters = encounters;
            coord.clearProgress.best = 0.f;
            coord.clearProgress.since = now;
            coord.clearMarked = false;
            coord.clearSkips = 0;
            return false;
        }

        // BUSY IS NOT STALLED. IsInCombat Unit.h:936; IsAlive Unit.h:1793;
        // IsSitState Unit.h:1781 (public from :666) covers eating and drinking,
        // which is what a party does between pulls; GetLootGUID Player.h:2026
        // and isResurrectRequested Player.h:1856 (both public from :1091) cover
        // the two that look most like standing still and are not.
        //
        // ASKED OF EVERY MEMBER, NOT ONLY THE ONES INSIDE. A member who died and
        // released to the graveyard outside is a party legitimately waiting for
        // somebody to run back, and it is the case most likely to look like a
        // freeze from in here. The whole party leaving is a different fact and
        // is already answered above, where a run that holds nobody ends.
        for (OverseerDecisions::DungeonRunEntryState const& state : states)
        {
            Player* member = ObjectAccessor::FindPlayerByName(state.name);
            if (!SteerableAI(member))
                continue;
            if (member->IsInCombat() || !member->IsAlive() || member->IsSitState() ||
                !member->GetLootGUID().IsEmpty() || member->isResurrectRequested())
            {
                coord.clearProgress.since = now;
                return false;
            }
        }

        // THE MARK IS A POSITION AND THE RATCHET TAKES ONE NUMBER, so the mark is
        // dropped here exactly as KeepRosterFollowing's stall check drops its
        // own, and for the same reason.
        float const movedFromMark =
            coord.clearMarked ? leader->GetExactDist2d(coord.clearX, coord.clearY) : 0.f;
        OverseerDecisions::RatchetVerdict const progress = OverseerDecisions::Ratchet(
            coord.clearProgress, movedFromMark, now, DUNGEON_CLEAR_RATCHET);

        if (!coord.clearMarked || progress.progressed)
        {
            coord.clearMarked = true;
            coord.clearX = leader->GetPositionX();
            coord.clearY = leader->GetPositionY();
            coord.clearProgress.since = now;
            // The run is moving, so whatever it had to skip to get moving is
            // paid for. A later obstacle gets the full three tries of its own.
            coord.clearSkips = 0;
            return false;
        }
        if (!progress.stalled)
            return false;

        // STALLED. The clock restarts on every rung, so whatever is tried here
        // gets a whole patience window to work in before the next thing is.
        coord.clearProgress.since = now;

        if (coord.clearSkips < DUNGEON_CLEAR_SKIPS)
        {
            ++coord.clearSkips;
            Player* issuer = AuthorizedDcIssuer(leader);
            PlayerbotAI* leaderAI = SteerableAI(leader);
            // Event(source, param, owner)  Event.h:21-24; the owner is what
            // IsAuthorized reads (DungeonClearChatActions.cpp:62), the same way
            // the arming drive issues `dc on`.
            bool const skipped =
                issuer && leaderAI &&
                leaderAI->DoSpecificAction("dc skip", Event("dc", "", issuer), true);
            LOG_WARN("module.overseer",
                     "overseer: dungeon run CLEARING has not moved '{}' more than {}y on "
                     "map {} for {} minutes, with nobody fighting, looting, resting, dead "
                     "or being resurrected, and no boss credited - so it is stuck on an "
                     "objective it cannot finish. Issuing 'dc skip' ({} of {}) as '{}': "
                     "{}. Which objective is in the dungeon module's own log, which is "
                     "the only place it exists",
                     leader->GetName(), static_cast<uint32>(DUNGEON_CLEAR_STALL_YARDS),
                     insideMapId,
                     static_cast<uint32>(DUNGEON_CLEAR_STALL_SECONDS / 60),
                     coord.clearSkips, static_cast<uint32>(DUNGEON_CLEAR_SKIPS),
                     issuer ? issuer->GetName() : "nobody",
                     skipped ? "accepted"
                             : "REFUSED or unavailable - the dungeon module's log says "
                               "why under 'DC command refused'");
            return false;
        }

        // SKIPPING DOES NOT REACH IT. Said once, and then the run ENDS rather
        // than being narrated for another half hour. It ends by walking the
        // party out through EXIT like every other finished run - the coordinator
        // has one way to leave an instance and this is not the place to grow a
        // second - and `stalledReason` is what makes the row that EXIT closes
        // say what actually happened instead of 'left'.
        coord.stalledReason =
            "the run stopped progressing inside map " + std::to_string(insideMapId) +
            ": no boss credit and no movement for " +
            std::to_string((DUNGEON_CLEAR_SKIPS + 1) * DUNGEON_CLEAR_STALL_SECONDS / 60) +
            " minutes, and " + std::to_string(DUNGEON_CLEAR_SKIPS) +
            " 'dc skip's did not restart it";
        LOG_ERROR("module.overseer",
                  "overseer: dungeon run {} of campaign {} cannot be cleared - {} 'dc "
                  "skip's and '{}' has still not moved {}y on map {}. Walking the party "
                  "back out through EXIT and ending the run so the next poll can open a "
                  "fresh one; a run left 'active' inside a dungeon it is not clearing is "
                  "the half hour of nothing this bound exists to end",
                  coord.runNumber, coord.campaignId,
                  static_cast<uint32>(DUNGEON_CLEAR_SKIPS), leader->GetName(),
                  static_cast<uint32>(DUNGEON_CLEAR_STALL_YARDS), insideMapId);

        coord.phase = DungeonRunPhase::Exiting;
        coord.crossing.best = 0.f;
        coord.crossing.since = std::time(nullptr);
        coord.loggedCrossingAim = false;
        coord.loggedCrossingWaiting = false;
        return true;
    }

    // ---- the staging watchdog: one member, one poll (#164) ----
    //
    // BARRIER ALREADY KNEW. It prints the gap for every member holding it up -
    // "BARRIER holds - Bork (18y away), Grog (549y away), Grug (in combat), Og
    // (22y away), Ugga (wrong map)" - and what it could never say is whether
    // Grog was walking in from 549 yards or standing at a herb node 549 yards
    // out. Those are the same line. The operator asked for that difference to
    // be detected and corrected by the module rather than by somebody watching
    // a stream, so the reading BARRIER already takes is put through the
    // ratchet's question - is the gap CLOSING - and the answer through an
    // escalating, bounded ladder of corrections.
    //
    // THE LADDER AND ITS BOUND LIVE IN OverseerDecisions::StagingWatchdog, where
    // a test can exercise them with no world, no bot and no database, and where
    // the argument for the shape is written down. What is here is the two halves
    // that need a world: whether this poll's distance is a reading about walking
    // at all, and what each rung actually does to the character.
    //
    // IT IS NOT A SECOND STALL MECHANISM. The comparison, the mark, the clock
    // and the verdict are OverseerDecisions::Ratchet - the same four that
    // KeepRosterFollowing's stall check, the travel backstop, the crossing
    // backstop and the quest re-pick all already share. What differs is the
    // reading (DistanceToTarget, because a staged member IS being sent
    // somewhere) and the reaction, which is the only part any of those five
    // sites ever had of its own.
    void RunStagingWatchdog(DungeonRunCoordinatorState& coord, std::string const& name,
                            Player* member,
                            OverseerDecisions::DungeonRunMemberState const& state)
    {
        // A STAGED MEMBER IS NOT WATCHED, AND ITS LADDER GOES WITH IT. Inside
        // the barrier radius there is nothing left to close. The leader in
        // particular is HELD there on purpose - BARRIER escorts him at any
        // distance, including none, so that his own `new rpg` cannot go idle
        // and wander off while the stragglers arrive - and a watchdog that
        // measured him would find a character that never gets nearer, because
        // it is already there, and start correcting the one member doing
        // exactly what was asked. Erased rather than merely skipped, so a
        // member that arrives, drifts back out and returns is watched from the
        // bottom of the ladder rather than from the rung its last bad patch
        // reached.
        if (state.distanceFromStage >= 0.f &&
            state.distanceFromStage <= DUNGEON_BARRIER_RADIUS_YARDS)
        {
            coord.staging.erase(name);
            return;
        }

        // WHEN THIS POLL'S DISTANCE IS NOT A READING ABOUT WALKING. A fight is
        // a pause and not a stall. A taxi leg routinely goes the wrong way
        // round a mountain, which is exactly why DriveTravel holds its own
        // errand clock over one. A dead member is a ghost walking to a corpse,
        // which is DriveStuckRevival's business and not a distance from
        // anybody. And a member on another map has no distance to this staging
        // point at all - the blockers line already names it, and an `at:` aim
        // cannot cross a map anyway. All four hold the clock rather than run
        // it, so none of them can spend a rung of the ladder.
        //
        // IsInFlight  Unit.h:1709  bool IsInFlight() const
        bool const measurable = state.seen && state.alive && !state.inCombat &&
                                state.distanceFromStage >= 0.f && !member->IsInFlight();

        OverseerDecisions::StagingStallState& stall = coord.staging[name];
        OverseerDecisions::StagingNudge const nudge = OverseerDecisions::StagingWatchdog(
            stall, state.distanceFromStage, measurable, std::time(nullptr),
            DUNGEON_STAGING_RATCHET);

        switch (nudge)
        {
            case OverseerDecisions::StagingNudge::Nothing:
                return;

            case OverseerDecisions::StagingNudge::Restrategy:
            {
                // RUNG ONE, AND IT IS FIRST BECAUSE IT MOVES NOTHING. If
                // something has put a diverter back on this character's engine
                // then it is being steered somewhere else, which is a complete
                // explanation for a gap that will not close and is the measured
                // cause of both halves of #164. Re-asserting costs a read of
                // the live list and, usually, no write at all.
                std::string const took = AssertTravelFocus(name);
                LOG_WARN("module.overseer",
                         "overseer: dungeon run BARRIER - '{}' has got no nearer than {} "
                         "yards to the staging point for {} seconds and is {} yards out. "
                         "Correction 1 of {}: re-asserting the escort's own strategy set "
                         "- {}",
                         name, static_cast<uint32>(stall.progress.best),
                         static_cast<uint32>(DUNGEON_STAGING_STALL_SECONDS),
                         static_cast<uint32>(state.distanceFromStage),
                         static_cast<uint32>(OverseerDecisions::STAGING_NUDGE_STEPS),
                         took.empty() ? std::string("nothing had come back on, so this "
                                                    "is not what is holding it")
                                      : ("took " + took + " back off it"));
                return;
            }

            case OverseerDecisions::StagingNudge::Reaim:
            {
                // RUNG TWO. The aim is still on the roster row - EscortToward
                // re-claims it every poll - so what can be missing is the walk
                // itself, and DriveTravel will not hand it over again while the
                // character's own `rpgInfo` still names the destination. This
                // is the one caller that knows better; see TravelState::reissue.
                _travelAims.StateFor(name).reissue = true;
                LOG_WARN("module.overseer",
                         "overseer: dungeon run BARRIER - '{}' is still {} yards out and "
                         "no nearer. Correction 2 of {}: the walk is handed to it again "
                         "on the next travel poll, over the guard that would otherwise "
                         "read its own stale state as proof it is already walking",
                         name, static_cast<uint32>(state.distanceFromStage),
                         static_cast<uint32>(OverseerDecisions::STAGING_NUDGE_STEPS));
                return;
            }

            case OverseerDecisions::StagingNudge::ClearMovement:
            {
                // RUNG THREE, AND IT IS LAST BECAUSE IT THROWS AWAY WHATEVER
                // THE CHARACTER WAS IN THE MIDDLE OF. Exactly the recovery
                // KeepRosterFollowing already applies to a follower jittering
                // in place, for exactly the reason set out there: a generator
                // that keeps producing small motion without ever arriving is
                // what MotionMaster::Clear() (MotionMaster.h:193, public from
                // :160) exists to discard, so the next tick starts a fresh move
                // instead of repeating a stale one. GetMotionMaster is public
                // (Unit.h:1758, public from Unit.h:666).
                member->GetMotionMaster()->Clear();
                LOG_WARN("module.overseer",
                         "overseer: dungeon run BARRIER - '{}' is still {} yards out and "
                         "no nearer. Correction 3 of {}: clearing its movement so the "
                         "next tick starts fresh, the same recovery the follow drive uses "
                         "for a follower jittering in place",
                         name, static_cast<uint32>(state.distanceFromStage),
                         static_cast<uint32>(OverseerDecisions::STAGING_NUDGE_STEPS));
                return;
            }

            case OverseerDecisions::StagingNudge::GiveUp:
                // SAID PLAINLY, ONCE, AND THEN NOTHING. A self-correcting
                // mechanism that never gives up is a loop with a log line in
                // it. The three corrections have each had a full
                // DUNGEON_STAGING_STALL_SECONDS to work and none of them did,
                // so this member is not going to be staged by anything this
                // watchdog can do, and saying so is more use than a fourth
                // attempt. What owns it from here is the errand's own
                // twenty-minute unreachable backstop in DriveTravel, and the
                // operator, who now has one line naming the character, the gap
                // and everything that was tried.
                LOG_ERROR("module.overseer",
                          "overseer: dungeon run BARRIER cannot stage '{}' - {} yards from "
                          "the staging point, no nearer for {} minutes, and all {} "
                          "corrections have been tried and made no difference. Nothing "
                          "further will be attempted for it; the errand's own {}-minute "
                          "unreachable backstop bounds the aim and the run's own timeout "
                          "owns the run",
                          name, static_cast<uint32>(state.distanceFromStage),
                          static_cast<uint32>(
                              (OverseerDecisions::STAGING_NUDGE_STEPS + 1) *
                              DUNGEON_STAGING_STALL_SECONDS / 60),
                          static_cast<uint32>(OverseerDecisions::STAGING_NUDGE_STEPS),
                          static_cast<uint32>(TRAVEL_BACKSTOP_SECONDS / 60));
                return;
        }
    }

    // ---- the mechanical half of a crossing, shared by ENTER and EXIT ----
    //
    // These two touch the world and so cannot be pure like the predicates
    // above; they are still written once rather than twice, for the reason the
    // predicates give.

    // Where every roster member stands relative to one door. `throughMapId` is
    // the map a member is on once it is through - the instance map for ENTER,
    // the map outside it for EXIT.
    static std::vector<OverseerDecisions::DungeonRunEntryState> DungeonRunCensus(
        std::vector<std::string> const& members, AreaTrigger const* door,
        uint32 throughMapId, uint32& through)
    {
        through = 0;

        std::vector<OverseerDecisions::DungeonRunEntryState> states;
        states.reserve(members.size());
        for (std::string const& name : members)
        {
            OverseerDecisions::DungeonRunEntryState state;
            state.name = name;

            // SteerableAI, not a bare lookup - a name can resolve to a Player
            // mid-login or mid-teardown. A member this poll cannot find is the
            // "not seen" case both predicates fail closed on.
            Player* member = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(member))
            {
                states.push_back(state);
                continue;
            }

            state.seen = true;
            // IsAlive  Unit.h:1793  bool IsAlive() const
            state.alive = member->IsAlive();
            // IsInCombat  Unit.h:936  bool IsInCombat() const
            state.inCombat = member->IsInCombat();

            // GetMapId  Position.h:281  uint32 GetMapId() const
            uint32 const memberMap = member->GetMapId();
            if (memberMap == throughMapId)
            {
                state.through = true;
                ++through;
            }
            // AreaTrigger::map  ObjectMgr.h:425  uint32 map
            else if (memberMap == door->map)
            {
                // AreaTrigger::x  ObjectMgr.h:426  float x
                // AreaTrigger::y  ObjectMgr.h:427  float y
                // GetDistance2d  Object.h:538  float GetDistance2d(float x, float y) const
                state.distanceFromDoor = member->GetDistance2d(door->x, door->y);
            }

            states.push_back(state);
        }
        return states;
    }

    // KNOCK, FOLLOWERS FIRST AND THE LEADER LAST. Returns how many actually
    // changed map, which is the only thing that may be read as having crossed.
    //
    // A pair of passes rather than a sort, because the only thing being ordered
    // is "is this the leader", and the leader is a name the caller already
    // holds. The ordering argument is in the section comment above and applies
    // in both directions: the leader is the anchor every follower is standing
    // next to, so he is the last thing that may move.
    uint32 DungeonRunKnock(std::vector<OverseerDecisions::DungeonRunEntryState> const& states,
                           std::string const& leaderName, uint32 triggerId)
    {
        std::ostringstream doorAim;
        doorAim << "trigger:" << triggerId;
        std::string const doorTarget = doorAim.str();

        bool const passes[2] = {false, true};
        uint32 crossed = 0;
        for (bool leaderPass : passes)
        {
            for (OverseerDecisions::DungeonRunEntryState const& state : states)
            {
                if (state.through)
                    continue;
                if ((state.name == leaderName) != leaderPass)
                    continue;

                Player* member = ObjectAccessor::FindPlayerByName(state.name);
                if (!SteerableAI(member))
                    continue;   // gone since the census - next poll

                // Returns true ONLY if the character actually changed map. A
                // refusal is the handler's own IsInAreaTriggerRadius being
                // stricter than the doorstep gate, and is not an error: the
                // member stays where it is and is knocked for again next poll.
                if (StepThroughAreaTrigger(state.name, member, doorTarget))
                    ++crossed;
            }
        }
        return crossed;
    }

    // What one poll of a crossing decided. Named rather than returned as a pair
    // of bools, because "still working", "everybody is through" and "the
    // backstop fired" are three outcomes and a caller that gets two bools can
    // be written to handle a fourth combination that does not exist.
    enum class DungeonCrossingResult : uint8
    {
        Working,   // walking, holding, or knocking - nothing decided this poll
        Through,   // every member is on the far side
        GaveUp     // no progress for DUNGEON_CROSSING_BACKSTOP_SECONDS
    };

    // ONE CROSSING, RUN ONCE PER POLL, IN EITHER DIRECTION. `what` is the state
    // name for the log lines ("ENTER" / "EXIT") so the reader of a log can tell
    // which door is being knocked on without inferring it from a trigger id.
    //
    // The caller owns what a result MEANS - which phase comes next, whether
    // giving up returns to IDLE or says something else - because that is the
    // only part that genuinely differs between the two directions.
    DungeonCrossingResult DriveDungeonCrossing(std::vector<std::string> const& members,
                                               std::string const& leaderName,
                                               AreaTrigger const* door, uint32 triggerId,
                                               uint32 throughMapId, char const* what,
                                               DungeonRunCoordinatorState& coord)
    {
        uint32 through = 0;
        std::vector<OverseerDecisions::DungeonRunEntryState> const states =
            DungeonRunCensus(members, door, throughMapId, through);

        // PROGRESS RESTARTS THE BACKSTOP'S CLOCK (#63's lesson, applied to a
        // different journey). One more member through is the only thing that
        // counts as progress here, and it is a fact a party standing at a door
        // cannot fake. The comparison and the clock are the shared ratchet;
        // the log-once flag is this site's own reaction to progress and stays
        // with it.
        OverseerDecisions::RatchetVerdict const progress = OverseerDecisions::Ratchet(
            coord.crossing, static_cast<float>(through), std::time(nullptr),
            DUNGEON_CROSSING_RATCHET);
        if (progress.progressed)
            coord.loggedCrossingWaiting = false;

        if (OverseerDecisions::DungeonRunAllThrough(states))
            return DungeonCrossingResult::Through;

        // BACKSTOP. A crossing that can never finish must not pin the party to
        // a door forever - that is five characters standing still, which is the
        // state this epic exists to stop mistaking for a working one. Read with
        // the progress check above, a stall means nobody has crossed for
        // DUNGEON_CROSSING_BACKSTOP_SECONDS, not that the crossing has simply
        // been going on that long.
        if (progress.stalled)
        {
            LOG_WARN("module.overseer",
                     "overseer: dungeon run {} has got {} of {} through areatrigger {} and "
                     "nobody else has crossed for {} minutes - giving up. Still short: {}",
                     what, through, static_cast<uint32>(states.size()), triggerId,
                     static_cast<uint32>(DUNGEON_CROSSING_BACKSTOP_SECONDS / 60),
                     OverseerDecisions::DungeonRunEntryBlockers(
                         states, DUNGEON_DOORSTEP_RADIUS_YARDS));
            return DungeonCrossingResult::GaveUp;
        }

        // WALK THE LEADER THE LAST YARDS, AND ONLY THE LEADER, with an `at:`
        // aim on the door's own coordinates. Deliberately NOT a `trigger:` aim:
        // that would have DriveTravel step the LEADER through on arrival,
        // alone and ahead of everyone following him, which is the failure this
        // state exists to prevent. `at:` only ever walks.
        //
        // CLAIMED RATHER THAN WRITTEN, and the guard that used to sit here is
        // gone with the raw write. This used to run a whole-roster SELECT of
        // `travel_npc` on every poll of a crossing purely to avoid stamping on a
        // live errand - a mutex improvised on a column because nothing owned it.
        // TravelAimBook owns it now, and Claim is idempotent against the walk in
        // flight from the memory it already keeps, so re-issuing over the aim
        // this state wrote last poll cannot happen and costs no query to avoid.
        // That is the thing the guard was written for: ChangeToWanderNpc's own
        // re-issue guard resets `lastReach` and `startT`, and defeating it from
        // out here is what would oscillate.
        //
        // WHAT THE GUARD ALSO DID BY ACCIDENT, AND DELIBERATELY NO LONGER DOES.
        // It refused to write while ANY other aim was outstanding, including one
        // this same coordinator had written moments earlier. BARRIER is satisfied
        // at DUNGEON_BARRIER_RADIUS_YARDS (10) while DriveTravel only releases a
        // staged `at:` errand at TRAVEL_ARRIVED_POSITION_YARDS (5), so a leader
        // that settles between those two figures reaches ENTER with its staging
        // errand still live - and the old guard then refused the door aim until
        // that errand's twenty-minute backstop fired. A crossing that cannot be
        // started because the walk to its own doorstep is still finishing is the
        // party standing still, which is the state this epic exists to stop
        // mistaking for a working one. A run supersedes an errand; Claim takes
        // the wheel and says so in its name.
        //
        // EVERY MEMBER WALKS THE LAST YARDS ITSELF, NOT JUST THE LEADER (#122).
        //
        // This used to aim the leader alone, on the reasoning that a follower
        // "comes with him by following". It does not, and the measurement is
        // unambiguous: the leader reached map 36 under his own power twice
        // while the other four stayed outside, strung out over several hundred
        // yards. `follow` has no way to cross a doorway - FollowAction only
        // acts while the master is on the same map, and what actually crosses
        // is a packet sent by a character standing inside the trigger's own
        // radius - and it is not reliably closing the last yards either.
        //
        // So each member that is on this door's map and not yet through is
        // ESCORTED to it: same `at:` aim on the door's own coordinates, same
        // column, same drive. Deliberately still NOT a `trigger:` aim - that
        // would have DriveTravel step each character through the moment it
        // arrived, which is the party trickling in one at a time and the leader
        // possibly first. `at:` only ever walks; the knock below is what
        // crosses, it happens for everybody at once, and it puts the leader
        // last so the anchor is the last thing to move.
        //
        // ONE COMPARISON COVERS THREE CASES, and deliberately: a member already
        // through and a member on some third map both carry a NEGATIVE
        // distanceFromDoor, and neither can be walked to this door by an `at:`
        // aim - ResolveTravelTarget refuses an `at:` whose map is not the
        // character's own. Testing `>= 0` keeps an aim that could only be
        // refused from ever being written, and both cases still show up in the
        // holds line below.
        //
        // THE DOORSTEP IS NOT THE CUT-OFF, which is the difference between a
        // party assembling and a party dissolving. A member inside
        // DUNGEON_DOORSTEP_RADIUS_YARDS keeps its escort so that DriveTravel
        // holds it there (see that drive's escort arrival branch); dropping the
        // escort on arrival would idle it, and an idle `new rpg` picks a random
        // errand on its next tick. Members stand at the door until everyone is
        // there.
        std::ostringstream aim;
        // AreaTrigger::map/x/y/z  ObjectMgr.h:425,426,427,428
        aim << "at:" << door->map << ':' << door->x << ',' << door->y << ',' << door->z;
        std::string const doorAim = aim.str();

        OverseerDecisions::DungeonRunEntryState const* leaderState = nullptr;
        for (OverseerDecisions::DungeonRunEntryState const& state : states)
        {
            if (state.name == leaderName)
                leaderState = &state;
            if (!state.through && state.distanceFromDoor >= 0.f)
                EscortToward(state.name, doorAim, what);
        }

        if (!coord.loggedCrossingAim)
        {
            coord.loggedCrossingAim = true;
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} walks the party the last yards onto "
                     "areatrigger {} ({}) - every member under its own power, and nobody "
                     "is knocked through until everybody is on the doorstep. Leader '{}' "
                     "is {:.0f}y out",
                     what, triggerId, doorAim, leaderName,
                     leaderState ? leaderState->distanceFromDoor : -1.f);
        }

        if (!OverseerDecisions::DungeonRunEntryReady(states, DUNGEON_DOORSTEP_RADIUS_YARDS))
        {
            if (!coord.loggedCrossingWaiting)
            {
                coord.loggedCrossingWaiting = true;
                LOG_INFO("module.overseer",
                         "overseer: dungeon run {} holds - {} of {} are through areatrigger "
                         "{} and the rest are not on the doorstep yet: {}",
                         what, through, static_cast<uint32>(states.size()), triggerId,
                         OverseerDecisions::DungeonRunEntryBlockers(
                             states, DUNGEON_DOORSTEP_RADIUS_YARDS));
            }
            return DungeonCrossingResult::Working;
        }

        uint32 const crossed = DungeonRunKnock(states, leaderName, triggerId);
        if (crossed)
        {
            coord.loggedCrossingWaiting = false;
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} knocked on areatrigger {} for the whole "
                     "party at once - {} crossed onto map {} this poll, {} of {} are now "
                     "through",
                     what, triggerId, crossed, throughMapId, through + crossed,
                     static_cast<uint32>(states.size()));
            return DungeonCrossingResult::Working;
        }

        // EVERYBODY ON THE DOORSTEP AND NOT ONE CROSSED. Said, not skipped. The
        // gate is DUNGEON_DOORSTEP_RADIUS_YARDS (5) against triggers of radius
        // 7 and 6, so this should not happen - and if it starts happening, a
        // line is how that becomes visible rather than a party quietly standing
        // at a door until the backstop fires.
        if (!coord.loggedCrossingWaiting)
        {
            coord.loggedCrossingWaiting = true;
            LOG_WARN("module.overseer",
                     "overseer: dungeon run {} has every member within {:.0f}y of "
                     "areatrigger {} and the server refused all of them - its own "
                     "IsInAreaTriggerRadius is the authority and is stricter than that "
                     "gate. Knocking again next poll, bounded by the {} minute backstop",
                     what, DUNGEON_DOORSTEP_RADIUS_YARDS, triggerId,
                     static_cast<uint32>(DUNGEON_CROSSING_BACKSTOP_SECONDS / 60));
        }
        return DungeonCrossingResult::Working;
    }

    // ------------------------------------------------------ dungeon run: reset --
    //
    // WHAT A RESET IS PRECONDITIONED ON, ALL FOUR OF THEM READ OUT OF THE
    // PINNED CORE RATHER THAN GUESSED. This is a list of ways the core will
    // decline, each of which it declines SILENTLY as far as this module is
    // concerned - Group::ResetInstances returns void and reports itself to a
    // CLIENT, which is precisely the class of call this module has already been
    // burned by twice (see the "`delivered` is not `done`" section in AGENTS.md,
    // and Trainer::TeachSpell before it). Checking them here is what turns four
    // silent no-ops into four named log lines.
    //
    //   1. THE GROUP, AND THE LEADER OF IT. The client's reset-all is
    //      WorldSession::HandleResetInstancesOpcode (MiscHandler.cpp:1255),
    //      which calls group->ResetInstances(INSTANCE_RESET_ALL, false, _player)
    //      only `if (group->IsLeader(_player->GetGUID()))` (MiscHandler.cpp:1262).
    //      Group::ResetInstances itself does NOT re-check that - it walks
    //      whichever player it is handed - so issuing it from a non-leader would
    //      not fail, it would reset the wrong character's binds. And it would
    //      not help: the instance a GROUPED player lands in is chosen from the
    //      GROUP LEADER's bind, not their own
    //      (InstanceSaveMgr::PlayerGetDestinationInstanceId,
    //      InstanceSaveMgr.cpp:957 - "2. leader temp/perm" wins over the
    //      player's own temp bind). Resetting a follower would leave the party
    //      walking back into the same saved instance.
    //
    //   2. NOT A BATTLEGROUND, BATTLEFIELD OR DUNGEON-FINDER GROUP:
    //      Group::ResetInstances returns immediately for those
    //      (Group.cpp:2330). The roster's group is an ordinary party, so this
    //      can only fire if something else put them in a queue - which is worth
    //      a line rather than a silent nothing.
    //
    //   3. NORMAL DUNGEON DIFFICULTY. INSTANCE_RESET_ALL breaks out at once
    //      unless leader->GetDifficulty(false) == DUNGEON_DIFFICULTY_NORMAL
    //      (Group.cpp:2337). Heroic lockouts are #12 and are not handled here;
    //      what IS handled is saying so instead of resetting nothing.
    //
    //   4. EVERY MEMBER OUT OF THE INSTANCE. This is the one the coordinator
    //      has to SEQUENCE rather than merely check, and it is why the reset is
    //      a phase rather than a line inside EXIT. Group::ResetInstances hands
    //      each bound save to InstanceMap::Reset (Map.cpp:2210), whose return
    //      value is m_mapRefMgr.IsEmpty() (Map.cpp:2249) - so an instance with
    //      anybody still standing in it refuses the reset and tells everyone
    //      inside instead (SendResetFailedNotify, Map.cpp:2240). The party has
    //      to be all the way out first, which the existing EXIT crossing is
    //      already the thing that does.
    //
    // Out of combat is on this list too, and it is the one precondition the CORE
    // does not care about: a party still fighting something that followed them
    // out of the door is not a party that is about to walk back in, and BARRIER
    // would refuse to open for them a moment later anyway. Waiting for it here
    // keeps the reset and the re-entry from disagreeing about when the party is
    // ready.
    //
    // Returns an empty string when the reset may be attempted, or the reason it
    // may not - phrased so the log line reads as a sentence.
    std::string DungeonResetBlockers(std::vector<std::string> const& members,
                                     std::string const& leaderName,
                                     uint32 insideMapId)
    {
        Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
        if (!SteerableAI(leader))
            return "the leader is not in the world right now";

        // GetGroup  Player.h  Group* GetGroup() const
        Group* group = leader->GetGroup();
        if (!group)
            return "the leader is in no group, so there is no group instance to reset";

        // IsLeader  Group.h:239  bool IsLeader(ObjectGuid guid) const
        if (!group->IsLeader(leader->GetGUID()))
            return "'" + leaderName + "' is not the group leader, and the instance a "
                   "grouped character lands in is chosen from the GROUP leader's bind";

        // isBGGroup/isBFGroup/isLFGGroup  Group.h:222-225
        if (group->isBGGroup() || group->isBFGroup() || group->isLFGGroup())
            return "this is a battleground, battlefield or dungeon-finder group, which "
                   "Group::ResetInstances refuses outright";

        // GetDifficulty  Player.h:1948  Difficulty GetDifficulty(bool isRaid) const
        if (leader->GetDifficulty(false) != DUNGEON_DIFFICULTY_NORMAL)
            return "the party is not on normal dungeon difficulty, and INSTANCE_RESET_ALL "
                   "only resets normal five-mans (heroic lockouts are #12)";

        std::string inside;
        std::string fighting;
        for (std::string const& name : members)
        {
            Player* member = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(member))
                continue;   // not in the world, and therefore not on the map either

            // GetMapId  Position.h:281  uint32 GetMapId() const
            if (member->GetMapId() == insideMapId)
            {
                if (!inside.empty())
                    inside += ", ";
                inside += name;
            }
            // IsInCombat  Unit.h:936  bool IsInCombat() const
            else if (member->IsInCombat())
            {
                if (!fighting.empty())
                    fighting += ", ";
                fighting += name;
            }
        }

        if (!inside.empty())
            return "still on map " + std::to_string(insideMapId) + ": " + inside;
        if (!fighting.empty())
            return "still in combat: " + fighting;
        return "";
    }

    // DO THE RESET, AND THEN READ THE WORLD BACK.
    //
    // The action is exactly the one a person clicking "Reset all instances"
    // takes - INSTANCE_RESET_ALL (Map.h:710) through
    // Group::ResetInstances(uint8, bool, Player*) (Group.h:283, Group.cpp:2328)
    // - and deliberately nothing more. In particular this does NOT call
    // InstanceSaveMgr::PlayerUnbindInstance (InstanceSaveMgr.h:205) on anybody
    // by hand. That was considered and rejected twice over: it is not a thing a
    // player can do, so a bug it papers over is a bug nobody would ever have
    // found from a client, and it is unnecessary anyway - the core's own reset
    // path ends in UnbindAllFor(save) (Group.cpp:2362, InstanceSaveMgr.cpp:988),
    // which unbinds every character on that save's player list, which is all
    // five of them because all five entered through the leader's save.
    // AGENTS.md's standing instruction is the general form of this: fix the
    // code, never reach for the admin shortcut.
    //
    // AND IT RESETS EVERY NORMAL FIVE-MAN THE LEADER IS BOUND TO, NOT JUST THIS
    // ONE. Group::ResetInstances walks the leader's whole bound-instance map for
    // DUNGEON_DIFFICULTY_NORMAL (Group.cpp:2340-2341) and resets each entry that
    // is not a raid and can be reset. That is not an oversight to work around -
    // it is precisely what the button on a player's portrait does, and this
    // function exists to do exactly what that button does. The family is a party
    // of five running one dungeon at a time, so in practice the set is the one
    // map. The VERIFICATION is scoped to the map this run is for, because that
    // is the only one whose bind could send the party back into a cleared
    // instance; a bind elsewhere that survived is somebody else's problem to
    // notice.
    //
    // THE VERIFICATION IS THE POINT OF THE FUNCTION. `delivered` is not `done`,
    // and a void call that reports its failure to a client the bot does not have
    // is the exact shape this module keeps being caught by. So the bind is read
    // before and after: INSTANCE_RESET_ALL pushes a save onto `toUnbind` only
    // when the map is unloaded or empty (Group.cpp:2345-2352), so a leader that
    // is still bound afterwards is a reset that did not happen, whatever the
    // call appeared to do.
    //
    // WHY THE BIND MAP AND NOT THE `character_instance` TABLE, which is how #144
    // phrases the same check. They are the same fact with two different
    // latencies: PlayerUnbindInstance deletes the row through
    // CharacterDatabase.Execute (InstanceSaveMgr.cpp:871-876), which is queued
    // on the async worker, while the in-memory bind is gone the instant the call
    // returns. The in-memory map is also the one the SERVER consults when it
    // decides where the party lands (PlayerGetDestinationInstanceId reads
    // PlayerGetBoundInstance, InstanceSaveMgr.cpp:957-970), so it is the
    // authority rather than a proxy for one. The table catches up on its own and
    // is where an operator can confirm it afterwards.
    bool ResetGroupInstance(std::string const& leaderName,
                            std::vector<std::string> const& members,
                            uint32 insideMapId, std::string& why)
    {
        Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
        if (!SteerableAI(leader))
        {
            why = "the leader left the world between the check and the reset";
            return false;
        }

        Group* group = leader->GetGroup();
        if (!group)
        {
            why = "the leader left his group between the check and the reset";
            return false;
        }

        // PlayerGetBoundInstance  InstanceSaveMgr.h:207
        InstancePlayerBind* before = sInstanceSaveMgr->PlayerGetBoundInstance(
            leader->GetGUID(), insideMapId, DUNGEON_DIFFICULTY_NORMAL);
        if (!before || !before->save)
        {
            // NOTHING BOUND IS A SUCCESS, NOT A SKIP. The party is free to walk
            // into a fresh instance, which is the whole thing the reset is for.
            // This is the ordinary case for the very first run of a campaign on
            // a worldserver that has just started.
            LOG_INFO("module.overseer",
                     "overseer: '{}' is not bound to any instance of map {} - there is "
                     "nothing to reset and the party will be given a new one on entry",
                     leaderName, insideMapId);
            return true;
        }

        // GetInstanceId / GetCompletedEncounterMask  InstanceSaveMgr.h:62,72.
        // Both read BEFORE the reset because the save can be gone after it, and
        // the encounter mask is the number that made #144 necessary: 127 on map
        // 36 is all seven Deadmines encounters already dead.
        uint32 const instanceId = before->save->GetInstanceId();
        uint32 const encounters = before->save->GetCompletedEncounterMask();

        LOG_INFO("module.overseer",
                 "overseer: resetting map {} for '{}' - he is bound to instance {} with "
                 "completedEncounters = {}, which is what a rerun would otherwise walk "
                 "back into. Issuing the group reset a player's own 'reset all instances' "
                 "would issue",
                 insideMapId, leaderName, instanceId, encounters);

        // ResetInstances  Group.h:283  void ResetInstances(uint8 method, bool isRaid, Player* leader)
        // INSTANCE_RESET_ALL  Map.h:710
        group->ResetInstances(INSTANCE_RESET_ALL, false, leader);

        if (InstancePlayerBind* after = sInstanceSaveMgr->PlayerGetBoundInstance(
                leader->GetGUID(), insideMapId, DUNGEON_DIFFICULTY_NORMAL))
        {
            why = "'" + leaderName + "' is still bound to instance " +
                  std::to_string(after->save ? after->save->GetInstanceId() : 0) +
                  " on map " + std::to_string(insideMapId) +
                  " after the reset, so the instance was not empty when it was asked";
            return false;
        }

        // AND THE OTHER FOUR, WHICH IS A WARNING RATHER THAN A FAILURE.
        // UnbindAllFor covers everyone on the leader's save, so a member left
        // bound here is one bound to a DIFFERENT save of the same map - entered
        // separately at some point. That cannot send the party anywhere wrong
        // (the group leader's bind decides, InstanceSaveMgr.cpp:957) but it does
        // leave a character_instance row behind, and a row nobody expected is
        // worth a line rather than a silence.
        std::string stillBound;
        for (std::string const& name : members)
        {
            if (name == leaderName)
                continue;
            Player* member = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(member))
                continue;
            if (sInstanceSaveMgr->PlayerGetBoundInstance(
                    member->GetGUID(), insideMapId, DUNGEON_DIFFICULTY_NORMAL))
            {
                if (!stillBound.empty())
                    stillBound += ", ";
                stillBound += name;
            }
        }

        if (!stillBound.empty())
            LOG_WARN("module.overseer",
                     "overseer: map {} was reset for the group and these members are still "
                     "bound to some other save of it: {}. They will follow the leader into "
                     "his new instance regardless - a grouped character's destination comes "
                     "from the GROUP leader's bind - but their character_instance rows "
                     "outlived the reset",
                     insideMapId, stillBound);

        LOG_INFO("module.overseer",
                 "overseer: map {} reset - instance {} (completedEncounters = {}) is no "
                 "longer bound to '{}', verified by reading the bind back rather than by "
                 "the call returning. The next entry gets a fresh instance",
                 insideMapId, instanceId, encounters, leaderName);
        return true;
    }

    // The `members` column is a list a person reads, so it is joined the way a
    // person writes one. Roster names are ASCII and cannot contain a comma.
    static std::string JoinNames(std::vector<std::string> const& names)
    {
        std::string joined;
        for (std::string const& name : names)
        {
            if (!joined.empty())
                joined += ",";
            joined += name;
        }
        return joined;
    }

    // A RUN THAT NEVER STARTED IS STILL A RUN THAT HAPPENED (#144). Called only
    // from RESETTING, on either of its two ways of giving up - preconditions
    // that never came true, or the core refusing DUNGEON_RESET_ATTEMPTS times.
    //
    // It ends at IDLE rather than looping straight back into another reset, so
    // that the next attempt goes through the campaign checks at the top of this
    // drive like any other run. That is what makes the three-consecutive-
    // failures stop reachable at all: the failure is written down first and read
    // back as a row, not carried in a counter this process could lose.
    void FailRunAtReset(DungeonRunCoordinatorState& coord,
                        std::string const& leaderName,
                        std::vector<std::string> const& members,
                        DungeonPortal const& portal,
                        std::string const& reason)
    {
        LOG_ERROR("module.overseer",
                  "overseer: dungeon run {} of campaign {} could not start - {}. It counts "
                  "as a failed run: the instance still holds whatever was already dead in "
                  "it, so walking back in would be the empty dungeon this whole loop "
                  "exists to stop happening",
                  coord.runNumber, coord.campaignId, reason);

        RecordResetFailedRun(leaderName, portal.insideMapId, coord.campaignId,
                             coord.runNumber, reason, JoinNames(members));
        CountRunDone(leaderName);
        _travelAims.Release(leaderName);
        coord = DungeonRunCoordinatorState();
    }

    // A STAGING THAT WILL NOT FINISH IS CLOSED, NOT NARRATED (#165).
    //
    // WHAT WAS WATCHED. A run `active` and unstarted for forty-three minutes,
    // printing "BARRIER holds - ..." on every poll and doing nothing about it,
    // twice, both times ended by an operator. Narration is not a bound.
    //
    // WHY CLOSING IS THE RIGHT REACTION AND STANDING DOWN IS NOT. A closed run
    // is recoverable: the accounting has a row with a reason on it, the campaign
    // has counted an attempt, and the coordinator's own next poll opens a fresh
    // run and resets the instance before it aims anybody. A run left `active`
    // is not recoverable by anything - it holds the one-active-run-per-map key
    // that the next run needs, so nothing else can even begin.
    //
    // IT ENDS THROUGH EndRunAndDecide LIKE EVERY OTHER RUN, rather than through
    // a private teardown of its own, so the campaign counter, the aim release
    // and the go-again decision all happen exactly once and in the order that
    // function fixes. The outcome vocabulary gains 'staging_failed', which the
    // accounting column takes without a migration because it is a VARCHAR and
    // was made one for this reason. It is deliberately NOT 'reset_failed': the
    // reset worked, and the consecutive-failure stop that reads that value is
    // about an instance that will not clear, which this is not.
    //
    // AND A REPEAT TERMINATES RATHER THAN LOOPING, which is the question a
    // close-and-go-again always has to answer. Each closed run counts against
    // the campaign's own cap, so a staging that fails for a reason that keeps
    // being true cannot run forever. In the measured case it terminates much
    // sooner than that: the member who deadlocks the barrier by being inside
    // the instance is also the member who blocks the RESET the next run opens
    // with (DungeonResetBlockers refuses while anybody is standing in there),
    // so the next run fails at its reset, and three of those stop the campaign
    // outright with the ERROR that already exists for it.
    void FailStaging(DungeonRunCoordinatorState& coord, std::string const& leaderName,
                     DungeonPortal const& portal, char const* phase,
                     std::string const& blockers, bool stillWanted)
    {
        std::string const minutes =
            std::to_string(DUNGEON_STAGING_BACKSTOP_SECONDS / 60);
        LOG_ERROR("module.overseer",
                  "overseer: dungeon run {} of campaign {} cannot be staged - {} has held "
                  "for over {} minutes. Unsatisfied: {}. The run is CLOSED rather than "
                  "left 'active', because a closed run is recoverable - the next poll "
                  "opens a fresh one and resets the instance before it aims anybody - and "
                  "a run that sits 'active' forever holds the one-active-run-per-map key "
                  "the next one needs",
                  coord.runNumber, coord.campaignId, phase, minutes, blockers);
        EndRunAndDecide(coord, leaderName, portal,
                        coord.runId ? coord.runId : ActiveRunIdOnMap(portal.insideMapId),
                        "staging_failed",
                        std::string(phase) + " held for more than " + minutes +
                            " minutes and never opened - " + blockers,
                        stillWanted);
    }

    // THE END OF A RUN, AND THE DECISION TO GO AGAIN.
    //
    // Every path that used to say "the run is over, returning to IDLE" comes
    // here instead. Three things happen in a fixed order, and the order is the
    // content: the run row is CLOSED (before #144 nothing ever closed one
    // deliberately - CloseAbandonedRuns took RUN_COLD_SECONDS to notice, which
    // in a loop means the next run either collides with the last one's row or
    // silently joins it), the campaign counter is INCREMENTED, and only then is
    // the next run decided.
    //
    // WHAT "THE GOAL" IS TODAY, STATED PLAINLY. #144 asks the loop to go again
    // "when a run's goal is not met", and this module has no goal to test. A
    // run's real goal - every held quest for the instance complete - is #143 and
    // is not built; gear as a goal is #145; "until item X drops" is #150. So the
    // condition implemented here is the one #144 itself names as the for-now
    // stand-in: the run ended and the party is outside. Everything that reaches
    // this function satisfies that by construction, which is why the only
    // question left is the COUNT.
    void EndRunAndDecide(DungeonRunCoordinatorState& coord,
                         std::string const& leaderName,
                         DungeonPortal const& portal,
                         uint32 runId, char const* outcome,
                         std::string const& reason,
                         bool stillWanted)
    {
        CloseRun(runId, outcome, reason);
        CountRunDone(leaderName);
        _travelAims.Release(leaderName);

        uint32 const finished = coord.runNumber;
        uint32 const wanted = coord.runsWanted;
        bool const known = coord.capKnown;
        uint32 const campaignId = coord.campaignId;
        std::string const keyword = coord.portalKeyword;
        float const stageX = coord.stageX;
        float const stageY = coord.stageY;
        float const stageZ = coord.stageZ;

        // THE OPERATOR OUTRANKS THE COUNT. `job` leaving 'dungeon' is a person
        // (or the bridge) saying the family has something else to do, and it is
        // already the path by which a run inside the instance is walked back
        // out. Without this check the run that job change ENDED would be
        // followed by a cheerful "the run goes again", a reset, and a re-entry,
        // all undone one poll later by the stand-down at the top of this drive.
        // Correct either way, but a loop that has to be told twice is a loop
        // nobody would trust.
        if (!stillWanted)
        {
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} ended '{}' - {}. It does not go again: "
                     "'{}' no longer has job = 'dungeon', and the campaign counter does "
                     "not outrank the operator",
                     runId, outcome, reason, leaderName);
            coord = DungeonRunCoordinatorState();
            return;
        }

        if (!known)
        {
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} ended '{}' - {}. It does not repeat: this "
                     "database has no dungeon_runs_wanted column, so there is no cap to "
                     "bound a loop with and looping without one is the worse direction to "
                     "guess in", runId, outcome, reason);
            coord = DungeonRunCoordinatorState();
            return;
        }

        if (finished >= wanted)
        {
            LOG_INFO("module.overseer",
                     "overseer: dungeon run {} ended '{}' - {}. That was run {} of {} in "
                     "campaign {}, so the campaign is done and the coordinator returns to "
                     "IDLE. The richer stopping conditions - until every held quest is "
                     "complete (#143), until nobody has an upgrade left inside (#145), "
                     "until a named item drops (#150) - do not exist yet; this loop stops "
                     "on the count and on nothing else",
                     runId, outcome, reason, finished, wanted, campaignId);
            coord = DungeonRunCoordinatorState();
            return;
        }

        coord = DungeonRunCoordinatorState();
        coord.phase = DungeonRunPhase::Resetting;
        coord.portalKeyword = keyword;
        // THE STAGING POINT IS CARRIED, NOT RE-RESOLVED. Its own comment gives
        // the reason it is resolved once per run rather than once per poll: the
        // answer depends on whether the grid was loaded when it was asked, and a
        // barrier circle that moves under a party standing in it is a gather
        // that gets harder the closer it gets. Between two runs of one campaign
        // the party is standing on that exact point, so the same argument says
        // to keep it rather than ask again.
        coord.stageX = stageX;
        coord.stageY = stageY;
        coord.stageZ = stageZ;
        coord.campaignId = campaignId;
        coord.runNumber = finished + 1;
        coord.runsWanted = wanted;
        coord.capKnown = known;
        coord.resetSince = std::time(nullptr);

        LOG_INFO("module.overseer",
                 "overseer: dungeon run {} ended '{}' - {}. That was run {} of {} in "
                 "campaign {}, so the run goes again: RESET on map {} first, because the "
                 "instance they just left still has its bosses dead in it and re-entering "
                 "without a reset is a walk through an empty dungeon",
                 runId, outcome, reason, finished, wanted, campaignId,
                 portal.insideMapId);
    }

    void DriveDungeonRun()
    {
        // FIRST STATEMENT, BEFORE ANY `return` CAN HAPPEN (#122). Everything
        // this function escorts is marked as still wanted at the point it is
        // wanted, and this ends whatever the previous poll stopped marking -
        // including on the dozen paths out of this function that end a run and
        // could each have forgotten. An escort that outlives its run is a
        // follower that keeps `new rpg` and stops following, which is the
        // scatter this module has already fixed three times. See the escort
        // section beside CanBeSentToNpc.
        SweepDungeonEscorts();

        // `job` is read through the SAME guarded loader LoadJobs() already
        // gives DriveQuests, not inlined into this query - infra#2846: a
        // schema older than one of these columns must cost that column, not
        // the whole roster read. Inlining `job` here would null this entire
        // query - and therefore `name` and `lead` with it - on exactly the
        // schema this coordinator most needs to degrade gracefully on.
        std::map<std::string, std::string> const jobs = LoadJobs();

        QueryResult result = CharacterDatabase.Query(
            "SELECT name, `lead` FROM overseer_roster WHERE enabled = 1");
        if (!result)
            return;

        std::vector<std::string> members;
        std::string leaderName;
        do
        {
            Field* fields = result->Fetch();
            std::string const name = fields[0].Get<std::string>();
            bool const isLead = fields[1].Get<uint8>() != 0;
            members.push_back(name);
            if (isLead)
                leaderName = name;
        } while (result->NextRow());

        // No leader on the roster at all is a roster-shape problem this drive
        // cannot fix, and every step below needs one to aim.
        if (leaderName.empty())
            return;

        // LoadJobs() ONLY returns rows whose job is set and is NOT 'quest' -
        // see its own comment for why absence already means "quest, or the
        // column does not exist yet", which for THIS drive both mean
        // "nothing to gather toward".
        auto const jobIt = jobs.find(leaderName);
        std::string const leaderJob = jobIt == jobs.end() ? std::string("quest") : jobIt->second;

        DungeonRunCoordinatorState& coord = _dungeonRunCoordinator;

        if (coord.phase == DungeonRunPhase::Idle)
        {
            // job != 'dungeon' is the ordinary case - most polls, this drive
            // has nothing to do, same as every other job-gated drive in this
            // file when the roster is questing.
            if (leaderJob != "dungeon")
                return;

            Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
            PlayerbotAI* leaderAI = SteerableAI(leader);
            if (!leaderAI)
                return;  // mid-login/mid-teardown - try again next poll

            // ALREADY INSIDE - SO ADOPT THE RUN RATHER THAN STAND CLEAR OF IT.
            //
            // This used to return, on the reasoning that GATHERING/BARRIER is
            // for BEFORE entry and there was no staging left to do. That was
            // true and it left a hole the size of a worldserver restart. The
            // coordinator's state is in-process and unguarded on purpose (see
            // DungeonRunCoordinatorState), so a bounce puts it back at IDLE
            // while the party is still standing in the instance and the run row
            // is still open - and a bounce ALSO wipes the run's `enabled`
            // flag, which lives on the leader's value context in memory
            // (DungeonClearChatActions.cpp:281), along with this module's
            // memory of having issued `dc on` (_dcOnIssued). A coordinator
            // that returns here would then never watch the re-arming it is
            // supposed to watch, and never walk anybody out either. The
            // obligation to notice a restart would be unfulfilled by
            // construction.
            //
            // ADOPTION IS GATED ON KNOWING THE WAY OUT, which is what makes it
            // safe rather than a guess: only a dungeon this module has a portal
            // row for is adopted, and a portal row carries the exit trigger. A
            // leader standing in some other instance is a run this coordinator
            // could not finish, so it still stands clear of it and says so.
            if (InDungeonRun(leader))
            {
                // GetMapId  Position.h:281  uint32 GetMapId() const
                DungeonPortal const* inside =
                    FindDungeonPortalByInsideMap(leader->GetMapId());
                if (!inside)
                {
                    LOG_INFO("module.overseer",
                             "overseer: '{}' is in a dungeon run on map {}, which this "
                             "coordinator has no portal row for - it knows no way out of "
                             "there, so it stays out of the run rather than owning one it "
                             "could not finish",
                             leaderName, static_cast<uint32>(leader->GetMapId()));
                    return;
                }

                coord = DungeonRunCoordinatorState();
                coord.phase = DungeonRunPhase::Clearing;
                coord.portalKeyword = inside->keyword;
                coord.awaitingSince = std::time(nullptr);

                // AND THE CAMPAIGN THE ADOPTED RUN BELONGS TO, READ OFF ITS ROW
                // (#144). The coordinator's campaign numbers are in-process and
                // die with the worldserver; the run row's do not. Reading them
                // back is what stops a bounce mid-campaign from either losing
                // the run's number or inventing a rival campaign for it. A row
                // with campaign_id 0 is a run this coordinator never stamped -
                // somebody walked in by another path - and it stays 0, because
                // putting a number on a run nobody drove would be a claim about
                // a series that never happened.
                coord.runId = ActiveRunIdOnMap(inside->insideMapId);
                ReadRunCampaign(coord.runId, coord.campaignId, coord.runNumber);

                // The cap is re-read rather than inherited: the operator may
                // have changed it while the process was down, and the adopted
                // run's own end has to know whether to go again.
                DungeonCampaignCap const adoptedCap = LoadCampaignCap(leaderName);
                coord.capKnown = adoptedCap.known;
                coord.runsWanted = adoptedCap.wanted;

                // A RUN WITH NO NUMBER STILL HAS A PLACE IN THE SEQUENCE. An
                // unstamped row (campaign_id 0 - somebody walked in by a path
                // this coordinator did not drive) reads back as run 0, and
                // leaving it there would make the NEXT run number 1 while
                // dungeon_runs_done had already moved past 1. The counter on the
                // roster row is the durable one, so the number is taken from it:
                // whatever this run turns out to be, it is the one after the
                // ones already counted.
                if (!coord.runNumber)
                    coord.runNumber = adoptedCap.done + 1;

                LOG_INFO("module.overseer",
                         "overseer: '{}' is already inside a dungeon run on map {} - "
                         "adopting it at CLEARING (run {} of campaign {}). The staging "
                         "half is over and there is nothing to gather; what is left to own "
                         "is whether the dungeon brain is on, and the way back out",
                         leaderName, inside->insideMapId, coord.runNumber,
                         coord.campaignId);
                return;
            }

            // HOW MANY RUNS ARE LEFT, ASKED BEFORE ANYTHING ELSE IS DONE
            // (#144). Above the portal lookup and the staging-point derivation
            // on purpose: a finished campaign is the common case once one ends,
            // it is reached on every poll for as long as `job` stays 'dungeon',
            // and ResolveDungeonStagingPoint asks the MAP for ground height.
            // Doing that work to then decide there is no run is a map query
            // every five seconds forever.
            DungeonCampaignCap const cap = LoadCampaignCap(leaderName);
            if (cap.known && cap.done >= cap.wanted)
            {
                // SAID ONCE, NOT EVERY FIVE SECONDS. `job` stays 'dungeon' after
                // a campaign finishes - this module does not write that column
                // and will not decide for the operator that the family has
                // something else to do - so this branch is reached on every poll
                // until somebody changes one of the two numbers. The flag lives
                // on the idle coordinator, which nothing reassigns while it is
                // idle, and is cleared the moment the campaign has room again.
                if (!coord.loggedCampaignOver)
                {
                    coord.loggedCampaignOver = true;
                    LOG_INFO("module.overseer",
                             "overseer: the dungeon campaign for '{}' is over - {} of {} "
                             "runs done. Nothing further is started; raise "
                             "overseer_roster.dungeon_runs_wanted, or set "
                             "dungeon_runs_done back to 0, to run it again",
                             leaderName, cap.done, cap.wanted);
                }
                return;
            }

            DungeonPortal const* portal = FindDungeonPortal("deadmines");
            if (!portal)
                return;  // no known portal for this job yet - nothing to gather toward

            // THE STAGING POINT IS RESOLVED HERE AND NOWHERE ELSE (#121), from
            // the two areatriggers this portal already names. A run that cannot
            // work out where to wait does not start and says why, because the
            // alternative - staging at whatever a failed derivation left in
            // three floats - is how a party ends up walking at solid rock.
            float stageX = 0.f, stageY = 0.f, stageZ = 0.f;
            std::string why;
            if (!ResolveDungeonStagingPoint(*portal, leader, stageX, stageY, stageZ, why))
            {
                LOG_ERROR("module.overseer",
                          "overseer: dungeon run requested (job=dungeon on leader '{}') but "
                          "the '{}' staging point cannot be worked out - {}. Nothing is "
                          "aimed anywhere; fix the portal's areatrigger rows",
                          leaderName, portal->keyword, why);
                return;
            }

            // THERE IS NO `loggedCampaignOver = false` BETWEEN THE TWO STOP
            // CONDITIONS, AND ITS ABSENCE IS THE FIX. Clearing the flag here
            // re-armed the SECOND condition on every poll:
            // the campaign-over branch above did not fire (there is room in the
            // count), the flag was cleared, and the three-consecutive-failures
            // branch below then said its piece again five seconds later,
            // forever. Nothing needs to clear it: the flag lives on the idle
            // coordinator and the only thing that ever leaves idle is a run
            // starting, which assigns a fresh DungeonRunCoordinatorState over
            // the whole struct.

            // WHICH CAMPAIGN, AND WHICH RUN OF IT. `dungeon_runs_done == 0` is
            // the operator's gesture for "start a new campaign" (see
            // 2026_09_02_01's own comment), so it is what decides whether this
            // run joins the campaign the previous rows belong to or opens a new
            // one. Everything here is skipped when the run table cannot carry a
            // campaign, in which case the run happens exactly as it does today
            // and simply is not numbered.
            uint32 campaignId = 0;
            uint32 runNumber = cap.done + 1;
            if (RunAccountingPresent())
            {
                campaignId = cap.done == 0
                                 ? 0
                                 : CampaignInProgress(leaderName, portal->insideMapId);
                if (!campaignId)
                    campaignId = AllocateCampaignId();

                uint32 const failures = TrailingResetFailures(campaignId);
                if (failures >= DUNGEON_CAMPAIGN_CONSECUTIVE_FAILURES)
                {
                    if (!coord.loggedCampaignOver)
                    {
                        coord.loggedCampaignOver = true;
                        LOG_ERROR("module.overseer",
                                  "overseer: the dungeon campaign for '{}' is stopped - the "
                                  "last {} runs of campaign {} all ended 'reset_failed', so "
                                  "the instance still has every boss dead in it and a "
                                  "further run would walk into the same empty dungeon. The "
                                  "reasons are on those rows; set dungeon_runs_done to 0 "
                                  "once the cause is fixed",
                                  leaderName, failures, campaignId);
                    }
                    return;
                }
            }

            coord = DungeonRunCoordinatorState();
            // THE RUN STARTS AT THE RESET, NOT AT THE STAGING POINT (#144). The
            // staging aim is deliberately NOT claimed here any more: it is
            // claimed by RESETTING once the instance is actually clean, so that
            // a party which can never be given a fresh instance is never walked
            // to a door it should not go through. See the phase's own comment.
            coord.phase = DungeonRunPhase::Resetting;
            coord.portalKeyword = "deadmines";
            coord.stageX = stageX;
            coord.stageY = stageY;
            coord.stageZ = stageZ;
            coord.campaignId = campaignId;
            coord.runNumber = runNumber;
            coord.runsWanted = cap.wanted;
            coord.capKnown = cap.known;
            coord.resetSince = std::time(nullptr);

            LOG_INFO("module.overseer",
                     "overseer: dungeon run requested (job=dungeon on leader '{}') - run {} "
                     "of {} in campaign {}. RESET goes first: the instance the party is "
                     "bound to is reset before anybody is aimed at the door, because a "
                     "rerun into a saved instance is a walk through a dungeon where "
                     "everything is already dead. The staging point ({:.1f}, {:.1f}, "
                     "{:.1f}) is resolved and held for GATHERING",
                     leaderName, runNumber,
                     cap.known ? cap.wanted : 0, campaignId, stageX, stageY, stageZ);
            return;
        }

        // Both GATHERING and BARRIER need to know the portal being staged
        // for and whether the operator has since cancelled the run (job no
        // longer 'dungeon') or the run already opened by some other path.
        DungeonPortal const* portal = FindDungeonPortal(coord.portalKeyword);
        if (!portal)
        {
            // Should be unreachable - the keyword was just resolved above -
            // but a coordinator that trusts its own cached keyword forever is
            // exactly the "guard whose condition can never become true" shape
            // the run table's own migration warns against. Fail back to IDLE
            // rather than dereference nothing.
            LOG_WARN("module.overseer",
                     "overseer: dungeon run coordinator lost its portal ('{}') - "
                     "returning to IDLE", coord.portalKeyword);
            coord = DungeonRunCoordinatorState();
            return;
        }

        Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
        PlayerbotAI* leaderAI = SteerableAI(leader);

        // THE JOB LEAVING 'dungeon' IS ONE OF TWO WAYS A RUN ENDS, and it is
        // the only one a person controls.
        //
        // The other is the instance map holding no roster member any more,
        // handled further down beside the census - a wipe, a relog, a party
        // that left by some other path. Since #144 those two ends are the same
        // end: both go through EndRunAndDecide, which closes the run row, counts
        // it against the campaign, and decides whether to go again. The
        // difference is that this one says the operator no longer wants the
        // run at all, which outranks any count (see EndRunAndDecide's
        // `stillWanted`).
        //
        // The state machine in the epic has the run end when the dungeon is
        // CLEARED, and that fact is not available here. It was looked for
        // rather than assumed absent: InstanceScript exposes GetEncounterCount
        // (InstanceScript.h:278) and GetBossState (InstanceScript.h:252), which
        // would answer "is every encounter DONE" generically - but Deadmines'
        // own script does not use that framework at all. Its data types are
        // TYPE_RHAHK_ZOR and TYPE_CANNON (deadmines.h), door events kept in a
        // private `_encounters` array, and it never calls SetBossState. So
        // GetEncounterCount is zero for map 36 and "all encounters done" would
        // be vacuously TRUE the moment the party walked in. A signal that reads
        // as success before anything has happened is worse than no signal, so
        // an automatic CLEARING -> LOOT -> EXIT is deliberately not built here.
        //
        // What IS real is the operator, or the bridge, taking the job off
        // 'dungeon'. Until now that stood the coordinator down and left five
        // characters standing in an instance with every other drive stood down
        // on them - the stranding shape this epic exists to end, reached by the
        // one path that was supposed to end a run cleanly.
        if (leaderJob != "dungeon" && coord.phase != DungeonRunPhase::Exiting)
        {
            bool const inside = coord.phase == DungeonRunPhase::StagedInside ||
                                coord.phase == DungeonRunPhase::Clearing;
            if (inside)
            {
                coord.phase = DungeonRunPhase::Exiting;
                coord.crossing.best = 0.f;
                coord.crossing.since = std::time(nullptr);
                coord.loggedCrossingAim = false;
                coord.loggedCrossingWaiting = false;
                // The staging aim is released here rather than left to lapse:
                // EXIT writes its own aim at the exit trigger, and an errand
                // pointing at the entrance is one DriveTravel would refuse from
                // inside anyway (ResolveTravelTarget checks the map), releasing
                // it on a line that says "no such spawn" and reads as a fault.
                _travelAims.Release(leaderName);
                LOG_INFO("module.overseer",
                         "overseer: '{}' job left 'dungeon' while the party was inside - "
                         "the run is over, so EXIT walks them back out through the door "
                         "they came in by rather than leaving them in an instance with "
                         "every other drive stood down on them", leaderName);
                return;
            }

            // RESET, GATHERING, BARRIER or ENTER: the party is not staged
            // inside, so there is no crossing to reverse from here. RESET is on
            // that list since #144 and belongs there for the same reason as the
            // rest: it happens entirely outside the instance, so a stand-down
            // costs nothing but the wait. If somebody did get
            // through during an ENTER that is being stood down, IDLE adopts
            // that run on its next poll and EXIT becomes reachable again - the
            // adoption path exists precisely so this does not have to guess.
            LOG_INFO("module.overseer",
                     "overseer: '{}' job left 'dungeon' before the party was staged "
                     "inside - the dungeon run coordinator stands down and clears its "
                     "staging aim", leaderName);
            // AND THE `if (leaderAI)` THAT USED TO GATE THIS IS GONE. Releasing
            // an errand is a write to a roster row and an erase of this module's
            // own memory of it; neither needs the character to be in the world,
            // and the check below is the one that does. What the gate actually
            // did was leak the staging aim in the one case where nothing else
            // would ever clear it: the coordinator returns to IDLE on this line,
            // so a leader that is mid-login or logged out when its job leaves
            // 'dungeon' kept a live aim at a dungeon portal with no coordinator
            // left to own it. That is the "outlived its purpose" shape this
            // whole change is about.
            _travelAims.Release(leaderName);
            coord = DungeonRunCoordinatorState();
            return;
        }

        if (!leaderAI)
            return;  // mid-login/mid-teardown - hold phase, try again next poll

        // ONLY WHILE STAGING, AND THE SCOPE IS THE WHOLE POINT. Crossing the
        // portal is precisely what makes InDungeonRun true, so from ENTER
        // onward being in a run is this coordinator's OWN doing. Reading it as
        // "somebody beat me to it" there would tear the run down at the exact
        // moment it started working - the guard would fire on success. Before
        // ENTER it still means what it always meant: entry happened by some
        // other path (a human command, a different drive, a relog that landed
        // inside) and there is no gather left to own.
        if ((coord.phase == DungeonRunPhase::Resetting ||
             coord.phase == DungeonRunPhase::Gathering ||
             coord.phase == DungeonRunPhase::Barrier) && InDungeonRun(leader))
        {
            LOG_INFO("module.overseer",
                     "overseer: '{}' is already inside a dungeon run - the staging "
                     "coordinator had not finished BARRIER, but entry has already "
                     "happened by some other path; returning to IDLE", leaderName);
            // AND THE STAGING AIM GOES WITH THE COORDINATOR THAT OWNED IT. It
            // points at a spot on the OUTSIDE map, the leader is now on the
            // inside one, and nothing is left here to clear it. DriveTravel
            // would eventually release it on a line reading "there is no such
            // spawn on map {} - releasing the errand", which is true and reads
            // as a fault; worse, anything that puts a member back on the outside
            // map before that poll (a wipe releases to a graveyard out there)
            // finds a live aim walking it to the dungeon door. Released where
            // the reason is known. Same argument as the ENTER success path.
            _travelAims.Release(leaderName);
            coord = DungeonRunCoordinatorState();
            return;
        }

        // THE WHOLE-RUN STAGING CLOCK STARTS HERE (#165), on the first poll of
        // whichever of the three assembling phases this run reaches first.
        // Stamped lazily rather than at each phase transition because there are
        // six ways into them - a fresh campaign, a run that goes again, a
        // bounced worldserver adopting a run, a job change, a failed crossing,
        // an operator - and a clock that has to be started in six places is a
        // clock that is not started in one of them. Zero means "not staging",
        // and a run that leaves these phases zeroes it below.
        bool const assembling = coord.phase == DungeonRunPhase::Resetting ||
                                coord.phase == DungeonRunPhase::Gathering ||
                                coord.phase == DungeonRunPhase::Barrier;
        if (assembling && !coord.stagingSince)
            coord.stagingSince = std::time(nullptr);

        if (coord.phase == DungeonRunPhase::Resetting)
        {
            // WAITING AND FAILING ARE DIFFERENT THINGS, ON DIFFERENT CLOCKS.
            // A party that is still filing out of the door, or still finishing
            // a fight that followed them out, is a reset that is not READY -
            // nothing has gone wrong and the answer is to wait. A reset the
            // core has actually refused is a different fact and gets the much
            // smaller DUNGEON_RESET_ATTEMPTS. Conflating them would either give
            // up on a party that was about to be ready or hammer a refusal that
            // was never going to change.
            std::string const blockers =
                DungeonResetBlockers(members, leaderName, portal->insideMapId);
            if (!blockers.empty())
            {
                if (!coord.loggedResetWaiting)
                {
                    coord.loggedResetWaiting = true;
                    LOG_INFO("module.overseer",
                             "overseer: dungeon run {} holds at RESET - {}. The instance "
                             "cannot be reset while anybody is standing in it "
                             "(InstanceMap::Reset returns whether the map is empty), so "
                             "this waits rather than asking and being refused",
                             coord.runNumber, blockers);
                }

                if (coord.resetSince &&
                    std::time(nullptr) - coord.resetSince > DUNGEON_RESET_BACKSTOP_SECONDS)
                    FailRunAtReset(coord, leaderName, members, *portal,
                                   "the reset never became possible in " +
                                       std::to_string(DUNGEON_RESET_BACKSTOP_SECONDS / 60) +
                                       " minutes - " + blockers);
                return;
            }

            std::string why;
            if (ResetGroupInstance(leaderName, members, portal->insideMapId, why))
            {
                std::ostringstream aim;
                aim << "at:" << portal->outsideMapId << ':' << coord.stageX << ','
                    << coord.stageY << ',' << coord.stageZ;

                // CLAIM, not a raw UPDATE: the run supersedes whatever the
                // leader was doing, and taking the wheel also drops any errand
                // memory left over from the last time this exact staging point
                // was aimed at - which is the case DriveTravel's own
                // `target != target` reset cannot see, because the target is
                // the same string. Without that, a re-gather after a given-up
                // crossing inherits the failed crossing's backstop clock and is
                // released as unreachable on its first poll, having walked
                // nowhere. See TravelAimBook. In a LOOP that argument stops
                // being hypothetical: every run after the first aims at the
                // identical string the previous run just finished with.
                _travelAims.Claim(leaderName, aim.str());

                coord.phase = DungeonRunPhase::Gathering;
                coord.loggedGathering = false;
                LOG_INFO("module.overseer",
                         "overseer: dungeon run {} of {} - map {} is reset and '{}' is "
                         "aimed at the staging point ({}); GATHERING begins {:.0f}y back "
                         "down the approach corridor from areatrigger {}",
                         coord.runNumber, coord.capKnown ? coord.runsWanted : 0,
                         portal->insideMapId, leaderName, aim.str(),
                         DUNGEON_STAGING_STANDOFF_YARDS, portal->entryTriggerId);
                return;
            }

            ++coord.resetAttempts;
            LOG_WARN("module.overseer",
                     "overseer: dungeon run {} asked for the map {} reset and it did not "
                     "take ({} of {} attempts) - {}",
                     coord.runNumber, portal->insideMapId, coord.resetAttempts,
                     DUNGEON_RESET_ATTEMPTS, why);

            if (coord.resetAttempts >= DUNGEON_RESET_ATTEMPTS)
                FailRunAtReset(coord, leaderName, members, *portal, why);
            return;
        }

        if (coord.phase == DungeonRunPhase::Gathering)
        {
            if (!coord.loggedGathering)
            {
                coord.loggedGathering = true;
                // Still true as far as this coordinator is concerned: it
                // escorts nobody during GATHERING. A follower that drops more
                // than FOLLOW_CATCH_UP_YARDS behind on the way is picked up
                // by the follow drive's own catch-up (#138), which is a
                // property of following, not of the run.
                LOG_INFO("module.overseer",
                         "overseer: '{}' is walking to the staging point - followers "
                         "reach it by following, not by their own aim", leaderName);
            }

            // THE LEADER'S OWN FOCUS, ON THE RUN'S CLOCK (#164). GATHERING
            // escorts nobody and does not start to here - but the leader IS
            // travelling, under the staging aim this coordinator wrote, and the
            // death that made this rule was a leader on exactly that aim. While
            // nobody is escorted the travel poll is TRAVEL_POLL_MS (15 s); the
            // grant that killed him and the death were seconds apart. This
            // closes that window to DUNGEON_RUN_POLL_MS for the one character
            // the run is walking.
            AssertTravelFocus(leaderName);

            // AND GATHERING IS BOUNDED (#165). It had no bound of its own at
            // all: it returns and waits until the leader arrives, and if his
            // errand is released as unreachable by the travel backstop he never
            // will, so the run waits for an arrival nothing is any longer
            // working toward. Only the leader can hold this phase, so he is the
            // whole of the reason it gives.
            if (coord.stagingSince && std::time(nullptr) - coord.stagingSince >
                                          DUNGEON_STAGING_BACKSTOP_SECONDS)
            {
                std::string const where =
                    leader->GetMapId() != portal->outsideMapId
                        ? "on map " + std::to_string(uint32(leader->GetMapId())) +
                              " rather than map " + std::to_string(portal->outsideMapId)
                        : std::to_string(static_cast<uint32>(leader->GetDistance2d(
                              coord.stageX, coord.stageY))) +
                              "y from the staging point";
                FailStaging(coord, leaderName, *portal, "GATHERING",
                            leaderName + " (" + where + ")", leaderJob == "dungeon");
                return;
            }

            // GetMapId  Position.h:281  uint32 GetMapId() const
            // GetDistance2d  Object.h:538  float GetDistance2d(float x, float y) const
            if (leader->GetMapId() != portal->outsideMapId)
                return;  // still travelling, or on a different map entirely - keep waiting

            float const distance = leader->GetDistance2d(coord.stageX, coord.stageY);
            if (distance > DUNGEON_BARRIER_RADIUS_YARDS)
                return;

            coord.phase = DungeonRunPhase::Barrier;
            coord.loggedBarrierWaiting = false;
            // A fresh watchdog for a fresh barrier (#164): nothing measured
            // yet, nobody on the ladder.
            coord.staging.clear();
            LOG_INFO("module.overseer",
                     "overseer: '{}' reached the staging point ({:.0f}y out) - GATHERING "
                     "done, BARRIER holds until the whole roster is alive, out of combat "
                     "and within {:.0f}y",
                     leaderName, distance, DUNGEON_BARRIER_RADIUS_YARDS);
            return;
        }

        // ENTER, STAGED_INSIDE, CLEARING and EXIT all need the same two facts:
        // which door this run uses, and where it stands. Resolved once here
        // rather than in four places, because a run that could resolve its door
        // for one state and not another would be four bugs waiting.
        if (coord.phase == DungeonRunPhase::Enter ||
            coord.phase == DungeonRunPhase::StagedInside ||
            coord.phase == DungeonRunPhase::Clearing ||
            coord.phase == DungeonRunPhase::Exiting)
        {
            bool const leaving = coord.phase == DungeonRunPhase::Exiting;
            uint32 const triggerId =
                leaving ? portal->exitTriggerId : portal->entryTriggerId;

            // THE DOOR'S OWN COORDINATES, READ BACK OUT OF THE TRIGGER TABLE
            // and not carried in DungeonPortal next to them - see that struct's
            // comment. Read on every poll rather than cached: it is a hash
            // lookup in a store loaded once at startup, and a cached copy is
            // one more thing that can be stale.
            // GetAreaTrigger  ObjectMgr.h:868  AreaTrigger const* GetAreaTrigger(uint32) const
            AreaTrigger const* door = sObjectMgr->GetAreaTrigger(triggerId);
            if (!door)
            {
                // SAID AND ENDED, NOT SKIPPED. A missing trigger is not a
                // condition that becomes true later - this world's tables do
                // not have the door - and holding a party for a door that does
                // not exist is the "guard whose condition can never change"
                // shape this file has now paid for three times.
                LOG_ERROR("module.overseer",
                          "overseer: dungeon run wanted areatrigger {} and this world's "
                          "tables do not have it - there is no door to walk through, so the "
                          "run is given up rather than held forever", triggerId);
                _travelAims.Release(leaderName);
                coord = DungeonRunCoordinatorState();
                return;
            }

            if (coord.phase == DungeonRunPhase::Enter)
            {
                switch (DriveDungeonCrossing(members, leaderName, door, triggerId,
                                             portal->insideMapId, "ENTER", coord))
                {
                    case DungeonCrossingResult::Through:
                        coord.phase = DungeonRunPhase::StagedInside;
                        coord.loggedStagedWaiting = false;
                        // THE DOOR AIM DIES WITH THE CROSSING IT WAS FOR, and
                        // this release is new. ENTER aims the leader at the
                        // entrance trigger's own coordinates, which are on the
                        // OUTSIDE map; the party is now on the inside one. Left
                        // standing, DriveTravel refuses it on its next poll -
                        // "there is no such spawn on map {}" - which is true and
                        // reads as a fault rather than as the aim having simply
                        // done its job. And in the window before that poll, any
                        // member put back outside (a wipe releases to a
                        // graveyard on the outside map) is under a live aim
                        // walking it back to the dungeon door. That is the
                        // staging aim outliving its purpose, which is what this
                        // column's two writers used to do to each other.
                        _travelAims.Release(leaderName);
                        LOG_INFO("module.overseer",
                                 "overseer: all {} roster members went through areatrigger "
                                 "{} and are on map {} - ENTER done, STAGED_INSIDE holds",
                                 static_cast<uint32>(members.size()), triggerId,
                                 portal->insideMapId);
                        return;

                    case DungeonCrossingResult::GaveUp:
                        // Back to IDLE rather than to BARRIER. Anyone who did
                        // get through is inside a real run, and the run record
                        // plus the clearing drive own them from there; IDLE
                        // will adopt that run on its next poll rather than
                        // trying to re-gather a party that is no longer in one
                        // place. The crossing itself already said who was left.
                        _travelAims.Release(leaderName);
                        coord = DungeonRunCoordinatorState();
                        return;

                    case DungeonCrossingResult::Working:
                        return;
                }
                return;
            }

            if (coord.phase == DungeonRunPhase::Exiting)
            {
                switch (DriveDungeonCrossing(members, leaderName, door, triggerId,
                                             portal->outsideMapId, "EXIT", coord))
                {
                    case DungeonCrossingResult::Through:
                        LOG_INFO("module.overseer",
                                 "overseer: all {} roster members came back out through "
                                 "areatrigger {} and are on map {} - the run is over",
                                 static_cast<uint32>(members.size()), triggerId,
                                 portal->outsideMapId);
                        // THE ONE PRECONDITION A RESET CANNOT BE GIVEN BY
                        // ANYTHING ELSE (#144): every member out of the
                        // instance. EXIT is what establishes it, and this is
                        // the line where it becomes true - so the decision to
                        // go again is taken here rather than a phase later.
                        {
                            // COPIED, NOT REFERENCED. EndRunAndDecide resets
                            // `coord` at the end of every branch, and a reason
                            // that lived on `coord` would be destroyed under a
                            // reference the function still holds.
                            std::string const reason =
                                coord.stalledReason.empty()
                                    ? ("the party walked back out through areatrigger " +
                                       std::to_string(triggerId))
                                    : coord.stalledReason;
                            // AND THE ROW SAYS WHAT ACTUALLY HAPPENED (#171).
                            // 'stalled' was named and deliberately NOT written
                            // by the accounting migration, because this module
                            // could not prove it then. It can now: no boss
                            // credit, no movement, nobody busy, and every skip
                            // spent. The column is a VARCHAR for exactly this.
                            EndRunAndDecide(coord, leaderName, *portal,
                                            coord.runId
                                                ? coord.runId
                                                : ActiveRunIdOnMap(portal->insideMapId),
                                            coord.stalledReason.empty() ? "left" : "stalled",
                                            reason, leaderJob == "dungeon");
                        }
                        return;

                    case DungeonCrossingResult::GaveUp:
                        // THE PARTY IS STILL INSIDE AND THE COORDINATOR HAS
                        // STOPPED TRYING, which is worth an ERROR rather than
                        // the WARN the crossing already printed: an EXIT that
                        // cannot finish leaves characters in an instance with
                        // every other drive stood down on them, and that is the
                        // stranding shape this whole epic exists to end. The
                        // run row's own cold-heartbeat close is the only thing
                        // left, and it takes RUN_COLD_SECONDS to notice.
                        //
                        // THE FIRST THING TO SUSPECT IF THIS FIRES IS THE
                        // ARMED DUNGEON BRAIN, and it is named here so nobody
                        // has to rediscover it. While `dungeon clear` is held,
                        // mod-dungeon-clear is steering these characters too,
                        // and it has no idea the run is over - so it can walk
                        // them back toward the instance while the travel aim
                        // walks them toward the door. Disarming from here is
                        // NOT the fix and was rejected on purpose. When this
                        // was written the arming drive re-issued to any
                        // character whose strategy was missing, so a `dc off`
                        // from here was undone within seconds; since #140 it
                        // issues once per stay inside and would not, but a
                        // `dc off` asserted from out here is still a second
                        // opinion on a lifecycle the module's own `dc` verbs
                        // own. The real fix is for that drive to learn that a
                        // run can be over, which is a change to its lifecycle
                        // and belongs with those verbs.
                        LOG_ERROR("module.overseer",
                                  "overseer: dungeon run EXIT could not get the party back "
                                  "out through areatrigger {} - they are still on map {} "
                                  "with every other drive stood down on them. The "
                                  "coordinator returns to IDLE and will adopt the run again "
                                  "next poll; if this repeats, the party cannot reach its "
                                  "own exit", triggerId, portal->insideMapId);
                        _travelAims.Release(leaderName);
                        coord = DungeonRunCoordinatorState();
                        return;

                    case DungeonCrossingResult::Working:
                        return;
                }
                return;
            }

            // DungeonRunPhase::StagedInside and DungeonRunPhase::Clearing, both
            // of which are about who is inside rather than about a door.
            uint32 inside = 0;
            std::vector<OverseerDecisions::DungeonRunEntryState> const states =
                DungeonRunCensus(members, door, portal->insideMapId, inside);

            // NOBODY LEFT INSIDE MEANS THE RUN IS OVER, however it ended - a
            // wipe that released everyone to a graveyard, a party that walked
            // back out, a worldserver bounce. The coordinator does not try to
            // tell those apart, because it does not act differently on any of
            // them: IDLE re-gathers on the next poll if the job is still
            // 'dungeon', and WIPE_RECOVER is a later slice.
            if (!inside)
            {
                // HOWEVER IT ENDED - AND #144 MAKES THAT WORTH ASKING ABOUT.
                // The coordinator still does not act differently on a wipe than
                // on a relog: both end the run and both are followed by a reset
                // and another run. What changed is that the run now gets an
                // OUTCOME, and "the party all died" and "somebody logged out"
                // are different enough that recording them as the same thing
                // would make the run table useless for the question #151 will
                // ask of it. The test is over overseer_death rather than over
                // who happens to be a ghost this poll - see RunEndedInAWipe.
                uint32 const runId = coord.runId
                                         ? coord.runId
                                         : ActiveRunIdOnMap(portal->insideMapId);
                bool const wiped =
                    RunEndedInAWipe(runId, portal->insideMapId, coord.wentIn);
                LOG_INFO("module.overseer",
                         "overseer: the dungeon run holds nobody any more - not one roster "
                         "member is on map {}, so the run is over however it ended",
                         portal->insideMapId);
                EndRunAndDecide(coord, leaderName, *portal, runId,
                                wiped ? "wipe" : "emptied",
                                wiped
                                    ? "every member who went in died on map " +
                                          std::to_string(portal->insideMapId) +
                                          " during this run"
                                    : "the map emptied without the coordinator walking "
                                      "them out",
                                leaderJob == "dungeon");
                return;
            }

            // HOW MANY WENT IN, RATCHETED RATHER THAN SAMPLED. This is only ever
            // read by the wipe test at the end of the run, and what that test
            // needs is the size of the party that was in there - not the size of
            // whatever is left standing when the map finally empties, which is
            // zero by definition. A high-water mark is the honest reading, and
            // it also covers a run ADOPTED after a bounce, which never passes
            // through the ENTER crossing that would otherwise have counted.
            if (inside > coord.wentIn)
                coord.wentIn = inside;

            if (coord.phase == DungeonRunPhase::StagedInside)
            {
                if (!OverseerDecisions::DungeonRunAllThrough(states))
                {
                    // SOME IN, SOME OUT. Said out loud once rather than held
                    // silently: a party split across a portal is the exact
                    // state the barrier exists to prevent, and it having
                    // happened AFTER entry is a fact worth reading in a log.
                    if (!coord.loggedStagedWaiting)
                    {
                        coord.loggedStagedWaiting = true;
                        LOG_WARN("module.overseer",
                                 "overseer: dungeon run is staged inside map {} but the "
                                 "party is split - {} of {} are in there. Still outside: {}",
                                 portal->insideMapId, inside,
                                 static_cast<uint32>(states.size()),
                                 OverseerDecisions::DungeonRunEntryBlockers(
                                     states, DUNGEON_DOORSTEP_RADIUS_YARDS));
                    }
                    return;
                }

                coord.phase = DungeonRunPhase::Clearing;
                coord.awaitingSince = std::time(nullptr);
                coord.anchorSet = false;
                coord.loggedAccepted = false;
                coord.loggedNotAccepted = false;
                coord.loggedMoved = false;
                coord.loggedNotMoved = false;

                // THE RUN JOINS ITS CAMPAIGN HERE, AND HERE IS THE FIRST MOMENT
                // IT COULD (#144). The run ROW is opened by the arming drive,
                // which is the thing that notices somebody is on the instance
                // map - so it exists by now and not before, and this is the
                // earliest point at which the coordinator can put its own
                // numbers on it. `AND campaign_id = 0` inside StampRunIntoCampaign
                // is what keeps this idempotent against a re-entry to this
                // transition and against a run some other process already
                // numbered.
                coord.runId = ActiveRunIdOnMap(portal->insideMapId);
                StampRunIntoCampaign(coord.runId, coord.campaignId, coord.runNumber,
                                     JoinNames(members));
                // THE LINE THIS WHOLE EPIC WAS OPENED TO MAKE POSSIBLE, so it
                // says what is now true rather than what was attempted.
                LOG_INFO("module.overseer",
                         "overseer: dungeon run STAGED_INSIDE satisfied - all {} roster "
                         "members are on map {} together. CLEARING watches that the dungeon "
                         "brain actually comes on",
                         static_cast<uint32>(states.size()), portal->insideMapId);
                return;
            }

            // DungeonRunPhase::Clearing
            //
            // THIS STATE ARMS NOTHING. DriveDungeonClear already owns arming
            // and is the only thing that may - see this section's own comment
            // and that drive's. What this adds is a RUN-LEVEL VERDICT on
            // whether the arming worked, and every signal that looked like one
            // has now been wrong once:
            //
            //   - the command surface reports `delivered`, which means the bot
            //     received the whisper. `dc on` outside an instance is refused
            //     and still reports delivered with a null result.
            //   - a strategy probe showing "dungeon clear" was read as
            //     confirmation while every command was in fact being refused
            //     for an unauthorized issuer (#96).
            //   - this state then read the live strategy list off each engine
            //     and called the run "verified" when all five held it (#140).
            //     The dungeon module's gate installs that strategy on every
            //     bot standing on a dungeon map, asked or not
            //     (DcStrategyGate.cpp:60-112), so the line was always true and
            //     never evidence: it was logged at 08:49 over a party that
            //     stood at the door until 15:00.
            //
            // WHAT IS READ INSTEAD, IN TWO STEPS, EACH CLAIMING ONLY WHAT IT
            // CAN. First, this module's own record of what DcOnAction
            // RETURNED (_dcOnIssued, written by the drive): true for everyone
            // inside means the command was accepted on whichever of them the
            // module elected leader, and that is the whole claim - "accepted",
            // not "driving". Second, the leader's POSITION: the module's
            // triggers read the `enabled` flag that `dc on` set and walk the
            // tank toward the first boss, and a tank that has moved more than
            // DUNGEON_DC_ON_MOVE_YARDS from where it stood when the command
            // was accepted is a run the module is driving, by the only measure
            // this module can take without the module's headers. The flag
            // itself, and the `dc status` reply that would report it, are out
            // of reach: the reply is an addon-channel packet sent only to real
            // clients in the group (DcStatusPublisher::SendAddonMessage,
            // DcStatusPublisher.cpp:186-201), not party chat.
            //
            // A BOUNCE IS NOTICED BY CONSTRUCTION rather than by a separate
            // "armed, then not" branch. What a worldserver restart wipes is
            // the `enabled` flag, the drive's record and this state, all
            // together; the adopted run starts at CLEARING with an empty
            // record, the drive issues again, and this verifies again. There
            // is no in-process event that turns the flag off while everyone
            // stays inside, so there is nothing for such a branch to read.
            uint32 const runId = ActiveRunIdOnMap(portal->insideMapId);
            uint32 accepted = 0;
            std::string notAccepted;
            for (OverseerDecisions::DungeonRunEntryState const& state : states)
            {
                if (!state.through)
                    continue;   // outside: the drive only issues to a character inside

                if (runId && DcOnAccepted(state.name, runId))
                {
                    ++accepted;
                    continue;
                }

                if (!notAccepted.empty())
                    notAccepted += ", ";
                notAccepted += state.name;
            }

            if (!accepted || accepted != inside)
            {
                // NOT (OR NO LONGER) ACCEPTED ON EVERYONE INSIDE. If it was a
                // moment ago, the clocks restart: the drive re-issues on its
                // next pass and the grace is measured from now, not from an
                // entry that may be an hour old.
                if (coord.loggedAccepted)
                {
                    coord.loggedAccepted = false;
                    coord.anchorSet = false;
                    coord.loggedMoved = false;
                    coord.loggedNotMoved = false;
                    coord.awaitingSince = std::time(nullptr);
                }

                // NEVER ACCEPTED, AND THE GRACE HAS LAPSED. ERROR rather than
                // WARN: a run whose brain is off is the single most
                // consequential state this module can be in, it has been in
                // it for its entire life, and the only reason that survived so
                // long is that nothing ever said so. Said once per lapse
                // rather than every poll; acceptance clears the flag, so a
                // line here means it is still true.
                if (coord.awaitingSince &&
                    std::time(nullptr) - coord.awaitingSince > DUNGEON_ARMING_GRACE_SECONDS &&
                    !coord.loggedNotAccepted)
                {
                    coord.loggedNotAccepted = true;
                    LOG_ERROR("module.overseer",
                              "overseer: dungeon run CLEARING has 'dc on' accepted for {} of "
                              "{} inside map {} after {}s - the dungeon brain is OFF for the "
                              "rest and they are fighting a five-man with open-world logic. "
                              "The clearing drive's own log says whether it was refused or "
                              "never issued. Not accepted: {}",
                              accepted, inside, portal->insideMapId,
                              static_cast<uint32>(DUNGEON_ARMING_GRACE_SECONDS), notAccepted);
                }
                return;
            }

            // ACCEPTED ON EVERYONE INSIDE. Said once, claiming only that.
            if (!coord.loggedAccepted)
            {
                coord.loggedAccepted = true;
                coord.loggedNotAccepted = false;
                LOG_INFO("module.overseer",
                         "overseer: dungeon run CLEARING - 'dc on' accepted for all {} "
                         "roster members inside map {}, read off what DcOnAction returned "
                         "rather than off a strategy list. That is the command taking, not "
                         "the run moving; watching for '{}' to move more than {:.0f}y "
                         "within {}s",
                         accepted, portal->insideMapId, leaderName,
                         DUNGEON_DC_ON_MOVE_YARDS,
                         static_cast<uint32>(DUNGEON_DC_ON_MOVE_WINDOW_SECONDS));
            }

            // THE MOVEMENT VERDICT, ON THE LEADER. Measured from where it
            // stood when acceptance was first seen complete rather than from
            // the door, because this module does not know where the interior
            // is - the first boss is the dungeon module's knowledge - and the
            // issue's own acceptance criterion is stated the same way. Only a
            // leader this module can see is measured; mid-relog it is skipped
            // for a poll and the anchor waits.
            Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
            if (!leader || leader->GetMapId() != portal->insideMapId)
                return;

            // HAS THE RUN ACTUALLY GONE ANYWHERE (#171)? Asked BEFORE the
            // arming verdict below, because that verdict answers itself once
            // ("CLEARING verified") and then returns on every later poll - so
            // anything placed after it would never run again for the rest of
            // the run, which is precisely the window a twenty-six minute freeze
            // lives in.
            if (RunClearingWatchdog(coord, leader, states, portal->insideMapId))
                return;   // the run is on its way out; the phase has changed

            if (!coord.anchorSet)
            {
                coord.anchorSet = true;
                coord.anchorX = leader->GetPositionX();
                coord.anchorY = leader->GetPositionY();
                coord.anchorAt = std::time(nullptr);
                return;
            }

            if (coord.loggedMoved)
                return;   // verified; nothing further to say until something changes

            // Position::GetExactDist2d (Position.h:170) and not
            // WorldObject::GetDistance2d, for the reason the quest re-pick
            // ratchet gives: the latter subtracts the character's own size,
            // and this is a question about ground covered.
            float const moved = leader->GetExactDist2d(coord.anchorX, coord.anchorY);
            time_t const waited = std::time(nullptr) - coord.anchorAt;
            if (moved > DUNGEON_DC_ON_MOVE_YARDS)
            {
                coord.loggedMoved = true;
                coord.loggedNotMoved = false;
                // THE LINE THIS EPIC WAS OPENED TO MAKE TRUE, and the first
                // version of it that is evidence.
                LOG_INFO("module.overseer",
                         "overseer: dungeon run CLEARING verified - '{}' has moved {:.0f}y "
                         "from where it stood when 'dc on' was accepted, {}s later. The "
                         "dungeon module is driving the run",
                         leaderName, moved, static_cast<uint32>(waited));
                return;
            }

            if (waited > DUNGEON_DC_ON_MOVE_WINDOW_SECONDS && !coord.loggedNotMoved)
            {
                coord.loggedNotMoved = true;
                // ERROR, ONCE. This is the shape #140 was opened on: command
                // accepted, strategy held, party at the door for six hours.
                // Not retried from here - a second `dc on` on an enabled run
                // resets it (see DUNGEON_DC_ON_RETRY_SECONDS) - and the why
                // is in the dungeon module's own log, which is where the
                // stall reasons it publishes end up.
                LOG_ERROR("module.overseer",
                          "overseer: dungeon run CLEARING - 'dc on' was accepted but '{}' "
                          "has moved only {:.0f}y in {}s (needed more than {:.0f}y within "
                          "{}s). The command took and the run is not moving; the dungeon "
                          "module's own log has its stall reason, if it published one",
                          leaderName, moved, static_cast<uint32>(waited),
                          DUNGEON_DC_ON_MOVE_YARDS,
                          static_cast<uint32>(DUNGEON_DC_ON_MOVE_WINDOW_SECONDS));
            }
            return;
        }

        // DungeonRunPhase::Barrier
        //
        // THE WHOLE ROSTER IS RE-CHECKED HERE, NOT JUST THE MEMBERS DriveTravel
        // MOVED. The barrier is about who would walk into the instance
        // together, and a follower reaches the staging point by FOLLOWING the
        // leader (see the section comment above) - so a follower's own
        // aliveness, combat state and distance are facts only this poll can
        // measure, not facts the GATHERING phase already established for it.
        //
        // AND A MEMBER THAT FOLLOWING HAS NOT DELIVERED IS ESCORTED (#122, and
        // the wound #70 measured). GATHERING deliberately escorts nobody: the
        // leader is walking and the party's job is to follow him, which is what
        // following is for and what it does well. BARRIER is the other case -
        // the leader has arrived and stopped, so a member still short of the
        // staging point is not "behind the leader", it is a follower that came
        // off its path and will not close the gap. Measured live: leader inside,
        // one member 27 yards out, the rest 220, 450 and 745 yards back, and
        // none of them moving. The escort walks it to the point the party is
        // already standing on, which is the only direction it can take a
        // character - toward the family, never away from it.
        //
        // The barrier radius is the cut-off rather than the arrival radius on
        // purpose: at 10 yards the leader is right there, so `follow` picks the
        // member up as the escort hands back, and the escort never has to end on
        // an arrival (which would idle it, see DriveTravel's escort branch).
        std::ostringstream stageAim;
        stageAim << "at:" << portal->outsideMapId << ':' << coord.stageX << ','
                 << coord.stageY << ',' << coord.stageZ;
        std::string const stageTarget = stageAim.str();

        std::vector<OverseerDecisions::DungeonRunMemberState> states;
        states.reserve(members.size());
        for (std::string const& name : members)
        {
            OverseerDecisions::DungeonRunMemberState state;
            state.name = name;

            // SteerableAI, not a bare lookup - the same reason every other
            // drive in this file uses it: a name can resolve to a Player
            // mid-login or mid-teardown. A member this poll cannot even find
            // is exactly the "not seen" case the predicate fails closed on.
            Player* member = ObjectAccessor::FindPlayerByName(name);
            if (!SteerableAI(member))
            {
                states.push_back(state);
                continue;
            }

            state.seen = true;
            // IsAlive  Unit.h:1793  bool IsAlive() const
            state.alive = member->IsAlive();
            // IsInCombat  Unit.h:936  bool IsInCombat() const
            state.inCombat = member->IsInCombat();
            // ALREADY THROUGH THE DOOR (#165). Recorded for the blockers line
            // and for the give-up below, and for nothing else: the predicate
            // still counts this member as not staged, because whether being
            // ahead should satisfy a barrier is that issue's own question. What
            // this fixes today is a run that spent forty-three minutes saying
            // "not seen" about somebody standing thirty yards away through a
            // wall.
            state.inside = member->GetMapId() == portal->insideMapId;
            // GetMapId  Position.h:281  uint32 GetMapId() const
            if (member->GetMapId() == portal->outsideMapId)
                // GetDistance2d  Object.h:538  float GetDistance2d(float x, float y) const
                state.distanceFromStage = member->GetDistance2d(coord.stageX, coord.stageY);

            // A member on some other map carries a negative distance and cannot
            // be walked here by an `at:` aim at all - ResolveTravelTarget
            // refuses one whose map is not the character's own - so it is left
            // to the blockers line rather than given an errand that could only
            // be refused.
            //
            // THE LEADER IS ESCORTED AT ANY DISTANCE, INCLUDING NONE, and that
            // is not a no-op. BARRIER opens at DUNGEON_BARRIER_RADIUS_YARDS (10)
            // while DriveTravel calls an `at:` errand done at
            // TRAVEL_ARRIVED_POSITION_YARDS (5), so without this the leader
            // arrives, his errand is released, his own `new rpg` goes idle and
            // NewRpgStatusUpdateAction sends him off to grind or quest while the
            // barrier is still waiting for the stragglers he is the anchor for.
            // Escorting him holds him on the spot instead (see DriveTravel's
            // escort arrival branch). It claims the identical aim string
            // GATHERING already wrote, so TravelAimBook::Claim is idempotent
            // against the walk still in flight, and it grants him nothing - he
            // carries `new rpg` in his own right and keeps it.
            //
            // A follower is escorted only while it is genuinely short of the
            // staging point. Inside the radius the leader is right there, so
            // `follow` is both sufficient and preferable.
            bool const isLeader = name == leaderName;
            if (state.distanceFromStage >= 0.f &&
                (isLeader || state.distanceFromStage > DUNGEON_BARRIER_RADIUS_YARDS))
                EscortToward(name, stageTarget, "BARRIER");

            // AND THE ESCORT RE-ASSERTS ITSELF ON THE RUN'S OWN CLOCK (#164).
            // DriveTravel asserts the focus on every travel poll already; this
            // is the same call on DUNGEON_RUN_POLL_MS instead, because a
            // staging window is exactly when something else granting a
            // movement strategy is most expensive and the measured window
            // between such a grant and a death was seconds. A no-op for a
            // member the travel drive has not focused yet.
            AssertTravelFocus(name);

            // IS IT ACTUALLY COMING? The reading above is a distance, and a
            // distance alone cannot tell a member walking in from a member
            // standing still. This can, and corrects what it finds.
            RunStagingWatchdog(coord, name, member, state);

            states.push_back(state);
        }

        if (OverseerDecisions::DungeonRunBarrierMet(states, DUNGEON_BARRIER_RADIUS_YARDS))
        {
            // THE BARRIER IS A ONE-WAY DOOR INTO ENTER, not a condition ENTER
            // keeps re-asking. Once the party is gathered, the next thing that
            // happens to it is walking twenty yards onto a portal - and by the
            // time the first member is through, "all five within 10y of the
            // staging point" is false BY SUCCEEDING. A barrier re-checked after
            // it is met is a barrier that can never be passed.
            coord.phase = DungeonRunPhase::Enter;
            // ...and the whole-run staging clock stops with it (#165). ENTER has
            // a bound of its own, DUNGEON_CROSSING_BACKSTOP_SECONDS, and two
            // clocks on one phase is two answers to the same question.
            coord.stagingSince = 0;
            // BARRIER is over, so its watchdog is too - the staging point
            // stops being the thing anybody is measured against the moment
            // the party starts walking through the door (#164).
            coord.staging.clear();
            coord.crossing.best = 0.f;
            coord.crossing.since = std::time(nullptr);
            coord.loggedCrossingAim = false;
            coord.loggedCrossingWaiting = false;
            LOG_INFO("module.overseer",
                     "overseer: dungeon run BARRIER satisfied - all {} roster members "
                     "alive, out of combat and within {:.0f}y of the staging point. ENTER "
                     "takes them the last {}y to areatrigger {} and knocks for all of "
                     "them at once, followers first and the leader last",
                     static_cast<uint32>(states.size()), DUNGEON_BARRIER_RADIUS_YARDS,
                     static_cast<uint32>(DUNGEON_STAGING_STANDOFF_YARDS),
                     portal->entryTriggerId);
            return;
        }

        // THE WHOLE-RUN STAGING BACKSTOP (#165), asked AFTER the barrier has
        // had this poll's chance to open, so a party that assembles on the very
        // poll the clock runs out is let through rather than written off.
        //
        // THIS IS THE SECOND HALF OF THE ESCALATION AND NOT A RIVAL TO IT. The
        // per-character ladder above has, by the time this can fire, tried every
        // correction on every member that is merely slow, twice over, and said
        // so each time. What is left when it has not worked is a member nothing
        // this module can do reaches - one that is already through the door
        // being the measured case - and the honest answer to that is to say who
        // and why and close the run, not to keep printing the same line.
        if (coord.stagingSince &&
            std::time(nullptr) - coord.stagingSince > DUNGEON_STAGING_BACKSTOP_SECONDS)
        {
            FailStaging(coord, leaderName, *portal, "BARRIER",
                        OverseerDecisions::DungeonRunBarrierBlockers(
                            states, DUNGEON_BARRIER_RADIUS_YARDS),
                        leaderJob == "dungeon");
            return;
        }

        if (!coord.loggedBarrierWaiting)
        {
            coord.loggedBarrierWaiting = true;
            LOG_INFO("module.overseer",
                     "overseer: dungeon run BARRIER holds - {}",
                     OverseerDecisions::DungeonRunBarrierBlockers(
                         states, DUNGEON_BARRIER_RADIUS_YARDS));
        }
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

    // THIS REALM SAYS WHAT IT IS AND WHAT IT IS RUNNING (mod-overseer#184).
    //
    // WHY THE REALM WRITES THIS RATHER THAN ANYTHING ELSE READING IT. Three
    // realms run this module and they do not run the same build of it - as this
    // is written, the live one is on an AzerothCore twelve days older than the
    // other two, and the only place that is recorded is a line in a container
    // log. Which realm a reader is looking at has so far been carried by the
    // hostname the page came from, and that distinction is being retired. Once
    // it is gone, an unlabelled page is one accident away from showing the live
    // family's positions under a promise that it is not the live family.
    //
    // The site is a passive reader: it opens a database and renders what it
    // finds. So the label has to be IN the database, next to the data it
    // labels. A site pointed at the wrong world then renders the banner of the
    // world it is actually reading, which is the only arrangement where the
    // label cannot come apart from the thing it describes.
    //
    // WHY IT IS WRITTEN FROM THE FIRST TICK RATHER THAN ON A TIMER. Nothing
    // here changes while the process lives: the module's version and the core's
    // are compiled in, and the environment is fixed at exec. Re-writing it every
    // thirty seconds would be a database round trip to reach the same answer.
    // Writing it once, at the same moment the watch list and the roster are
    // loaded, means the page can label the world from the first snapshot rather
    // than from whenever the second timer happens to fire.
    //
    // WHAT HAPPENS IF THE DEPLOYMENT SETS NONE OF THE VARIABLES, which is what
    // every realm will do on the first start after this ships: the report still
    // gets written, and it says what this binary knows about itself and admits
    // it was told nothing else. The one thing it never does is guess.
    void WriteBuildReport()
    {
        // The environment, read once here so that everything downstream of it
        // is a decision about strings that a test can reach with no world
        // behind it. std::getenv returns nullptr for a variable that was not
        // set, and an absent key is exactly how BuildReport wants to be told.
        std::map<std::string, std::string> env;
        for (char const* name : {OverseerDecisions::ENV_REALM,
                                 OverseerDecisions::ENV_REALM_KIND,
                                 OverseerDecisions::ENV_PIN_CORE,
                                 OverseerDecisions::ENV_PIN_PLAYERBOTS,
                                 OverseerDecisions::ENV_PIN_OLLAMA_CHAT,
                                 OverseerDecisions::ENV_PIN_DUNGEON_CLEAR,
                                 OverseerDecisions::ENV_PIN_AH_BOT})
        {
            if (char const* value = std::getenv(name))
                env[name] = value;
        }

        // GitRevision is the core reporting itself, the same call Banner.cpp
        // makes for the line that goes into the log at startup. It is the one
        // upstream version that does not have to be declared, because the core
        // is the only upstream here that is linked rather than merely compiled
        // alongside - which is why it is also the witness the pin verdict uses.
        std::vector<OverseerDecisions::BuildFact> const facts =
            OverseerDecisions::BuildReport(GitRevision::GetFullVersion(), env);

        // INTO THE LOG FIRST, BEFORE ANY OF IT GOES NEAR THE DATABASE. The
        // database is exactly the place this is unreadable from when something
        // has gone wrong enough to be worth reading, and a report that is only
        // written after a successful write is missing precisely when it would
        // have explained the failure. This is the line an operator greps for
        // when a realm is behaving like a build it is not supposed to be
        // running.
        for (OverseerDecisions::BuildFact const& fact : facts)
            LOG_INFO("module.overseer", "overseer build: {} = {} ({})",
                     fact.name, fact.value, fact.source);

        // ONE TRANSACTION, AND IT STARTS WITH A DELETE. The report is the
        // CURRENT state of this realm, not a history of it: a row for a fact an
        // older build used to report and this one does not would otherwise sit
        // there forever, indistinguishable from a fresh one, and the page would
        // render it. Clearing first means the table always holds exactly one
        // report and never a merge of two. The transaction is what stops a
        // reader polling between the delete and the writes from seeing a realm
        // that has suddenly reported nothing at all.
        //
        // WRAPPED, THOUGH NOTHING IN HERE IS EXPECTED TO THROW, and the reason
        // is what this code is for rather than what it does. This runs on the
        // world's FIRST TICK, in OnUpdate, with no handler between it and the
        // core's update loop. Everything it writes is a banner. There is no
        // version of "the page could not label the realm" that is worth even a
        // small chance of "the world did not come up", so the cost of being
        // certain is one block.
        //
        // WHAT COULD ACTUALLY REACH IT, since a reader deserves better than a
        // defensive catch with no story: not a MySQL fault. CommitTransaction
        // hands the batch to the async writer and returns, so a database that
        // is down, read-only, or rejecting the statement fails on the worker
        // thread long after this function has returned - see the durability
        // note on the chat flush, which is the same arrangement. What is left
        // is allocation, and a format failure inside Append. Neither is
        // expected. Both are survivable, and neither is worth a realm.
        //
        // AND THIS IS NOT A SWALLOW. It logs at ERROR with what failed, and the
        // consequence is visible on the page rather than hidden by it: a realm
        // with no rows in this table renders as REALM NOT VERIFIED, which is
        // the alarm the site already draws for a realm that has not reported.
        // The failure surfaces in the one place somebody is looking.
        try
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            trans->Append("DELETE FROM overseer_build");
            for (OverseerDecisions::BuildFact const& fact : facts)
            {
                // Esc, not EscLong: `value` is VARCHAR(255) and MAX_CHAT_BYTES
                // is 255, so the truncation is the column width and it cuts on
                // a UTF-8 boundary rather than mid-character. Nothing that
                // belongs in this table is near that length; an environment
                // variable that is has been set to something that is not a
                // commit.
                trans->Append(
                    "INSERT INTO overseer_build (name, value, source) VALUES ('{}', '{}', '{}')",
                    Esc(fact.name), Esc(fact.value), Esc(fact.source));
            }
            CharacterDatabase.CommitTransaction(trans);
        }
        catch (std::exception const& e)
        {
            LOG_ERROR("module.overseer",
                      "overseer build: could not write the report ({}) - the "
                      "world is fine and the page will show this realm as "
                      "unverified until the next start",
                      e.what());
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
                    // Resolved HERE, on the world thread, precisely so that the
                    // chat hooks never resolve anything - see ForEachWatcher.
                    // CharacterCache covers offline characters too, so a watch
                    // row written before its character ever logs in still arms
                    // correctly, and because this list is rebuilt wholesale on
                    // every poll a character created later is picked up on the
                    // next one.
                    std::string canonical = entry.name;
                    if (normalizePlayerName(canonical))
                        entry.guid = sCharacterCache->GetCharacterGuidByName(canonical);

                    // Carry the realm's own spelling, not the row's. heardBy
                    // used to come from Player::GetName() and the relay matches
                    // on it, so a row written as "og" must still record "Og".
                    if (!entry.guid.IsEmpty())
                        sCharacterCache->GetCharacterNameByGuid(entry.guid, entry.name);

                    if (entry.guid.IsEmpty())
                    {
                        // Said once per name rather than once per poll. The old
                        // behaviour for a misspelled row was to capture nothing
                        // at all, forever, with nothing in the log to say why -
                        // which is the exact failure the roster comment above
                        // says this file exists to abolish.
                        static std::set<std::string> warned;
                        if (warned.insert(entry.name).second)
                            LOG_WARN("module.overseer",
                                     "overseer: chat watch row names '{}', which is not a character "
                                     "this realm knows - capturing nothing for it. Check the spelling "
                                     "in overseer_chat_watch.",
                                     entry.name);
                        continue;
                    }

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

            CaptureBypassed(player, KindFromName(channel), text, [&](ObjectGuid w)
            {
                if (!group->IsMember(w))
                    return false;
                if (subgroupOnly && group->GetMemberGroup(w) != senderSub)
                    return false;
                return WatcherOnline(w);
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
            CaptureBypassed(player, kind, text, [&](ObjectGuid w)
                            { return GuildCanHear(guild, w, kind) && WatcherOnline(w); });
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

    // ------------------------------------------- a bag goes in a bag slot --
    //
    // A CONTAINER DOES NOT NEED INVENTORY SPACE. It needs a BAG SLOT, and a
    // bag slot is a different place: the four at INVENTORY_SLOT_BAG_START..
    // INVENTORY_SLOT_BAG_END (Player.h:684-688), which sit after the nineteen
    // equipment slots and before the sixteen backpack ones (Player.h:659-694)
    // and are the only slots FindEquipSlot will consider for an INVTYPE_BAG
    // (PlayerStorage.cpp:214-219).
    //
    // Asking CanStoreItem about a bag therefore asks about the wrong place,
    // and the answer it gives back - EQUIP_ERR_INVENTORY_FULL, "no room in the
    // inventory" - names the one condition that a bag is the CURE for. That is
    // mod-overseer#169 in a sentence: three characters could not loot, the
    // family was carrying 22 unused slots' worth of bags, and every attempt to
    // hand one over was refused on the grounds that it was needed. Measured on
    // the dev world at the time: one receiver had a single bag equipped and
    // three empty bag slots, and was told his bags were full.

    // How many of the four bag slots are empty. Reported on every give row so
    // the two refusals can be told apart by anybody reading the queue: four
    // bags and a full inventory is a standing fact about the character, while
    // "no room" alongside three empty bag slots is the bug this issue is
    // about, and until now the row said the same thing in both cases.
    static uint8 CountFreeBagSlots(Player* who)
    {
        uint8 freeSlots = 0;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (!who->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                ++freeSlots;
        return freeSlots;
    }

    // Can the receiver WEAR this container, and where? The core is the
    // authority and is asked as the core: CanEquipItem with slot = NULL_SLOT
    // is exactly what makes FindEquipSlot search the four bag slots for a free
    // one (PlayerStorage.cpp:260-265), and it also applies every rule this
    // module has no business restating - quiver and ammo-pouch uniqueness
    // (PlayerStorage.cpp:2015-2023), combat (PlayerStorage.cpp:1938-1941),
    // binding, and the script hook.
    //
    // `pos` comes back as (bag << 8) | slot, which is the form Player::EquipItem
    // takes (PlayerStorage.cpp:2838-2839).
    static InventoryResult CanWearContainer(Player* receiver, Item* container, uint16& pos)
    {
        pos = 0;
        // Item.h:254 - IsBag() is InventoryType == INVTYPE_BAG, the same fact
        // FindEquipSlot switches on, so this and the core agree by
        // construction rather than by coincidence.
        if (!container->IsBag())
            return EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT;

        InventoryResult const res =
            receiver->CanEquipItem(NULL_SLOT, pos, container, false, true);
        if (res != EQUIP_ERR_OK)
            return res;

        // Belt and braces on a uint16 that decides where an item is PUT. If
        // this is ever not one of the four bag slots (PlayerStorage.cpp:594-602), the
        // right answer is to refuse rather than to place an item somewhere
        // nobody meant it to go.
        if (!Player::IsBagPos(pos))
            return EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT;
        return EQUIP_ERR_OK;
    }

    // A SPARE BAG THE GIVER COULD HAND OVER TO UNBLOCK HIMSELF. Searched only
    // when a give has already been refused for want of room, because that is
    // the only time the answer is worth anything: the container removes the
    // condition that blocked the transfer, so offering it first turns one
    // impossible hand-over into two possible ones.
    //
    // Worn bags are skipped by CanBeTraded, which refuses a bag that is in a
    // bag position or is not empty (Item.cpp:800-801), so what comes back here
    // is a spare being carried as cargo - which is exactly what the family had
    // four of while three of its members could not loot.
    static Item* SpareContainerFor(Player* giver, Player* receiver, uint16& pos)
    {
        auto usable = [&](Item* candidate) -> bool
        {
            if (!candidate || !candidate->IsBag() || candidate->IsSoulBound())
                return false;
            if (!candidate->CanBeTraded())
                return false;
            // Into a local first, so `pos` is written only for the container
            // that is actually returned and can never be read as the answer
            // for one that was rejected.
            uint16 where = 0;
            if (CanWearContainer(receiver, candidate, where) != EQUIP_ERR_OK)
                return false;
            pos = where;
            return true;
        };

        // The backpack first, then inside the worn bags - the same two sweeps
        // FindCarriedItem makes, minus the equipment slots, which for a
        // container can only mean a bag that is being worn and therefore one
        // CanBeTraded would refuse anyway.
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* candidate = giver->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (usable(candidate))
                    return candidate;

        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        {
            Bag* bag = giver->GetBagByPos(bagSlot);
            if (!bag)
                continue;
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                if (Item* candidate = bag->GetItemByPos(i))
                    if (usable(candidate))
                        return candidate;
        }
        return nullptr;
    }

    // THE MOVE ITSELF, once it is settled where the item is going. Both of
    // DoGive's transfers go through this - the requested item, and the bag
    // handed over first to make room for it - so the four steps below are
    // written once and the two hand-overs cannot drift apart.
    //
    // Returns the slot the item landed in on the receiver, or -1 if it merged
    // into a stack he already had (see step 4).
    static int32 PlaceItemOn(Player* giver, Player* receiver, Item* item,
                             ItemPosCountVec const& dest, uint16 equipPos, bool equip)
    {
        ObjectGuid const itemGuid = item->GetGUID();
        uint8 const srcBag = item->GetBagSlot();
        uint8 const srcSlot = item->GetSlot();

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

        // 3. Into the receiver's bags - or ONTO him, if this is a container
        //    going into a bag slot. EquipItem is the same call the core makes
        //    for a player dragging a bag onto a bag slot: SwapItem checks
        //    CanEquipItem and then equips (PlayerStorage.cpp:3764-3772), and
        //    IsEquipmentPos counts the four bag slots as equipment
        //    (PlayerStorage.cpp:574-581). VisualizeItem inside it does the
        //    SetState(ITEM_CHANGED, receiver) that MoveItemToInventory would
        //    otherwise have done (PlayerStorage.cpp:2981-3003), so the item is
        //    in the receiver's update queue either way.
        //
        //    in_characterInventoryDB = true on the store because the
        //    character_inventory row is written just below rather than being
        //    deferred to the receiver's next save.
        if (equip)
            receiver->EquipItem(equipPos, item, true);
        else
            receiver->MoveItemToInventory(dest, item, true, true);

        // 4. The placement, durably. Re-found by guid rather than reusing
        //    `item`: a store that merges into an existing stack consumes the
        //    source item (PlayerStorage.cpp:2786-2800), and in that case there
        //    is no new row to write - the surviving stack already has one.
        //    GetItemByGuid's first sweep covers slots 0..38
        //    (PlayerStorage.cpp:413-416), so it finds an equipped bag as
        //    readily as a stored one.
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
        return destSlot;
    }

    // EVERY exit from here writes `out`, including every refusal. A row that
    // ends 'error' with an empty result is a row nobody can diagnose from
    // outside the worldserver, and this queue has already shipped one action
    // that reported success while doing nothing.
    //
    // NOT static, unlike everything around it, and for one reason: the refusal
    // memory below. A backoff is a fact about what happened on an EARLIER
    // poll, and there is nowhere for that to live but the script object.
    char const* DoGive(Player* giver, std::string const& receiverName,
                       std::string const& command, char const*& status,
                       std::string& out)
    {
        time_t const now = std::time(nullptr);

        // THE GIVE, AS THE REFUSAL BOOK KEYS IT. These three fields are what
        // the sender re-inserts unchanged every time it re-asks for a
        // hand-over it has not seen succeed, which is what makes them a key
        // and not just a description.
        std::string const giveKey = giver->GetName() + "\n" + command;
        OverseerDecisions::GiveRefusalBook& book = _giveRefusals[receiverName];

        std::string heldReason;
        if (OverseerDecisions::GiveHeldOff(book, giveKey, now, GIVE_REFUSAL_BACKOFF_SECONDS,
                                           GIVE_REFUSAL_FORGET_SECONDS, heldReason))
        {
            std::ostringstream o;
            o << "{\"outcome\":\"held\",\"reason\":" << J(heldReason)
              << ",\"from\":" << J(giver->GetName())
              << ",\"to\":" << J(receiverName)
              << ",\"request\":" << J(command)
              << ",\"backoff_seconds\":" << int64(GIVE_REFUSAL_BACKOFF_SECONDS) << "}";
            out = o.str();
            // Deliberately no log line. This is the poll that exists in order
            // to be quiet: the reason was printed once when the wall was first
            // hit, and it is in the row above for anybody who wants it again.
            return "not retried; the same refusal is still standing";
        }

        // Every refusal below goes through this, so arming the backoff cannot
        // be forgotten at one exit and remembered at the others. It returns
        // the detail it was given so a refusal still reads as one line.
        auto arm = [&](char const* detail) -> char const*
        {
            if (OverseerDecisions::NoteGiveRefusal(book, giveKey, detail, now))
                LOG_INFO("module.overseer",
                         "overseer: '{}' cannot give {} to '{}': {}; leaving it alone for {}s",
                         giver->GetName(), command, receiverName, detail,
                         int64(GIVE_REFUSAL_BACKOFF_SECONDS));
            return detail;
        };

        auto refuse = [&](char const* reason, char const* detail) -> char const*
        {
            std::ostringstream o;
            o << "{\"outcome\":\"refused\",\"reason\":" << J(reason)
              << ",\"from\":" << J(giver->GetName())
              << ",\"to\":" << J(receiverName)
              << ",\"request\":" << J(command) << "}";
            out = o.str();
            return arm(detail);
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
        // Captured for the same reason as the rest: describe() runs after the
        // move as well as before it, and on a store that merged into an
        // existing stack the Item this was read from is already gone.
        bool const isContainer = item->IsBag();

        // WHAT MADE ROOM, if anything did. Filled by the bag hand-over below
        // and reported on every exit after it, because a row that says "the
        // receiver was full" while a bag quietly went the other way is a row
        // that describes half of what happened.
        std::string madeRoom;
        // What the core said about wearing this item, for the rows that get
        // written before the question is asked. Declared up here rather than
        // where it is answered because describe() reads it.
        InventoryResult wearResult = EQUIP_ERR_OK;

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
              << ",\"store_result\":" << storeResult
              // The two facts that separate "he is full" from "he has nowhere
              // to PUT A BAG", which is the distinction this whole path exists
              // to make. Reported on refusals and on successes alike, so the
              // row that refuses and the row that works are comparable.
              << ",\"is_container\":" << (isContainer ? "true" : "false")
              // Read fresh on every call rather than captured, so a row
              // written AFTER a bag went the other way reports the receiver
              // as he is now and not as he was when this function started.
              << ",\"free_bag_slots\":" << uint32(CountFreeBagSlots(receiver))
              // What the core said about WEARING it, which for a container is
              // the answer that decided the outcome. EQUIP_ERR_OK on anything
              // that is not a container simply means the question was never
              // asked, which is why is_container is next to it.
              << ",\"wear_result\":" << int32(wearResult);
            if (!madeRoom.empty())
                o << ",\"made_room\":" << madeRoom;
            o << "}";
            out = o.str();
        };

        // Soulbound is checked separately from CanBeTraded purely so the
        // refusal can NAME it: it is the one refusal that is permanent, and
        // telling an operator "cannot be handed over" when the real answer is
        // "and never will be" invites them to retry forever.
        if (item->IsSoulBound())
        {
            describe("refused", "item is soulbound to the giver", EQUIP_ERR_OK, -1);
            return arm("item is soulbound and can never be handed over");
        }
        // Everything else Item::CanBeTraded (Item.cpp:795-820) knows about: a
        // non-empty bag, a bag that is itself equipped, an item currently
        // being looted, a temporary or bind-on-enchant.
        if (!item->CanBeTraded())
        {
            describe("refused", "item cannot be traded (non-empty bag, worn bag, being looted, "
                                "or bound by enchant)", EQUIP_ERR_OK, -1);
            return arm("item cannot be handed over");
        }

        // ---- WHERE THE RECEIVER CAN PUT IT ---------------------------------
        //
        // Asked BEFORE anything is removed from the giver, so a receiver with
        // no room costs nothing: the giver still has the item and the row says
        // why.
        //
        // TWO QUESTIONS, and which one applies depends on the ITEM. See the
        // note above CountFreeBagSlots: a container goes in a bag slot, an
        // ordinary item goes in the inventory, and answering the second
        // question for both is the whole of mod-overseer#169.
        ItemPosCountVec dest;
        uint16 equipPos = 0;
        bool equip = false;
        InventoryResult msg = EQUIP_ERR_OK;

        if (isContainer)
        {
            // The bag slot FIRST, and not merely as a fallback for a full
            // inventory. A spare bag carried as cargo is eight slots doing
            // nothing and one more slot spent holding it; the same bag worn is
            // eight slots of room. The issue asks for it equipped, and that is
            // also the only outcome that changes anything.
            wearResult = CanWearContainer(receiver, item, equipPos);
            equip = (wearResult == EQUIP_ERR_OK);
        }
        if (!equip)
        {
            // Either it is not a container, or every bag slot is worn and it
            // has to be carried - which is how the family came to be holding
            // four spares in the first place, and is still a real hand-over.
            msg = receiver->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        }

        // ---- OFFER A BAG FIRST, IF ONE WOULD UNBLOCK THIS ------------------
        //
        // The receiver has no room and this item is not the thing that would
        // give him any. If the giver is carrying a spare bag the receiver
        // could wear, hand THAT over first: it is the one item on either
        // character that changes the answer, and the family had four of them
        // in its bags while three of its members could not loot.
        //
        // Only for a non-container, and not merely as an optimisation: a
        // container that reached here has already failed CanWearContainer, so
        // any spare would fail it for the same reason - and SpareContainerFor
        // could otherwise hand back the very item being given.
        if (!equip && !isContainer && msg == EQUIP_ERR_INVENTORY_FULL)
        {
            uint16 roomPos = 0;
            if (Item* spare = SpareContainerFor(giver, receiver, roomPos))
            {
                ObjectGuid const spareGuid = spare->GetGUID();
                uint32 const spareEntry = spare->GetEntry();
                std::string const spareName = spare->GetTemplate()->Name1;
                uint32 const spareSize = spare->ToBag() ? spare->ToBag()->GetBagSize() : 0;

                ItemPosCountVec none;
                int32 const spareSlot = PlaceItemOn(giver, receiver, spare, none, roomPos, true);

                LOG_INFO("module.overseer",
                         "overseer: '{}' had no room for {}, so '{}' handed over "
                         "'{}' (item {}, {} slots) to make some; it is now in bag slot {}",
                         receiver->GetName(), itemName, giver->GetName(), spareName,
                         spareGuid.GetCounter(), spareSize, spareSlot);

                std::ostringstream room;
                room << "{\"item_guid\":" << spareGuid.GetCounter()
                     << ",\"entry\":" << spareEntry
                     << ",\"name\":" << J(spareName)
                     << ",\"slots\":" << spareSize
                     << ",\"bag_slot\":" << spareSlot << "}";
                madeRoom = room.str();

                // ASK AGAIN. The receiver has just grown by a bagful, so this
                // is a different question from the one answered a moment ago,
                // and answering it here is what lets the hand-over that has
                // been failing for hours finish on the poll that unblocked it
                // instead of on some later one.
                dest.clear();
                msg = receiver->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
            }
        }

        if (!equip && msg != EQUIP_ERR_OK)
        {
            describe("refused", isContainer ? "receiver has no free bag slot and cannot store it"
                                            : "receiver cannot store the item",
                     int32(msg), -1);
            // No apostrophe, and that is not a style choice: `detail` is
            // embedded straight into the UPDATE below, so every literal on
            // this path must be free of quote characters.
            //
            // The message for a full receiver is UNCHANGED, deliberately: a
            // character wearing four bags with nothing free in them is still
            // refused, and refused in the same words an operator has been
            // reading all along. What changed is who gets told it.
            return arm(msg == EQUIP_ERR_INVENTORY_FULL ? "receiver bags are full"
                                                       : "receiver cannot store the item");
        }

        int32 const destSlot = PlaceItemOn(giver, receiver, item, dest, equipPos, equip);

        LOG_INFO("module.overseer",
                 "overseer: gave item {} (entry {}) from '{}' to '{}'{}",
                 itemGuid.GetCounter(), itemEntry, giver->GetName(), receiver->GetName(),
                 equip ? ", worn in a bag slot" : "");

        describe("moved", equip ? "equipped in a free bag slot" : "", EQUIP_ERR_OK, destSlot);
        status = "delivered";

        // SOMETHING GOT THROUGH, so every refusal remembered against this
        // receiver is now an answer to a question whose facts have changed -
        // most obviously if what got through was a bag. Forgetting them costs
        // one retry each and buys the family the room immediately. `book` is a
        // reference into the map being erased from, so nothing may touch it
        // after this line.
        _giveRefusals.erase(receiverName);
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
    // The first roster sweep runs one poll after startup rather than
    // immediately: the world is still settling in the first ticks, and there
    // is nobody to evict yet because nothing logs a roster character in any
    // more (#131) - only a client can.
    uint32 _rosterTimer = 0;
    uint32 _partyTimer = 0;
    uint32 _trainTimer = 0;
    uint32 _questTimer = 0;
    // WHEN A DEAD CHARACTER WAS FIRST SEEN STILL WAITING TO RELEASE, by name.
    //
    // There is no ghost clock for this state - a corpse, and therefore a ghost
    // time, only exists after the release - so the drive has to keep its own.
    // Written and read only from DriveStuckRevival on the world thread, so it
    // is unguarded like TravelAimBook's own maps and _lastAim above, and lost
    // on restart, which costs at most one extra grace period.
    std::map<std::string, int64> _awaitingRelease;

    // WHICH DEATHS THIS DRIVE HAS SAID IT IS LEAVING TO THE HEALER, by name.
    // Same ownership and lifetime as _awaitingRelease: DriveStuckRevival on
    // the world thread only, cleared when the character is alive again, lost
    // on restart at the cost of one repeated line.
    std::set<std::string> _healerExpected;

    // WHERE THIS DRIVE LAST REVIVED EACH CHARACTER (graveyard id, when), so a
    // second revival at the same graveyard inside GRAVEYARD_REPEAT_SECONDS
    // goes home instead. Cleared by a revival anywhere that is not a
    // graveyard. Same ownership and lifetime as _awaitingRelease.
    std::map<std::string, std::pair<uint32, int64>> _lastRevival;

    // UNTIL WHEN EACH REVIVED CHARACTER IS HELD, and whether it led (carried
    // `new rpg`) when the hold began, so the right mover is restored. Read
    // by the movers this module owns through HeldAfterRevival. Same
    // ownership and lifetime as _awaitingRelease: lost on restart, which
    // costs nothing, because a restart also resets every bot's strategies
    // to upstream's defaults and there is no `stay` left to take off.
    std::map<std::string, std::pair<int64, bool>> _revivalHoldUntil;

    // What KeepRosterFollowing knew about each follower's position last
    // time round, so a stall is measured against ITS OWN best position
    // rather than the previous poll - the same closest-ratchet shape as
    // TravelState::closest above, applied to "has this character actually
    // moved" instead of "has this errand gotten nearer". Written and read
    // only from KeepRosterFollowing on the world thread, so it is unguarded
    // like TravelAimBook's errand memory and _awaitingRelease, and lost on
    // restart, which costs at most one extra grace period before a stall is
    // acted on again.
    struct FollowStallState
    {
        bool seen{false};      // false until the first position is recorded
        float x{0.f};          // the mark a stall is measured FROM
        float y{0.f};
        // The shared ratchet's memory: how far this character has got from
        // that mark, and when it last got far enough to count. The mark
        // itself stays here because it is a POSITION and the ratchet deals
        // in one number.
        OverseerDecisions::RatchetState progress;
        time_t nudged{0};      // when a recovery was last tried, so a stall
                                // that survives one MotionMaster::Clear() is
                                // not clobbered again every poll
    };
    std::map<std::string, FollowStallState> _followStall;

    uint32 _travelTimer = 0;
    uint32 _professionTimer = 0;
    uint32 _gearTimer = 0;
    uint32 _engagementTimer = 0;
    uint32 _terrainRecoveryTimer = 0;
    // GATHERING/BARRIER coordinator poll (mod-overseer#88). Its own timer,
    // not piggybacked on QUEST_POLL_MS or ENGAGEMENT_POLL_MS: this drive is
    // safety-critical for the same reason DriveEngagementSafety is - see
    // DUNGEON_RUN_POLL_MS above - and either existing cadence would be either
    // too slow (quest) or would tie this drive's interval to a constant that
    // is free to change for engagement-safety reasons that have nothing to do
    // with gathering a party.
    uint32 _dungeonRunTimer = 0;

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

    // `overseer_roster.travel_npc`, AND the two pieces of memory that have to
    // move with it - the errand in flight and the hand-back clock. One object,
    // because they were only ever one thing: see TravelAimBook for why the
    // column having two writers with different manners was a bug and not a
    // style. Both drives reach the column through this and nothing else. World
    // thread only, like everything else on these loops.
    TravelAimBook _travelAims;

    // The unlearn this module has already refused for each character, so a
    // standing disagreement about the price is said once rather than twice a
    // minute forever (infra#2757). Name -> the skill id refused. Cleared when
    // the request is cleared, and lost on restart - which costs one repeated
    // log line and no correctness, exactly like the hand-back clock inside
    // _travelAims.
    std::map<std::string, uint32> _unlearnRefused;
    // The characters whose declared end state asks for more primary
    // professions than the realm lets anybody hold, so that unsatisfiable
    // roster row is said once instead of twice a minute forever. Nothing is
    // given up for these characters and nothing ever will be - see the Nothing
    // branch of StepTowardAssignment - so this is a log-volume matter and not
    // a piece of state anything decides on. Lost on restart, which costs one
    // repeated line, exactly like _unlearnRefused above.
    std::set<std::string> _assignmentTooBig;
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

    // THE GIVES THIS MODULE HAS ALREADY REFUSED, so a hand-over that cannot
    // work is not attempted thirty times a minute for as long as it cannot
    // (mod-overseer#169). Keyed by RECEIVER, then by the give itself, because
    // the receiver is the unit that gets forgotten: what these refusals are
    // usually about is how much room he has, so the moment anything does reach
    // him every one of them is stale. World thread only, like every other
    // memory on this loop, and lost on a restart - which costs one early retry
    // per entry and no correctness. The rule it implements is in
    // OverseerDecisions::GiveHeldOff, where it can be tested without a world.
    std::map<std::string, OverseerDecisions::GiveRefusalBook> _giveRefusals;

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
