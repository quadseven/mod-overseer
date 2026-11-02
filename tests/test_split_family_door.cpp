/*
 * A family split at a dungeon door has a way out.
 *
 * One member's bags held the whole family at the door while four stood inside,
 * so the fifth was refused at the entrance and the run closed `split_failed`
 * every 15 minutes. A leader that revived outdoors left the party unarmed, and
 * the arming hold returned before the leader-away bound was asked.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::ClearingArmGate;
using OverseerDecisions::DecideClearingArmGate;
using OverseerDecisions::FamilyDoorHeldForBags;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

int main()
{
    Check("no room, nobody inside: held", FamilyDoorHeldForBags(true, false));
    Check("no room, a run with a member inside: not held", !FamilyDoorHeldForBags(true, true));
    Check("room: not held", !FamilyDoorHeldForBags(false, false));
    Check("room, inside: not held", !FamilyDoorHeldForBags(false, true));

    time_t const now = 100000;
    long const bound = 180;
    Check("armed wins over an away leader",
          DecideClearingArmGate(true, now - 1000, now, bound) == ClearingArmGate::Armed);
    Check("not armed, leader here: held",
          DecideClearingArmGate(false, 0, now, bound) == ClearingArmGate::HoldNotArmed);
    Check("not armed, leader away inside the bound: held",
          DecideClearingArmGate(false, now - bound, now, bound) == ClearingArmGate::HoldNotArmed);
    Check("not armed, leader away past the bound: the run ends",
          DecideClearingArmGate(false, now - bound - 1, now, bound) ==
              ClearingArmGate::LeaderAwayEnds);

    // The door mark and the arming gate are what the module actually calls, and
    // the bound is timed ahead of the arming verdict.
    std::string const source = Source();
    Check("source readable", !source.empty());
    Check("the door mark asks FamilyDoorHeldForBags",
          source.find("MarkFamilyBagHeld(family, members,\n"
                      "                              OverseerDecisions::FamilyDoorHeldForBags(") !=
              std::string::npos);
    std::size_t const stamp = source.find("coord.leaderAwaySince = clearingNow;");
    std::size_t const gate = source.find("armGate == OverseerDecisions::ClearingArmGate::HoldNotArmed");
    Check("the absence is timed before the arming hold",
          stamp != std::string::npos && gate != std::string::npos && stamp < gate);

    if (failures)
        return 1;
    std::printf("test_split_family_door: ok\n");
    return 0;
}
