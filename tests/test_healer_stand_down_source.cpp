/*
 * Source contract for the dungeon-resurrector stand-down (mod-overseer#1026).
 *
 * The decision is proved in test_healer_stand_down.cpp. What only the adapter
 * can get wrong is whether the healerExpected lambda ASKS it, and whether
 * anything else ends the wait early. #753 did exactly that: the lambda stamped
 * a `last_spirit_healer` mark on the first poll of the wait and read it on the
 * next, so a member with a RecoveryMark stopped waiting one second into the
 * death. A decision nothing feeds, or one the adapter overrides, is the
 * failure this repository has shipped before, so the wiring is pinned as text.
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

    std::size_t const start = text.find("auto const healerExpected = [&](int64 deadSeconds)");
    if (start == std::string::npos)
        return Fail("cannot find the healerExpected lambda");
    std::size_t const end = text.find("\n            };\n", start);
    if (end == std::string::npos)
        return Fail("cannot find the end of the healerExpected lambda");
    std::string const lambda = text.substr(start, end - start);

    std::size_t const asks = lambda.find("OverseerDecisions::HealerStandsDown(");
    if (asks == std::string::npos)
        return Fail("healerExpected must decide the stand-down with HealerStandsDown");

    // Nothing before the decision may return: an early `return false` above it
    // is how #753 cut the wait short.
    if (lambda.substr(0, asks).find("return") != std::string::npos)
        return Fail("healerExpected returns before it asks HealerStandsDown (#1026)");

    if (text.find("last_spirit_healer") != std::string::npos)
        return Fail("the #753 last_spirit_healer mark is back: it ends the resurrector wait "
                    "one second into a death (#1026)");

    std::cout << "healer stand-down source: all checks passed\n";
    return EXIT_SUCCESS;
}
