/*
 * A travel-column errand to a spawn that exists only on another map is refused
 * for the cool-off, so the quest drive does not re-pick it every few minutes
 * (mod-overseer#1711). The decisions are pure; the wiring half reads
 * src/mod_overseer.cpp (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    Check("a bare spawn id is refused", UnresolvedSpawnIsRefused("12345", false));
    Check("a counter keyword is not (its landing stands it down)",
          !UnresolvedSpawnIsRefused("auctioneer", false));
    Check("an at: point is not (the poison-aim breaker counts it)",
          !UnresolvedSpawnIsRefused("at:0:1,2,3", false));
    Check("an operator's guard is never refused",
          !UnresolvedSpawnIsRefused("12345", true));
    Check("an empty aim is not", !UnresolvedSpawnIsRefused("", false));

    std::string const reason = UnresolvedSpawnRefusal(530);
    Check("the reason names the map and the issue",
          Has(reason, "map 530") && Has(reason, "#1711"));

    // Repeated polls: the release at t=0 refuses the aim; the same aim written
    // again is called off for the whole cool-off and only then granted again.
    ErrandDeathLimits const limits{};
    int64_t const cooloff = ErrandCooloffSeconds(0, limits);
    Check("the bound is a real, finite cool-off", cooloff > 0);
    int regrants = 0;
    for (int64_t t = 5; t < cooloff; t += 5)
    {
        ErrandDeathToll toll;
        toll.sinceRefused = t;
        if (ErrandDeathBreaker(toll, limits).remedy != ErrandDeathRemedy::RefuseReissue)
            ++regrants;
    }
    Check("never re-sent inside the bound", regrants == 0);
    ErrandDeathToll after;
    after.sinceRefused = cooloff + 1;
    Check("granted again once the bound has passed",
          ErrandDeathBreaker(after, limits).remedy != ErrandDeathRemedy::RefuseReissue);

    std::string const source = Read("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp was read", !source.empty());
    Check("the drive asks the decision and refuses the aim",
          Has(source, "OverseerDecisions::UnresolvedSpawnIsRefused(target, guarded)") &&
              Has(source, "OverseerDecisions::UnresolvedSpawnRefusal("));
    Check("a spawn on the leader's own map still resolves through ResolveTravelTarget",
          Has(source, "!ResolveTravelTarget(bot, target, entry, pos, wantSkill, &said)"));

    if (failures)
        return EXIT_FAILURE;
    std::puts("unresolved spawn refusal: all checks passed");
    return EXIT_SUCCESS;
}
