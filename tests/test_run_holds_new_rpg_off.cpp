/*
 * A run inside its dungeon holds `new rpg` off the members in there (#1210).
 *
 * Measured on the dev realm: the dungeon finder took a family into its
 * dungeon, all five of it, and within seconds the leader was outside again.
 * He had been handed `new rpg` back one poll before the finder queue, and the
 * first rpg tick inside judged him stuck on counters from before the run and
 * teleported him to his old rpg destination outside. The census then found
 * nobody inside and ended the run 'emptied', which was right for a run that
 * had emptied. The fix is the hold: while the run is STAGED_INSIDE or
 * CLEARING, a member on the run's map does not carry `new rpg`.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace OverseerDecisions;

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

std::string Slice(std::string const& source, std::string const& start, std::string const& end)
{
    std::string::size_type const first = source.find(start);
    if (first == std::string::npos)
        return std::string();
    std::string::size_type const last = source.find(end, first + start.size());
    if (last == std::string::npos)
        return std::string();
    return source.substr(first, last - first);
}

}  // namespace

int main()
{
    // THE DECISION. The leader just taken in by the finder: the run is staged
    // inside and he stands on its map, so he does not carry `new rpg`.
    Check("a run staged inside holds `new rpg` off the leader on its map",
          RunHoldsNewRpgOff(true, true));
    // A member outside the map is walked back in under an escort's `new rpg`,
    // so the hold leaves it alone.
    Check("a member outside the run's map keeps whatever walks it back in",
          !RunHoldsNewRpgOff(true, false));
    // Gathering, entering, exiting, or no run at all: `new rpg` is given back.
    Check("a run that is not staged inside or clearing holds nothing",
          !RunHoldsNewRpgOff(false, true));
    Check("no run and no dungeon map holds nothing", !RunHoldsNewRpgOff(false, false));

    // THE WIRING.
    std::ifstream file("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable (run from the repo root)", file.good());
    std::string const source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    std::string const reader = Slice(source, "bool RunHoldsNewRpgOff(Player* p) const",
                                     "bool KeepNewRpgOffInsideTheRun(");
    Check("the adapter asks the decision", reader.find("OverseerDecisions::RunHoldsNewRpgOff(") !=
                                               std::string::npos);
    Check("the hold covers STAGED_INSIDE",
          reader.find("DungeonRunPhase::StagedInside") != std::string::npos);
    Check("the hold covers CLEARING", reader.find("DungeonRunPhase::Clearing") != std::string::npos);
    Check("the hold is read from the live coordinator, not the per-member snapshot",
          reader.find("_dungeonRunCoordinators.find(") != std::string::npos &&
              reader.find("_familyRunPhaseByMember") == std::string::npos);
    Check("on the run's map means the portal's inside map",
          reader.find("portal->insideMapId") != std::string::npos);

    std::string const keep = Slice(source, "void TakeNewRpgOffForTheRun(",
                                   "// THE RUN TIMELINE (overseer_dungeon_run_event)");
    Check("the hold takes `new rpg` off",
          keep.find("ChangeStrategy(\"-new rpg\"") != std::string::npos);
    Check("the hold reads the strategy back",
          keep.rfind("HasStrategy(\"new rpg\"") > keep.find("ChangeStrategy(\"-new rpg\""));

    // The leader's grant: the hold is asked before the grant and turns it off.
    std::string const leader = Slice(source, "KeepLeaderStayHonest(leader, leaderName, leaderAI, held);",
                                     "leads but did not carry `new rpg` - granting it");
    std::string::size_type const asked = leader.find("KeepNewRpgOffInsideTheRun(leader, leaderAI)");
    std::string::size_type const grant = leader.find("if (mayCarry && !leaderAI->HasStrategy(\"new rpg\"");
    Check("the leader's poll asks the hold", asked != std::string::npos);
    Check("the leader's grant comes after the hold", grant != std::string::npos && asked < grant);
    Check("the hold withholds the leader's grant",
          leader.find("if (runHoldsNewRpgOff)\n                mayCarry = false;") !=
              std::string::npos);
    Check("the in-town strip does not also fire for a run inside",
          leader.find("&&\n                !runHoldsNewRpgOff && !HeldAfterRevival(leaderName)") != std::string::npos);

    // The finder take-in takes it off on the same poll, not thirty seconds later.
    std::string const takeIn = Slice(source, "StageInsideAfterFinder(coord);",
                                     "return FinderRungOutcome::Inside;");
    Check("the finder take-in asks the decision for everybody it put inside",
          takeIn.find("for (std::string const& name : names)") != std::string::npos &&
              takeIn.find("OverseerDecisions::RunHoldsNewRpgOff(") != std::string::npos &&
              takeIn.find("p->GetMapId() == portal.insideMapId") != std::string::npos);
    Check("the finder take-in takes `new rpg` off on the same poll",
          takeIn.find("TakeNewRpgOffForTheRun(p, GET_PLAYERBOT_AI(p))") != std::string::npos);

    // The cut-off follower's grant: a follower inside the run is not handed it.
    std::string const follower = Slice(source, "bool const cutOffIdle =", "bool const cutOffRoams");
    Check("a cut-off follower inside the run is not granted `new rpg`",
          follower.find("!KeepNewRpgOffInsideTheRun(p, botAI)") != std::string::npos);

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("run holds new rpg off: all checks passed\n");
    return 0;
}
