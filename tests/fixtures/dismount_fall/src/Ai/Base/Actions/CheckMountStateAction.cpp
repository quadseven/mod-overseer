/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CheckMountStateAction.h"
#include "PlayerbotsDatabase.h"
#include "AreaDefines.h"
#include "BattleGroundTactics.h"
#include "BattlegroundEY.h"
#include "BattlegroundWS.h"
#include "DBCStores.h"
#include "Event.h"
#include "GameTime.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SpellAuraEffects.h"

static constexpr uint32 SPELL_COLD_WEATHER_FLYING = 54197;
static constexpr float PARACHUTE_LAND_THRESHOLD = 15.0f;

// Preferred mounts per bot, loaded once at startup by LoadPreferredMounts()

            bot->SendMovementFlagUpdate();
    }
}

void CheckMountStateAction::CompleteDismount(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return;

    float const startZ = bot->GetPositionZ();

    // Settle the bot on the ground when it was dismounted in mid-air. MoveFall()
    // is the whole job on its own: it returns when no valid height is found, and
    // aborts when the ground is already less than 0.1 yards away, so a bot that
    // dismounts standing on solid footing is left untouched.
    //
    // MAX_FALL_DISTANCE IS NOT A SAFETY BOUND, and the comment that used to sit
    // here said it was. It is 250000.0f, commented "unlimited fall" at
    // GridTerrainData.h:28. So where the 50 yard default would have refused to
    // find a surface, this finds one arbitrarily far down and MoveFall commits
    // to the whole descent. On vmap geometry - a dock, a pier, a bridge, a
    // building floor - where the height search resolves to the terrain
    // UNDERNEATH the surface the bot is standing on, that descent is a drop the
    // world never asked for.
    //
    // That matters more than it looks, because it is the half of the old bug
    // that was left standing. The removed HandleFall() block probed with
    // UpdateAllowedPositionZ, whose search is DEFAULT_HEIGHT_SEARCH (50 yards),
    // so it was capped at 0.018 * 50 - 0.2426 = 65.7% of maximum health per
    // dismount and needed two inside the regeneration window to kill. This call
    // has no such cap. The fix stopped fabricating the damage and left the code
    // that can fabricate the fall, and a bot that shares its Player with a real
    // connected client will have that manufactured drop billed back to it for
    // real, as a MSG_MOVE_FALL_LAND the client sends on landing.
    bot->GetMotionMaster()->MoveFall();

    // Rebase the fall baseline on the spot the bot dismounted at, so a fall is
    // never measured from a stale, much higher Z. MoveFall() does this itself
    // once it commits to a descent, but it returns before that point in both of
    // its early exits and m_lastFallZ then keeps whatever value it was already
    // carrying.
    //
    // THE TIME ARGUMENT IS NOT COSMETIC, AND `0` WAS THE WRONG ONE.
    // SetFallInformation sets two fields, m_lastFallTime and m_lastFallZ, and
    // only the Z is what the paragraph above is about. The time decides how long
    // the Z survives, in Player::UpdateFallInformationIfNeed
    // (PlayerUpdates.cpp:2191):
    //
    //     if (m_lastFallTime >= minfo.fallTime ||
    //         m_lastFallZ <= minfo.pos.GetPositionZ() ||
    //         opcode == MSG_MOVE_FALL_LAND)
    //         SetFallInformation(minfo.fallTime, minfo.pos.GetPositionZ());
    //
    // `minfo.fallTime` is milliseconds since the current fall began, so it is a
    // small number. Every other SetFallInformation call site in the core passes
    // GameTime::GetGameTime().count(), a Unix timestamp, which makes
    // `m_lastFallTime >= minfo.fallTime` permanently true: those baselines
    // re-base on the very next movement packet and cannot go stale. That is not
    // an accident, it is why a teleport, a taxi landing, a vehicle exit and a
    // knockback all grant a free fall rather than a fatal one.
    //
    // `0` is the single value that defeats that clause. With it the baseline
    // survives every packet the client sends while it is airborne and below
    // startZ, and is only cleared by a landing or by returning to that height.
    // Exactly two call sites in the whole tree pass 0, and this was one of them.
    // For a bot that has no client attached that is invisible. For one that
    // shares its Player with a real connected client - which is how this module
    // is run on a streamed realm - it is the reference the core charges that
    // client's next landing against.
    //
    // The self-clearing form is correct on BOTH of MoveFall()'s paths, which is
    // why there is no branch here. Where MoveFall committed, it has already
    // written (GameTime, GetPositionZ()) and the position has not moved yet, so
    // this is the same pair of values written twice. Where it declined, this is
    // the only thing that clears the stale Z, and there is no descent to measure
    // so nothing is lost by letting the next packet re-base it.
    bot->SetFallInformation(GameTime::GetGameTime().count(), startZ);

    // OPEN QUESTION, DELIBERATELY NOT CHANGED HERE. When MoveFall() has just
    // committed, this clears MOVEMENTFLAG_FALLING two lines after MoveFall() set
    // it, while its spline is still running - so the server tells a connected
    // client it is not falling during exactly the descent whose landing packet
    // gets billed. Making it conditional needs a reliable "did MoveFall commit"
    // signal, MoveFall() returns void, and getting that guard wrong reintroduces
    // the stuck flight flags this function exists to clear. It is filed rather
    // than guessed at.
    bot->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);

    // DO NOT call Player::HandleFall() here, and do not reintroduce a
    // UpdateAllowedPositionZ()-derived "ground" to feed it. That is what this
    // function used to do, and it billed environmental fall damage on EVERY
    // SMSG_DISMOUNT for the drop between the bot's feet and whatever surface the
    // height search resolved to, at the instant of dismount, for a descent that
    // had not happened and often never would. The bot did not move, so the
    // damage landed with zero distance travelled.
    //
    // HandleFall() is the handler for the client's MSG_MOVE_FALL_LAND opcode. It
    // is meant to run when a fall ENDS, on a real landing reported by a real
    // client. It is reached from exactly one place in the core,
    // MovementHandler.cpp:635, gated on that opcode. Execute() above says the
    // same thing in its forced flight dismount note: "Without
    // MSG_MOVE_FALL_LAND, HandleFall doesn't trigger, meaning bots don't get
    // fall damage in forced dismounts anyway". This function was the one place
    // that contradicted it.
    //
    // BUT "BOTS HAVE NO CLIENT" IS AN ASSUMPTION ABOUT A DEPLOYMENT, NOT A FACT
    // ABOUT THIS MODULE, and this comment used to assert it flatly. A Player
    // driven by PlayerbotAI can also have a real authenticated session on it -
    // that is how a streamed realm works, where a person watches the character
    // the AI is playing. Such a character sends movement packets like any other,
    // MSG_MOVE_FALL_LAND included, and everything above about what the core will
    // and will not charge it stops holding.
    //
    // So nothing in this function may leave state behind that is only harmless
    // because no client will ever read it. That is what the fall baseline below
    // got wrong on the first pass: it was safe as private server-side
    // bookkeeping and unsafe the moment a client's landing was measured against
    // it. Anything added here should be checked against both deployments.
    //
    // The billing was also inverted with respect to the mid-air case it was
    // written for. UpdateAllowedPositionZ() only lowers Z for a unit whose
    // CanFly() is false; for one that can fly it raises Z at most and leaves it
    // otherwise alone. So a genuinely flying bot was charged nothing, while a
    // grounded bot standing on vmap geometry (a bridge, a dock, a building
    // floor, a raised city surface) was charged the full drop to the terrain
    // underneath it, up to the 50 yard search limit. That is 65.7% of maximum
    // health per dismount at Rate.Damage.Fall = 1, so two dismounts inside the
    // health regeneration window killed a healthy bot outright.
    //
    // If simulated fall damage for bots is ever wanted, it belongs where the
    // fall ends and needs to be driven by the distance actually descended, not
    // applied up front from a projected landing point.
}

bool CheckMountStateAction::TryForms(Player* master, int32 masterMountType, int32 masterSpeed) const
{
    if (!master)
        return false;
}
