/*
 * A run that reassembles with its dungeon module off is armed again (#1500).
 *
 * MEASURED ON THE DEV REALM 2026-11-11, A BLACKROCK DEPTHS RUN. The dungeon
 * module switched itself off on a party wait at 17:39Z (a stand-in paladin with
 * no drink was short of mana for 180 seconds). A member then died, was released
 * and the run left CLEARING to reassemble. The re-arm that exists for that stop
 * (#1308) lives in the clearing watchdog, which a dead member holds out, so
 * nobody armed the module again and four living members stood inside for about
 * sixteen minutes until the split ceiling closed the run. Armed again, the same
 * four fought normally.
 *
 * Pinned here: the decision, and the adapter contract (the reassembly phase asks
 * it before the split ceiling, shares the watchdog's counter, and forgets every
 * member's `dc on` record).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, run from the
 * repository root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::StagedInsideBrainRearms;

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

std::string const WAIT =
    "Dungeon clear stopped: the party never became ready after 180s "
    "(Waiting on Frakk (low mana)).";
constexpr unsigned BOUND = 3;
constexpr time_t GAP = OverseerDecisions::STAGED_INSIDE_REARM_GAP_SECONDS;
}  // namespace

int main()
{
    Check("a party-wait stop with four alive inside is armed again",
          StagedInsideBrainRearms(WAIT, 0, BOUND, 4, GAP));
    Check("and with five", StagedInsideBrainRearms(WAIT, 0, BOUND, 5, GAP));
    Check("the last allowed re-arm still goes", StagedInsideBrainRearms(WAIT, BOUND - 1, BOUND, 4, GAP));
    Check("a spent bound does not", !StagedInsideBrainRearms(WAIT, BOUND, BOUND, 4, GAP));
    Check("three alive inside is a party too small to clear without a rez",
          !StagedInsideBrainRearms(WAIT, 0, BOUND, 3, GAP));
    Check("any other stop text is left alone",
          !StagedInsideBrainRearms("All bosses cleared!", 0, BOUND, 4, GAP));
    Check("no stop text is left alone", !StagedInsideBrainRearms("", 0, BOUND, 4, GAP));
    Check("a re-arm sent a moment ago is given time to take",
          !StagedInsideBrainRearms(WAIT, 1, BOUND, 4, GAP - 1));
    Check("the gap is shorter than the split ceiling it must beat",
          GAP < 5 * 60);

    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp (run from the repository root)\n");
        return 1;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::size_t const rearm = text.find("StagedInsideBrainRearms(");
    std::size_t const ceiling = text.find("FailSplitInside(coord, leaderName, members");
    Check("the reassembly phase asks the decision", rearm != std::string::npos);
    Check("and asks it before the split ceiling",
          rearm != std::string::npos && ceiling != std::string::npos && rearm < ceiling);
    if (rearm != std::string::npos)
    {
        std::string const after = text.substr(rearm, 1600);
        Check("it shares the watchdog's re-arm counter",
              after.find("++coord.clearRearms") != std::string::npos);
        Check("it forgets every member's dc on record",
              after.find("_dcOnIssued.erase(state.name)") != std::string::npos);
        Check("it stamps its own clock", after.find("coord.stagedRearmAt") != std::string::npos);
    }

    if (failures)
        return 1;
    std::printf("staged inside re-arm: all checks passed\n");
    return 0;
}
