/*
 * Source contract for the main-hand cast's target (#1304).
 *
 * `cast-spell-on-mainhand` (#1264) casts a known spell at the equipped weapon.
 * It sends CMSG_CAST_SPELL with TARGET_FLAG_ITEM, and the core reads the target
 * that follows with SpellCastTargets::Read, which expects ONE PACKED guid there.
 * The verb wrote the raw 8-byte guid, so the core read the wrong bytes. The
 * packet is built in the adapter, which the decisions check does not compile,
 * so the bytes it writes are pinned as text, as the other source contracts are.
 * Run against a cast that writes the raw guid, this test fails.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const flag = "raw << uint32(TARGET_FLAG_ITEM);";
    std::size_t const flagAt = text.find(flag);
    if (flagAt == std::string::npos)
        return Fail("the main-hand cast must send TARGET_FLAG_ITEM");
    if (text.find(flag, flagAt + flag.size()) != std::string::npos)
        return Fail("TARGET_FLAG_ITEM must appear once: the main-hand cast");

    std::size_t const sentAt = text.find("session->HandleCastSpellOpcode(raw);", flagAt);
    if (sentAt == std::string::npos)
        return Fail("the main-hand cast must hand its packet to HandleCastSpellOpcode");
    std::string const target =
        text.substr(flagAt + flag.size(), sentAt - flagAt - flag.size());

    if (target.find("raw << item->GetGUID().WriteAsPacked();") == std::string::npos)
        return Fail("the main-hand cast must write its item target as a packed guid");
    if (target.find("raw << item->GetGUID();") != std::string::npos)
        return Fail("the main-hand cast wrote the raw, unpacked item guid");

    return EXIT_SUCCESS;
}
