/*
 * A character the module lets go of the head - a released stand-in guest, or
 * the member sitting out - has its master cleared only once it is out of the
 * head's group (#1437).
 *
 * Upstream playerbots hands a grouped bot with no master the group leader
 * again on its next AI tick and whispers him "Hello, I follow you!". A clear
 * made while the character is still in the head's group (a dungeon finder
 * group the module may not leave, or a refused leave) undoes itself at once,
 * and made every 5-second poll it was that whisper every 5 seconds.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::MayClearMasterOnRelease;

int main()
{
    int failures = 0;
    auto check = [&](bool ok, char const* what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    };

    check(!MayClearMasterOnRelease(true),
          "a character still in the head's group keeps its master, or upstream re-adopts "
          "the head with a hello whisper every poll");
    check(MayClearMasterOnRelease(false),
          "a character out of the head's group stops following him");

    if (failures)
        return 1;
    std::printf("test_release_master: all checks passed\n");
    return 0;
}
