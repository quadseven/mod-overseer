/*
 * mod-playerbots config keys are read under the spelling the linked
 * mod-playerbots reads (#1068).
 *
 * mod-playerbots #2854 renamed every AiPlayerbot.<Name> key to
 * Playerbots.<Name>, and the newer mod-playerbots does not read the old
 * spelling at all. The natural-guild gate (Playerbots.NaturalGuild after the
 * rename, AiPlayerbot.NaturalGuild before it) is read by a deployment's
 * mod-playerbots patch AND by this module, and the two must agree: when the
 * realm overlay moved to Playerbots.NaturalGuild and the module still read the
 * old key, the gate read empty and every naturalize reset was refused.
 *
 * The decision half is pure and asked directly. The adapter half needs a
 * worldserver, so its wiring is pinned as text: no mod-playerbots key is a
 * string literal in the adapter, the spelling comes from the compat header's
 * type test, and the refusal names the key that was read.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using OverseerDecisions::NameListHas;
using OverseerDecisions::NaturalizeRefusal;
using OverseerDecisions::NaturalizeRefusalSaid;
using OverseerDecisions::ParseNameList;
using OverseerDecisions::PlayerbotsConfigKey;
using OverseerDecisions::PlayerbotsOptionRead;
using OverseerDecisions::ReadPlayerbotsOption;

namespace
{
int failures = 0;

void Check(std::string const& what, bool ok)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

void Require(bool ok, std::string const& what)
{
    Check(what, ok);
}

std::string Read(char const* path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text without its whole-line // comments, so prose that names a key does
// not count as a read.
std::string Code(std::string const& text)
{
    std::string out;
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

// What the adapter's gate reads when the realm config holds `newKey` under
// Playerbots.NaturalGuild and `oldKey` under AiPlayerbot.NaturalGuild, for a
// mod-playerbots that reads the `renamed` spelling.
PlayerbotsOptionRead GateRead(bool renamed, std::string const& newKey, std::string const& oldKey)
{
    auto valueOf = [&](std::string const& key)
    {
        if (key == "Playerbots.NaturalGuild")
            return newKey;
        if (key == "AiPlayerbot.NaturalGuild")
            return oldKey;
        return std::string();
    };
    return ReadPlayerbotsOption(valueOf(PlayerbotsConfigKey(renamed, "NaturalGuild")),
                                valueOf(PlayerbotsConfigKey(!renamed, "NaturalGuild")));
}

void TestKeySpelling()
{
    Check("after mod-playerbots #2854 the key is Playerbots.NaturalGuild",
          PlayerbotsConfigKey(true, "NaturalGuild") == "Playerbots.NaturalGuild");
    Check("before it the key is AiPlayerbot.NaturalGuild",
          PlayerbotsConfigKey(false, "NaturalGuild") == "AiPlayerbot.NaturalGuild");
}

void TestGateReadsTheKeyTheFactoryReads()
{
    // The bug: the overlay moved to the new key with the image, and the gate
    // read the old one, got "", and refused every reset.
    PlayerbotsOptionRead const moved = GateRead(true, "Cave,Bonkers", "");
    Check("with only Playerbots.NaturalGuild set, the renamed gate names the guild",
          NameListHas(ParseNameList(moved.value), "Bonkers"));
    Check("and says nothing about the other spelling", !moved.otherSpellingOnly);

    PlayerbotsOptionRead const old = GateRead(false, "", "Cave,Bonkers");
    Check("on the older mod-playerbots, AiPlayerbot.NaturalGuild still names the guild",
          NameListHas(ParseNameList(old.value), "Cave"));
    Check("and says nothing about the other spelling", !old.otherSpellingOnly);

    // The reverse: an overlay left on the spelling the factory ignores. The
    // factory's gate is empty, so this one must be too, and must say why.
    PlayerbotsOptionRead const stale = GateRead(true, "", "Cave,Bonkers");
    Check("an overlay left on AiPlayerbot.NaturalGuild does not open the renamed gate",
          ParseNameList(stale.value).empty());
    Check("and is said", stale.otherSpellingOnly);

    PlayerbotsOptionRead const early = GateRead(false, "Cave", "");
    Check("an overlay moved ahead of the image does not open the older gate",
          ParseNameList(early.value).empty());
    Check("and is said", early.otherSpellingOnly);

    PlayerbotsOptionRead const both = GateRead(true, "Cave", "Bonkers");
    Check("with both set, the key the factory reads wins",
          NameListHas(ParseNameList(both.value), "Cave") &&
              !NameListHas(ParseNameList(both.value), "Bonkers"));
    Check("and nothing is said", !both.otherSpellingOnly);

    PlayerbotsOptionRead const blank = GateRead(true, "  ", " ");
    Check("blank values are absent, not a stranded overlay", !blank.otherSpellingOnly);
}

void TestRefusalNamesTheKeyRead()
{
    char const* const renamed = NaturalizeRefusalSaid(NaturalizeRefusal::PlayerbotsGateOff, true);
    char const* const older = NaturalizeRefusalSaid(NaturalizeRefusal::PlayerbotsGateOff, false);
    Check("the renamed gate's refusal names Playerbots.NaturalGuild",
          std::strstr(renamed, "Playerbots.NaturalGuild") != nullptr &&
              std::strstr(renamed, "AiPlayerbot.") == nullptr);
    Check("the older gate's refusal names AiPlayerbot.NaturalGuild",
          std::strstr(older, "AiPlayerbot.NaturalGuild") != nullptr);
}

void TestAdapterWiring()
{
    std::string const adapterText = Read("src/mod_overseer.cpp");
    std::string const compat = Read("src/overseer_core_compat.h");
    if (adapterText.empty() || compat.empty())
    {
        Require(false, "cannot read src/mod_overseer.cpp or src/overseer_core_compat.h "
                     "(run from the repo root)");
        return;
    }
    std::string const adapter = Code(adapterText);

    // EVERY mod-playerbots key, not only NaturalGuild: a literal of either
    // spelling is right on one pin set and silently empty on the other.
    Require(adapter.find("\"AiPlayerbot.") == std::string::npos,
          "the adapter must not read an AiPlayerbot.* key by literal; use PlayerbotsConfigKey");
    // "Playerbots.h" is the module's header, not a key.
    bool newSpellingLiteral = false;
    for (std::size_t at = adapter.find("\"Playerbots."); at != std::string::npos;
         at = adapter.find("\"Playerbots.", at + 1))
        if (adapter.compare(at, 14, "\"Playerbots.h\"") != 0)
            newSpellingLiteral = true;
    Require(!newSpellingLiteral,
          "the adapter must not read a Playerbots.* key by literal; use PlayerbotsConfigKey");

    Require(compat.find("inline constexpr bool PlayerbotsKeysRenamed") != std::string::npos &&
              compat.find("requires(Config const& config) { config.spellDistance; }") !=
                  std::string::npos,
          "overseer_core_compat.h must tell the spelling from the type, not assume it");
    Require(adapter.find("OverseerCompat::PlayerbotsKeysRenamed<PlayerbotAIConfig>") !=
              std::string::npos,
          "the adapter must ask the linked mod-playerbots which spelling it reads");
    Require(adapter.find("PlayerbotsConfigKey(PLAYERBOTS_KEYS_RENAMED, \"NaturalGuild\")") !=
              std::string::npos,
          "the natural-guild gate must read the key the linked mod-playerbots reads");
    Require(adapter.find("NaturalizeRefusalSaid(refusal)") == std::string::npos &&
              adapter.find("NaturalizeRefusalSaid(would)") == std::string::npos,
          "every naturalize refusal must name the key that was read");
}
}  // namespace

int main()
{
    TestKeySpelling();
    TestGateReadsTheKeyTheFactoryReads();
    TestRefusalNamesTheKeyRead();
    TestAdapterWiring();
    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_playerbots_config_keys: ok\n";
    return EXIT_SUCCESS;
}
