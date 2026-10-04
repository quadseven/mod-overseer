/*
 * A revival hold that ends under another hold hands its debt on.
 *
 * wow-dev 2026-10-04: 50 of 132 guild members stood in place after a
 * spirit-healer revival with `stay` on and `new rpg` off until the next
 * restart, because the revival hold deferred to the revived-sickness hold and
 * dropped what it owed, and that hold had recorded taking nothing.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::HandRevivalDebtOn;
using OverseerDecisions::HoldDebt;

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
    HoldDebt const none{};
    HoldDebt const leader = HandRevivalDebtOn(none, true, false);
    Check("the leader's stay is owed", leader.addedStay);
    Check("the leader's new rpg is owed", leader.removedNewRpg);
    Check("the leader is owed no follow", !leader.removedFollow);

    HoldDebt const follower = HandRevivalDebtOn(none, false, true);
    Check("a follower's stay is owed", follower.addedStay);
    Check("a follower's follow is owed", follower.removedFollow);
    Check("a follower is owed no new rpg", !follower.removedNewRpg);

    HoldDebt const alone = HandRevivalDebtOn(none, false, false);
    Check("a masterless non-leader is owed stay only",
          alone.addedStay && !alone.removedFollow && !alone.removedNewRpg);

    HoldDebt took{};
    took.removedFollow = true;
    HoldDebt const kept = HandRevivalDebtOn(took, true, false);
    Check("what the hold in force already owed is kept", kept.removedFollow && kept.removedNewRpg);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    std::size_t const release = source.find("void ReleaseRevivalHold(");
    Check("ReleaseRevivalHold exists", release != std::string::npos);
    if (release == std::string::npos)
        return;
    std::size_t next = source.find("\n    }\n", release);
    if (next == std::string::npos)
        next = source.size();
    std::string const body = source.substr(release, next - release);
    Check("the deferral hands the debt on",
          body.find("OverseerDecisions::HandRevivalDebtOn(") != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_revival_debt\n");
    return 0;
}
