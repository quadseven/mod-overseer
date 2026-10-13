/*
 * A logout marks only its own character offline while account-mates stay in
 * the world.
 *
 * The core's logout clears the online flag for the whole account. Under
 * playerbots many characters share one account, so a random bot's logout
 * marked always-online guild members offline until their next save: on the
 * dev realm 42 characters in the world read online 0 (wow-overseer#874).
 * These pin which statement a logout gets.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::LogoutOfflineScope;
using OverseerDecisions::LogoutScope;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    Check(LogoutOfflineScope(0) == LogoutScope::Account,
          "a lone session keeps the core's account-wide statement");
    Check(LogoutOfflineScope(1) == LogoutScope::CharacterOnly,
          "one account-mate in the world leaves only the leaving character offline");
    Check(LogoutOfflineScope(41) == LogoutScope::CharacterOnly,
          "many account-mates in the world leave only the leaving character offline");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
