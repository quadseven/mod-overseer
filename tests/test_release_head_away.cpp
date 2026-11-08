/*
 * A released stand-in guest is taken out of the family's group even when the
 * head is not in the world for a poll (#1450).
 *
 * Seen live (2026-11-08, Blackrock Depths): the stand-in ended while the head
 * was mid-hearth. SweepLeavingGuests found the family's group only through the
 * head's player, so with the head absent it had no group, tried no leave and
 * dropped the guest from its release list as if it had left. The guest stayed
 * in the party on the dungeon map, grouped, and so was never sent home by the
 * bridge's stranded-member pass, for 46 minutes.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::GuestGroupIsFamilys;

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

    // headInWorld, inHeadsGroup, groupLedByHead
    check(GuestGroupIsFamilys(false, false, true),
          "with the head out of the world, a guest in a group he leads is still in the "
          "family's group and must be taken out of it");
    check(!GuestGroupIsFamilys(false, false, false),
          "with the head out of the world, a guest in some other group, or none, is not "
          "in the family's group");
    check(GuestGroupIsFamilys(true, true, true),
          "with the head in the world, a guest in his group is in the family's group");
    check(!GuestGroupIsFamilys(true, false, true),
          "with the head in the world only his own group counts, whatever a group's "
          "leader is called");
    check(!GuestGroupIsFamilys(true, false, false),
          "with the head in the world, a guest outside his group is out of it");

    if (failures)
        return 1;
    std::printf("test_release_head_away: all checks passed\n");
    return 0;
}
