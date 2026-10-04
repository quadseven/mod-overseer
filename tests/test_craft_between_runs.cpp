/*
 * A family on a campaign job crafts between runs (2026-10-04).
 *
 * Measured on the dev realm: the family members are every trade's master
 * crafter, they sat on 'town run' or a 'dungeon' job for good, DriveCraft
 * cast only for job 'craft', and nine of ten crafts stood at 1 of 75.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::MayCraftNow;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheDecision()
{
    Check("job craft always crafts", MayCraftNow("craft", true, true, true, true));
    Check("waiting in town crafts", MayCraftNow("town run", false, true, false, false));
    Check("between dungeon runs crafts",
          MayCraftNow("dungeon:shadowfang", false, true, false, false));
    Check("bare dungeon job crafts", MayCraftNow("dungeon", false, true, false, false));
    Check("never inside a run", !MayCraftNow("dungeon:shadowfang", true, true, false, false));
    Check("never in a fight", !MayCraftNow("town run", false, true, true, false));
    Check("never on the move", !MayCraftNow("town run", false, true, false, true));
    Check("never dead", !MayCraftNow("town run", false, false, false, false));
    Check("questing does not craft", !MayCraftNow("quest", false, true, false, false));
    Check("a dungeon-looking prefix is not enough",
          !MayCraftNow("dungeons", false, true, false, false));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("DriveCraft asks the decision",
          source.find("OverseerDecisions::MayCraftNow(jobIt->second, InDungeonRun(bot),")
              != std::string::npos);
    Check("and no longer requires job craft alone",
          source.find("if (jobIt == jobs.end() || jobIt->second != \"craft\")")
              == std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_craft_between_runs\n");
    return 0;
}
