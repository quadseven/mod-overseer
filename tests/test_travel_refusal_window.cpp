/*
 * The travel refusal row says when the refusal ends and how long the planned walk
 * is against the straight line (mod-overseer#1498), so the bridge can pick another
 * aim before an errand fails. The decisions are pure; the wiring half reads
 * src/mod_overseer.cpp and the migration (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::RouteLengthRatio;
using OverseerDecisions::TravelRefusalEndsAt;

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
    Check("a refusal ends one cool-off after it began",
          TravelRefusalEndsAt(1000, 7200) == 8200);
    Check("no refusal has no end", TravelRefusalEndsAt(0, 7200) == 0);
    Check("no cool-off has no end", TravelRefusalEndsAt(1000, 0) == 0);

    Check("a walk many times the straight line records its ratio",
          RouteLengthRatio(5000.f, 500.f) > 9.99f && RouteLengthRatio(5000.f, 500.f) < 10.01f);
    Check("nothing planned records no ratio", RouteLengthRatio(0.f, 500.f) == 0.f);
    Check("an aim under a yard away records no ratio", RouteLengthRatio(40.f, 0.5f) == 0.f);

    std::string const source = Read("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp was read", !source.empty());
    Check("the row is written with the end time and the ratio",
          Has(source, "refused_until") && Has(source, "route_ratio") &&
              Has(source, "OverseerDecisions::TravelRefusalEndsAt(") &&
              Has(source, "OverseerDecisions::RouteLengthRatio("));
    Check("the drive records the ratio of a walk, refused or not",
          Has(source, "_travelAims.NoteRouteRatio(name, target, planned.routeRatio);"));
    Check("the schema probe asks for the new columns",
          Has(source, "'refused_until'") && Has(source, "'route_ratio_target'"));

    std::string const sql =
        Read("data/sql/characters/base/2026_11_21_00_overseer_travel_refusal_window.sql");
    Check("the migration was read", !sql.empty());
    for (char const* column : {"`refused_until`", "`route_ratio`", "`route_ratio_target`"})
        Check(column, Has(sql, column));

    if (failures)
        return EXIT_FAILURE;
    std::puts("travel refusal window: all checks passed");
    return EXIT_SUCCESS;
}
