/*
 * A far walk may start in the Blood Elf and Draenei starting lands (2026-10-04).
 *
 * Measured on the dev realm: 24 guild members at levels 11 to 24 stood on map
 * 530's starting isles and 62 walks a day ended "a far walk starts only on the
 * Eastern Kingdoms or Kalimdor". Outland proper stays refused.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::FarWalkAllowedAt;

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
    Check("Azuremyst Isle", FarWalkAllowedAt(530, 3524));
    Check("Bloodmyst Isle", FarWalkAllowedAt(530, 3525));
    Check("the Exodar", FarWalkAllowedAt(530, 3557));
    Check("Eversong Woods", FarWalkAllowedAt(530, 3430));
    Check("Ghostlands", FarWalkAllowedAt(530, 3433));
    Check("Silvermoon City", FarWalkAllowedAt(530, 3487));
    Check("Hellfire Peninsula stays refused", !FarWalkAllowedAt(530, 3483));
    Check("Northrend stays refused", !FarWalkAllowedAt(571, 3524));
    Check("the classic continents still walk", FarWalkAllowedAt(0, 12) && FarWalkAllowedAt(1, 14));
    Check("a dungeon is no continent", !FarWalkAllowedAt(36, 1581));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the far walk asks where the walker stands",
          source.find("D::FarWalkAllowedAt(ev.mapId, who->GetZoneId())") != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_far_walk_starting_lands\n");
    return 0;
}
