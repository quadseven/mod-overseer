#ifndef MOD_OVERSEER_CORE_COMPAT_H
#define MOD_OVERSEER_CORE_COMPAT_H

// ONE ADAPTER, TWO UPSTREAM PIN SETS (wow-overseer#826).
//
// The adapter compiles against the pinned core + mod-playerbots in build.yml,
// and it must also compile against the heads the upstream catch-up (infra#5655)
// moves to, so the pins can move without a flag day. Between the two:
//
//   - core #27533 renamed WorldSession::IsBot() to IsHeadless();
//   - the core dropped Player::IsInChannel (Channel::IsOn is in both);
//   - the core dropped PlayerbotScript; the packet hook is now
//     ServerScript::OnPacketSent (ScriptMgr::OnPacketSent);
//   - the core dropped DatabaseScript::OnDatabaseSelectIndexLogout and
//     CHAR_UPD_CHAR_OFFLINE; a script now vetoes the account-wide offline mark
//     with PlayerScript::OnPlayerCanMarkAccountOffline instead;
//   - mod-playerbots #2854 renamed every PlayerbotAIConfig member to
//     UpperCamelCase.
//
// Every difference is answered here, once, by asking the type whether it has
// the member (a C++20 requires-expression; the core builds with C++20), never
// by a version macro. Whichever API the pinned tree has is the one that is
// called, and the old API is always tried first, so against the old pins every
// call below is exactly the call the adapter made before this header existed.
//
// WHY EVERYTHING HERE IS A TEMPLATE. A requires-expression only evaluates to
// false for a member that does not exist when the expression depends on a
// template parameter; outside a template a missing member is a hard error. The
// same goes for the discarded branch of `if constexpr`. So every shim takes
// its object as a deduced type, and the two hook adapters below are class
// templates whose base class and hook enumerators are made dependent with
// Dependent<>, so only the specialisation the pinned core selects is ever
// instantiated, and the names it uses only have to exist in that core.

#include "Channel.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <mutex>
#include <set>

// The older core's playerbot hook class. Declared, never defined, so naming it
// is legal against a core that no longer has it; it is only ever used as a
// base in the specialisation that core does not select.
class PlayerbotScript;

namespace OverseerCompat
{
// T, but spelled so the compiler cannot look inside it until D is known.
template <typename T, typename D>
struct Dependent
{
    using type = T;
};
template <typename T, typename D>
using DependentT = typename Dependent<T, D>::type;

// --- WorldSession: IsBot() (old) / IsHeadless() (new) ----------------------
// Both mean the same thing: a session with no client socket behind it.
template <typename Session>
bool SessionIsHeadless(Session const& session)
{
    if constexpr (requires { session.IsBot(); })
        return session.IsBot();
    else
        return session.IsHeadless();
}

// --- Player::IsInChannel (old) / Channel::IsOn (new) ------------------------
// The newer core has no public membership test at all: Player's joined-channel
// list is protected and Channel::IsOn is private. mod-ollama-chat met the same
// removal at the same pins and reads Channel::IsOn directly; so does this. An
// explicit instantiation may name a private member (C++20 [temp.spec.general]),
// and the friend it defines hands the member pointer out. IsOn has the same
// signature in both cores, so this compiles in both, and the older core never
// calls it.
struct ChannelIsOnTag
{
    using type = bool (Channel::*)(ObjectGuid) const;
    friend type MemberOf(ChannelIsOnTag);
};

template <typename Tag, typename Tag::type Member>
struct PrivateMember
{
    friend typename Tag::type MemberOf(Tag) { return Member; }
};

template struct PrivateMember<ChannelIsOnTag, &Channel::IsOn>;

template <typename P>
bool PlayerOnChannel(P& player, Channel const* channel)
{
    if constexpr (requires { player.IsInChannel(channel); })
        return player.IsInChannel(channel);
    else
        return (channel->*MemberOf(ChannelIsOnTag{}))(player.GetGUID());
}

// --- PlayerbotAIConfig members (mod-playerbots #2854) -----------------------
// Each returns a reference to the member, so an array indexes as before.
template <typename Config>
decltype(auto) SpellDistance(Config& config)
{
    if constexpr (requires { config.spellDistance; })
        return (config.spellDistance);
    else
        return (config.SpellDistance);
}

template <typename Config>
decltype(auto) RandomBotAccountPrefix(Config& config)
{
    if constexpr (requires { config.randomBotAccountPrefix; })
        return (config.randomBotAccountPrefix);
    else
        return (config.RandomBotAccountPrefix);
}

template <typename Config>
decltype(auto) RandomClassSpecIndex(Config& config)
{
    if constexpr (requires { config.randomClassSpecIndex; })
        return (config.randomClassSpecIndex);
    else
        return (config.RandomClassSpecIndex);
}

// --- Playerbots config key names (mod-playerbots #2854, mod-overseer #1068) --
// The same rename moved every config key from AiPlayerbot.<Name> to
// Playerbots.<Name>, and the newer mod-playerbots reads only the new spelling:
// a realm overlay that still sets the old one is ignored. Key names are
// strings, invisible to the compiler, so the member rename above is how this
// header tells which spelling the linked mod-playerbots (and every patch on
// it) reads. True when it reads Playerbots.<Name>. The adapter turns it into a
// key with OverseerDecisions::PlayerbotsConfigKey, never a literal.
template <typename Config>
inline constexpr bool PlayerbotsKeysRenamed = !requires(Config const& config) { config.spellDistance; };

// --- Every packet a session sends, client or bot ----------------------------
// Old core: PlayerbotScript::OnPlayerbotPacketSent(Player*, WorldPacket const*).
// New core: ServerScript::OnPacketSent(WorldSession*, WorldPacket const&).
// Both are called from WorldSession::SendPacket before the socket test, so a
// bot's packets reach the hook in both. Handler supplies
//   static void OnPacketSent(Player* player, WorldPacket const& packet);
// and is called with the session's player, which may be null.
template <typename M>
concept HasServerPacketSentHook = requires(M& mgr, WorldSession* session, WorldPacket const& packet) {
    mgr.OnPacketSent(session, packet);
};

template <typename Handler, bool ServerHook = HasServerPacketSentHook<ScriptMgr>>
class PacketSentScript;

template <typename Handler>
class PacketSentScript<Handler, false> : public DependentT<::PlayerbotScript, Handler>
{
    using Base = DependentT<::PlayerbotScript, Handler>;

public:
    explicit PacketSentScript(char const* name) : Base(name) {}

