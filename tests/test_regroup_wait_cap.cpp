/*
 * Nobody stands still for a member a quarter of an hour away.
 *
 * Measured on the dev realm, 2026-09-26 03:33 UTC, while the operator watched:
 * the Alliance leader was held still to regroup "for at most 1200s" because a
 * member was 7,391 yards behind. At run speed that is a quarter of an hour of
 * a leader standing in a field. RegroupLimits::maxWaitYards bounds how far
 * back a member may be and still be waited for standing still; past it the
 * member walks back under its catch-up aim while the family carries on (or the
 * leader goes back for it, or the family meets at the inn).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::FamilyRegroup;
using OverseerDecisions::ReadFamilyRegroup;
using OverseerDecisions::ReadRegroupClaim;
using OverseerDecisions::RegroupCarriedOnWithout;
using OverseerDecisions::RegroupClaim;
using OverseerDecisions::RegroupLimits;
using OverseerDecisions::RegroupMember;

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

RegroupMember Behind(char const* name, float yards)
{
    RegroupMember m;
    m.name = name;
    m.seen = true;
    m.sameMap = true;
    m.alive = true;
    m.yards = yards;
    return m;
}

RegroupLimits const CAPPED{160.f, 25.f, 1200.f};
RegroupLimits const UNCAPPED{160.f, 25.f};

}  // namespace

int main()
{
    Check("a member within the cap is waited for",
          ReadRegroupClaim(Behind("Grog", 900.f), CAPPED, false) == RegroupClaim::Rejoining);
    Check("the measured member is too far to wait for",
          ReadRegroupClaim(Behind("Ugga", 7391.f), CAPPED, false) ==
              RegroupClaim::TooFarToWaitFor);
    Check("with no cap nothing changes",
          ReadRegroupClaim(Behind("Ugga", 7391.f), UNCAPPED, false) == RegroupClaim::Rejoining);

    std::vector<RegroupMember> family{Behind("Ugga", 7391.f)};
    FamilyRegroup verdict = ReadFamilyRegroup(family, CAPPED, false);
    Check("the leader does not hold for the measured member", !verdict.wait);
    Check("that member is counted as not waited for", verdict.notWaitedFor == 1);
    std::string const said = RegroupCarriedOnWithout(family, CAPPED, false);
    Check("and the line says why", said.find("too far back") != std::string::npos);

    family.push_back(Behind("Grog", 400.f));
    verdict = ReadFamilyRegroup(family, CAPPED, false);
    Check("a nearer straggler is still waited for", verdict.wait && verdict.waitingFor == "Grog");

    // A member already waited for that falls back past the cap is let go too:
    // the wait ends instead of running its twenty minutes.
    verdict = ReadFamilyRegroup({Behind("Grog", 1500.f)}, CAPPED, true);
    Check("a wait ends when the member falls past the cap", !verdict.wait);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("nobody stands still for a member a quarter of an hour away\n");
    return EXIT_SUCCESS;
}
