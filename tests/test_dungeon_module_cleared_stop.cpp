/*
 * A dungeon module that stopped because it counted every boss down finished
 * the run (#1322).
 *
 * Run 1280737 on map 209 ended 'stalled' with the module's own stop text "All
 * bosses cleared!", because the CLEARING stall path read every switched-off
 * module as a failure. Only that stop is a clear; every other reason the
 * module gives for switching itself off keeps the stalled ending.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonModuleStopIsCleared;

namespace
{

int failures = 0;

void Check(char const* what, std::string const& reason, bool want)
{
    if (DungeonModuleStopIsCleared(reason) == want)
        return;
    std::printf("FAIL %s: '%s' should%s classify as cleared\n", what, reason.c_str(),
                want ? "" : " not");
    ++failures;
}

void TheAllBossesClearedStopIsAClear()
{
    Check("run 1280737", "All bosses cleared!", true);
    Check("with the module's prefix", "Dungeon clear stopped: All bosses cleared!", true);
    Check("with surrounding blanks", "  All bosses cleared!\r\n", true);
}

void EveryOtherStopIsNot()
{
    Check("failed event", "failed at step 3 of the pull", false);
    Check("nothing reachable", "nothing left it could reach", false);
    Check("empty", "", false);
    Check("prefix only", "Dungeon clear stopped:  ", false);
    Check("party wait",
          "Dungeon clear stopped: the party never became ready after 180s (Waiting on Uzza "
          "(low mana))",
          false);
    Check("a sentence that contains it", "Not All bosses cleared! before the timer", false);
}

}  // namespace

int main()
{
    TheAllBossesClearedStopIsAClear();
    EveryOtherStopIsNot();
    if (failures == 0)
        std::printf("PASS: dungeon module cleared stop\n");
    return failures == 0 ? 0 : 1;
}