    void OnPlayerbotPacketSent(Player* player, WorldPacket const* packet) override
    {
        if (!packet)
            return;
        Handler::OnPacketSent(player, *packet);
    }
};

template <typename Handler>
class PacketSentScript<Handler, true> : public DependentT<ServerScript, Handler>
{
    using Base = DependentT<ServerScript, Handler>;

public:
    explicit PacketSentScript(char const* name)
        : Base(name, {DependentT<ServerHook, Handler>::SERVERHOOK_ON_PACKET_SENT})
    {
    }

    void OnPacketSent(WorldSession* session, WorldPacket const& packet) override
    {
        Handler::OnPacketSent(session ? session->GetPlayer() : nullptr, packet);
    }
};

// --- Clearing `online` for one character instead of its whole account ------
// WorldSession::LogoutPlayer marks every character on the leaving account
// offline (CHAR_UPD_ACCOUNT_ONLINE). Handler supplies
//   static bool OnLogout(Player* player);
// called while the leaving player is still in the world; true means "mark only
// this character offline".
//
// Old core: DatabaseScript::OnDatabaseSelectIndexLogout lets a script swap the
// statement, so a true answer swaps in CHAR_UPD_CHAR_OFFLINE for this guid -
// exactly what the adapter did before this header.
// New core: PlayerScript::OnPlayerLogout runs the handler, and
// PlayerScript::OnPlayerCanMarkAccountOffline, called later in the same
// LogoutPlayer, vetoes the account-wide statement and runs the one-character
// update (the same SQL CHAR_UPD_CHAR_OFFLINE held) instead.
template <typename M>
concept HasCanMarkAccountOfflineHook = requires(M& mgr, ObjectGuid guid, uint32 account) {
    mgr.OnPlayerCanMarkAccountOffline(guid, account);
};

template <typename Handler, bool VetoHook = HasCanMarkAccountOfflineHook<ScriptMgr>>
class LogoutOnlineScript;

template <typename Handler>
class LogoutOnlineScript<Handler, false> : public DependentT<DatabaseScript, Handler>
{
    using Base = DependentT<DatabaseScript, Handler>;

public:
    explicit LogoutOnlineScript(char const* name)
        : Base(name, {DependentT<DatabaseHook, Handler>::DATABASEHOOK_ON_DATABASE_SELECT_INDEX_LOGOUT})
    {
    }

    void OnDatabaseSelectIndexLogout(Player* player, uint32& statementIndex,
                                     uint32& statementParam) override
    {
        if (!Handler::OnLogout(player))
            return;
        statementIndex = DependentT<CharacterDatabaseStatements, Handler>::CHAR_UPD_CHAR_OFFLINE;
        statementParam = player->GetGUID().GetCounter();
    }
};

template <typename Handler>
class LogoutOnlineScript<Handler, true> : public DependentT<PlayerScript, Handler>
{
    using Base = DependentT<PlayerScript, Handler>;
    using Hook = DependentT<PlayerHook, Handler>;

public:
    explicit LogoutOnlineScript(char const* name)
        : Base(name, {Hook::PLAYERHOOK_ON_LOGOUT, Hook::PLAYERHOOK_CAN_MARK_ACCOUNT_OFFLINE})
    {
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!Handler::OnLogout(player))
            return;
        std::lock_guard<std::mutex> guard(_lock);
        _characterOnly.insert(player->GetGUID().GetCounter());
    }

    bool OnPlayerCanMarkAccountOffline(ObjectGuid guid, uint32 /*accountId*/) override
    {
        {
            std::lock_guard<std::mutex> guard(_lock);
            if (!_characterOnly.erase(guid.GetCounter()))
                return true;
        }
        CharacterDatabase.Execute("UPDATE characters SET online = 0 WHERE guid = {}",
                                  guid.GetCounter());
        return false;
    }

private:
    std::mutex _lock;
    std::set<ObjectGuid::LowType> _characterOnly;
};
}  // namespace OverseerCompat

#endif  // MOD_OVERSEER_CORE_COMPAT_H
