/*
 * A near walk whose straight first leg gives no step goes by the travel
 * survey instead (2026-10-03).
 *
 * Measured on the dev realm: every guild gear walk ended "the ground toward
 * the vendor does not hold" before taking a step, 502 yards down from the
 * Darnassus terraces and 413 yards across the Durotar cliffs. Both were under
 * the near cap, so they only ever tried the straight line.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::NearWalkFallsBackToRoute;

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
    Check("no straight step on a continent takes the road",
          NearWalkFallsBackToRoute(false, true));
    Check("a straight step that holds walks straight", !NearWalkFallsBackToRoute(true, true));
    Check("off the continents there is no road to take",
          !NearWalkFallsBackToRoute(false, false));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the step is computed without moving anybody",
          source.find("static bool MailWalkLegStep(Player* who, MailWalkEvidence& ev, "
                      "WorldPosition& step)") != std::string::npos);
    Check("the near walk probes its first leg before the hold",
          source.find("D::NearWalkFallsBackToRoute(MailWalkLegStep(who, ev, probe),")
              != std::string::npos);
    std::size_t const probe = source.find("D::NearWalkFallsBackToRoute(MailWalkLegStep");
    Check("and the walk's hold is placed after the probe",
          probe != std::string::npos &&
              source.find("HoldStillAndReport(who, ev.character, MAIL_WALK_HOLD_VERB", probe)
                  != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_near_walk_route_fallback\n");
    return 0;
}
