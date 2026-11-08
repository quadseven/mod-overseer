/*
 * The #1426 teleport probe: a diagnostic that names who teleports a roster
 * member out of a dungeon. The rate limit is pinned as a decision; the wiring
 * is read from src/mod_overseer.cpp as text so the probe cannot move behind
 * the DoorShutFor early return or change what the hook returns.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "overseer_probe_rate.h"

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
    using OverseerDecisions::ProbeLineDue;
    Check("first line is due", ProbeLineDue(1000, 0));
    Check("nine seconds later is silent", !ProbeLineDue(1009, 1000));
    Check("ten seconds later is due", ProbeLineDue(1010, 1000));
    Check("a clock stepped back says again", ProbeLineDue(900, 1000));

    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return EXIT_FAILURE;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    std::size_t const hook = src.find("    bool OnPlayerBeforeTeleport(");
    std::size_t const end = src.find("\n    }\n", hook);
    std::string const body =
        hook == std::string::npos ? std::string() : src.substr(hook, end - hook);
    Check("the hook is found", !body.empty());

    std::size_t const probe = body.find("ProbeTeleportOut1426(player, mapid, options, target);");
    std::size_t const door = body.find("if (!OverseerWorldScript::DoorShutFor(player, mapid))");
    Check("the probe runs before the DoorShutFor early return",
          probe != std::string::npos && door != std::string::npos && probe < door);
    Check("the early return still returns true",
          Has(body, "DoorShutFor(player, mapid))\n            return true;"));
    Check("the refusal still returns false", Has(body, "        return false;"));
    Check("exactly one false and one true in the hook",
          body.find("return false;") == body.rfind("return false;") &&
              body.find("return true;") == body.rfind("return true;"));

    std::size_t const fn = src.find("    static void ProbeTeleportOut1426(");
    std::size_t const fnEnd = src.find("\n    }\n", fn);
    std::string const probeBody =
        fn == std::string::npos ? std::string() : src.substr(fn, fnEnd - fn);
    Check("the probe is found", !probeBody.empty());
    Check("the probe returns nothing to decide on", !Has(probeBody, "return true") &&
                                                       !Has(probeBody, "return false"));
    Check("only a roster member", Has(probeBody, "OnRoster(name)"));
    Check("only on an instance map", Has(probeBody, "map->Instanceable()"));
    Check("only toward another map", Has(probeBody, "player->GetMapId() == mapid"));
    Check("rate limited by the pure decision", Has(probeBody, "ProbeLineDue(now, last)"));
    Check("the line begins with the grep key",
          Has(probeBody, "\"overseer: probe #1426: '{}'"));
    Check("the line is a warning", Has(probeBody, "LOG_WARN("));
    Check("the line names options, target, group, lfg and the finder window",
          Has(probeBody, "options={}") && Has(probeBody, "target_null={}") &&
              Has(probeBody, "in_group={}") && Has(probeBody, "lfg_group={}") &&
              Has(probeBody, "finder_watched={}") && Has(probeBody, "isLFGGroup()") &&
              Has(probeBody, "IsFinderWatched(name)"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the #1426 teleport probe is wired before the door check and changes no return\n");
    return EXIT_SUCCESS;
}
