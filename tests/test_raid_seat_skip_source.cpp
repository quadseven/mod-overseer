/*
 * FORM names every seat it does not seat and clears a leftover finder group
 * instead of skipping its members in silence (mod-overseer#1555).
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

std::string Body(std::string const& src, char const* signature, char const* close)
{
    std::size_t const at = src.find(signature);
    if (at == std::string::npos)
        return std::string();
    std::size_t const end = src.find(close, at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
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

    std::string const canJoin = Body(src, "static bool RaidSeatCanJoin(", "\n    }\n");
    Check("one answer for the facts and the formation",
          Has(canJoin, "OverseerDecisions::RaidSeatBlockOf(RaidSeatFactsOf(p, group))"));
    Check("the old finder refusal is gone from the join test", !Has(canJoin, "isLFGGroup"));

    std::string const facts = Body(src, "static OverseerDecisions::RaidSeatJoinFacts RaidSeatFactsOf(",
                                   "\n    }\n");
    Check("the finder group's state is read", Has(facts, "sLFGMgr->GetState(mine->GetGUID())"));
    Check("a member inside an instance is read", Has(facts, "IsDungeon()"));

    std::string const form = Body(src, "unsigned FormRaidFromSeats(", "\n    }\n");
    Check("FORM asks why a seat cannot join", Has(form, "OverseerDecisions::RaidSeatBlockOf(facts)"));
    Check("FORM collects the skipped seats", Has(form, "skips.push_back({name, block})"));
    Check("FORM logs the line", Has(form, "OverseerDecisions::RaidSeatSkipLine("));
    Check("FORM says it only when it changes", Has(form, "_raidSeatSkipSaid[family]"));
    Check("FORM clears a leftover finder group",
          Has(form, "OverseerDecisions::RaidSeatNeedsFinderClear(facts)") &&
              Has(form, "stale->Disband()"));
    Check("FORM grants nothing to the cleared seat",
          !Has(form, "TeleportTo") && !Has(form, "AddItem"));

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
