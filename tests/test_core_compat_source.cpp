/*
 * Source contract for the two-pin-set adapter (wow-overseer#826).
 *
 * src/mod_overseer.cpp compiles against the pins in build.yml AND against the
 * upstream heads the catch-up (infra#5655) moves to. Every API that differs
 * between them is reached through src/overseer_core_compat.h. The compile legs
 * prove each pin set in CI, but they take minutes and the new-heads leg is not
 * required; this pins the rule as text, so a direct call to an API one of the
 * two trees lacks fails here in seconds instead.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
std::string Read(char const* path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text without its whole-line // comments, so prose that names an old API
// (the history in a comment block) does not count as a call.
std::string Code(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::size_t const first = text.find_first_not_of(" \t", start);
        bool const comment = first < end && text.compare(first, 2, "//") == 0;
        if (!comment)
            out.append(text, start, end - start);
        out += '\n';
        start = end + 1;
    }
    return out;
}

int failures = 0;

void Check(bool ok, std::string const& what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
}  // namespace

int main()
{
    std::string const adapterText = Read("src/mod_overseer.cpp");
    std::string const compat = Read("src/overseer_core_compat.h");
    if (adapterText.empty() || compat.empty())
    {
        std::cerr << "cannot read src/mod_overseer.cpp or src/overseer_core_compat.h "
                     "(run from the repo root)\n";
        return EXIT_FAILURE;
    }
    std::string const adapter = Code(adapterText);

    Check(adapter.find("#include \"overseer_core_compat.h\"") != std::string::npos,
          "the adapter includes overseer_core_compat.h");

    // Each of these exists in only one of the two pinned trees.
    for (char const* gone : {
             "->IsBot()",                  // core #27533: IsHeadless() in the newer core
             "->IsHeadless()",             // and IsBot() in the older one
             "IsInChannel(",               // Player::IsInChannel: older core only
             "public PlayerbotScript",     // PlayerbotScript: older core only
             "OnPlayerbotPacketSent",      // its packet hook
             "OnPacketSent(WorldSession",  // ServerScript::OnPacketSent: newer core only
             "OnDatabaseSelectIndexLogout",
             "DATABASEHOOK_ON_DATABASE_SELECT_INDEX_LOGOUT",
             "CHAR_UPD_CHAR_OFFLINE",
             "OnPlayerCanMarkAccountOffline",
             "sPlayerbotAIConfig.spellDistance",  // mod-playerbots #2854 renames
             "sPlayerbotAIConfig.SpellDistance",
             "sPlayerbotAIConfig.randomBotAccountPrefix",
             "sPlayerbotAIConfig.RandomBotAccountPrefix",
             "sPlayerbotAIConfig.randomClassSpecIndex",
             "sPlayerbotAIConfig.RandomClassSpecIndex",
             "sPlayerbotAIConfig.lootRollDisenchant",
             "sPlayerbotAIConfig.LootRollDisenchant",
         })
        Check(adapter.find(gone) == std::string::npos,
              std::string("the adapter must reach this through overseer_core_compat.h, not "
                          "directly: ") + gone);

    // The compat header answers each one by asking the type, old API first.
    for (char const* asks : {
             "requires { session.IsBot(); }",
             "requires { player.IsInChannel(channel); }",
             "requires { config.spellDistance; }",
             "requires { config.randomBotAccountPrefix; }",
             "requires { config.randomClassSpecIndex; }",
             "requires { config.lootRollDisenchant; }",
             "concept HasServerPacketSentHook",
             "concept HasCanMarkAccountOfflineHook",
         })
        Check(compat.find(asks) != std::string::npos,
              std::string("overseer_core_compat.h must detect the API, not assume it: ") + asks);

    // Both hook users go through the selecting adapters.
    Check(adapter.find("public OverseerCompat::PacketSentScript<OverseerFinderPackets>") !=
              std::string::npos,
          "the finder's packet hook goes through OverseerCompat::PacketSentScript");
    Check(adapter.find("public OverseerCompat::LogoutOnlineScript<OverseerLogoutOnline>") !=
              std::string::npos,
          "the logout online-scope hook goes through OverseerCompat::LogoutOnlineScript");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "the adapter reaches every API that differs between the pin sets through "
                 "overseer_core_compat.h\n";
    return EXIT_SUCCESS;
}
