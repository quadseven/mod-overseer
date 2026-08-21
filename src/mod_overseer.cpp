/*
 * mod-overseer: the bridge between the outside world and the simulation.
 *
 * Two jobs, both running on the world-update thread:
 *
 *  1. Command delivery. Rows inserted into acore_characters.overseer_command
 *     (by the Discord bridge) are delivered to the named bot as if whispered:
 *     PlayerbotAI::HandleCommand(CHAT_MSG_WHISPER, ...) with the bot itself
 *     as the speaker - the same self-command pattern mod-playerbots uses
 *     internally (CastCustomSpellAction). No network client, no session.
 *
 *  2. Presence snapshots. Every few seconds the positions and vitals of every
 *     online character are written to acore_characters.overseer_snapshot,
 *     which is what the live map reads. The characters table itself only
 *     persists position on the periodic save, which is minutes stale.
 *
 * The database is the whole interface on purpose: no listening socket, no new
 * network surface on the worldserver. Anything that can reach MySQL (which is
 * cluster-internal) can observe and command; nothing else can.
 */

#include "DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

#include <sstream>

namespace
{
constexpr uint32 COMMAND_POLL_MS = 2000;
constexpr uint32 SNAPSHOT_MS = 5000;
// One world tick must never stall on a burst of queued commands.
constexpr uint32 COMMANDS_PER_POLL = 20;
}

class OverseerWorldScript : public WorldScript
{
public:
    OverseerWorldScript() : WorldScript("OverseerWorldScript") {}

    void OnUpdate(uint32 diff) override
    {
        _commandTimer += diff;
        _snapshotTimer += diff;
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
    }

private:
    void DeliverPendingCommands()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT id, target_name, command FROM overseer_command "
            "WHERE status = 'pending' ORDER BY id ASC LIMIT {}",
            COMMANDS_PER_POLL);
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 id = fields[0].Get<uint32>();
            std::string targetName = fields[1].Get<std::string>();
            std::string command = fields[2].Get<std::string>();

            char const* status = "error";
            char const* detail = "";

            // Names are server-enforced to letters only, so they are safe to
            // embed; `detail` is one of the literals below; `command` is never
            // echoed back into SQL.
            if (Player* player = ObjectAccessor::FindPlayerByName(targetName))
            {
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
                {
                    botAI->HandleCommand(CHAT_MSG_WHISPER, command, player);
                    status = "delivered";
                }
                else
                    detail = "target has no bot AI (selfbot not enabled?)";
            }
            else
                detail = "target not online";

            CharacterDatabase.Execute(
                "UPDATE overseer_command SET status = '{}', detail = '{}' WHERE id = {}",
                status, detail, id);
        } while (result->NextRow());
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
};

void Addmod_overseerScripts()
{
    new OverseerWorldScript();
}
