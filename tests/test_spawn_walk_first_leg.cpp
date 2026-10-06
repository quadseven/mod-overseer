/*
 * The drop guard of a walk to a creature spawn reads the first leg (2026-10-06).
 *
 * Measured on the dev realm: of 163 class quest walk-to-spawn rows, 43 ended
 * "the first step toward the creature goes over a drop" with no step taken, and
 * not one of them was under 695 yards. The guard probed the whole distance to
 * the spawn from a character standing on a plateau or in a capital, was refused
 * for every spawn far enough off, and came before the travel survey was asked
 * for the road down. The walk takes its first step along the first leg, so
 * that leg is what the guard reads.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::SpawnWalkFirstStepRefusal;
namespace S = OverseerDecisions::SpawnWalkRefusal;

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
    Check("a creature spawn whose first leg has no step is refused",
          std::strcmp(SpawnWalkFirstStepRefusal(true, false), S::SpawnFirstStepDrop) == 0);
    Check("a creature spawn whose first leg has a step may start",
          *SpawnWalkFirstStepRefusal(true, true) == '\0');
    Check("a gameobject spawn is never refused for a drop",
          *SpawnWalkFirstStepRefusal(false, false) == '\0');
    Check("the words are the ones the bridge has always read",
          std::strcmp(S::SpawnFirstStepDrop,
                      "the first step toward the creature goes over a drop") == 0);
}

void TheAdapterReadsTheFirstLeg()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the guard no longer probes the whole way to the spawn",
          source.find("GroundedStep(who, WorldPosition(ev.mapId, ev.boxX, ev.boxY, ev.boxZ), "
                      "step)") == std::string::npos);
    std::size_t const fallback = source.find("D::NearWalkFallsBackToRoute(MailWalkLegStep");
    std::size_t const guard = source.find("D::SpawnWalkFirstLegRefusal(");
    std::size_t const hold =
        source.find("HoldStillAndReport(who, ev.character, MAIL_WALK_HOLD_VERB, ev.hold,",
                    guard == std::string::npos ? 0 : guard);
    Check("the guard exists", guard != std::string::npos);
    Check("it asks the first leg", source.find("MailWalkLegStep(who, ev, firstLeg)") !=
                                       std::string::npos);
    Check("it comes after the near walk has had its chance to take the road",
          fallback != std::string::npos && guard != std::string::npos && guard > fallback);
    Check("and before the hold is placed", hold != std::string::npos && hold > guard);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterReadsTheFirstLeg();
    if (failures)
        return 1;
    std::printf("ok test_spawn_walk_first_leg\n");
    return 0;
}
