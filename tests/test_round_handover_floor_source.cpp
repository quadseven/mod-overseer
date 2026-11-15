/*
 * The planner is given the aim's height, an attempt that ends above the aim is
 * said and not repeated unchanged, and the planned nodes are named once
 * (mod-overseer#1557).
 *
 * Reads src/mod_overseer.cpp as text: the wiring lives in the adapter, which
 * this binary cannot compile.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string src = buf.str();
    for (std::size_t at; (at = src.find('\r')) != std::string::npos;)
        src.erase(at, 1);

    Check("the planner is told the aim's height",
          Has(src, "limits.aimZKnown = true;\n        limits.aimZ = aimZ;"));
    Check("a wrong-level ending turns the way round off for the next plan",
          Has(src, "if (WrongLevelEnds().erase(bot->GetName()) > 0)") &&
              Has(src, "limits.allowRound = false;"));
    Check("the unreachable release asks whether it ended above the aim",
          Has(src, "OverseerDecisions::EndedAboveTheAim(bot->GetPositionZ(),"));
    Check("and remembers the walker", Has(src, "WrongLevelEnds().insert(name);"));
    Check("the planned nodes are logged",
          Has(src, "OverseerDecisions::RouteNodeList(plan.nodes, 64)"));

    // The release line itself is still one source line (the guard test greps it).
    Check("the release phrase is untouched",
          Has(src, "refused every bearing for {} polls running without getting any nearer - releasing the errand as unreachable\","));

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
