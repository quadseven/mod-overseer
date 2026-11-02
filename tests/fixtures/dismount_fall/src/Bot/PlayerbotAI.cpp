#include "LastSpellCastValue.h"
#include "LogLevelAction.h"
#include "LootObjectStack.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "NewRpgStrategy.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PerfMonitor.h"
#include "Player.h"

    if (bot)
        PlayerbotsMgr::instance().RemovePlayerBotData(bot->GetGUID(), true);
}

void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)
{
    // Handle the AI check delay
    if (nextAICheckDelay > elapsed)
        nextAICheckDelay -= elapsed;
    else
        nextAICheckDelay = 0;

    // Early return if bot is in invalid state
    if (!bot || !bot->GetSession() || !bot->IsInWorld() || bot->IsBeingTeleported() ||
        bot->GetSession()->IsLoggingOut() || bot->IsDuringRemoveFromWorld())
        return;

    // Bots send no movement opcodes, so m_lastFallZ stays frozen and Player::IsFalling() (a Z test
    // against it) blocks LFG teleports. Unit::IsFalling() is the flag test, so real falls keep theirs.
    if (!bot->Unit::IsFalling())
        bot->SetFallInformation(0, bot->GetPositionZ());

    // Handle cheat options (set bot health and power if cheats are enabled)
    if (bot->IsAlive() &&
}

        case SMSG_DISMOUNT:
        {
            WorldPacket p(packet);
            p.rpos(0);
            ObjectGuid guid;
            p >> guid.ReadAsPacked();
            if (guid != bot->GetGUID())
                return;
            CheckMountStateAction::CompleteDismount(bot);
            return;
        }
        default:
            botOutgoingPacketHandlers.AddPacket(packet);
    }
}

void PlayerbotAI::SpellInterrupted(uint32 spellid)
