/*
 * The recovery facts line never casts a non-finite reading (#1345).
 *
 * RunRecoveryFactsLine printed the leader's and the farthest member's
 * distances by casting them to unsigned after only a `>= 0.f` test. That
 * test excludes NaN but not +inf, and casting a value a float holds but an
 * unsigned cannot (inf, or anything above UINT_MAX) is undefined. The
 * sibling lines (StagingStallFactsLine, ApproachWhere) wrap the same kind
 * of reading in FiniteReading first; this line now gets the same guard,
 * and an unreadable distance is said as not measured, like a negative one.
 *
 * Compiled against src/overseer_decisions.cpp, run from the repo root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <limits>
#include <string>

using OverseerDecisions::RunFailureFacts;

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

bool Has(std::string const& text, std::string const& needle)
{
    return text.find(needle) != std::string::npos;
}

RunFailureFacts Base()
{
    RunFailureFacts f;
    f.leaderYardsFromStaging = 42.9f;
    f.farthestMemberYards = 7.5f;
    return f;
}

}  // namespace

int main()
{
    float const inf = std::numeric_limits<float>::infinity();
    float const nan = std::numeric_limits<float>::quiet_NaN();

    std::string const plain = OverseerDecisions::RunRecoveryFactsLine(Base());
    Check("a measured leader distance is printed",
          Has(plain, "leader 42y from the staging point"));
    Check("a measured member distance is printed", Has(plain, "farthest member 7y from the leader"));

    RunFailureFacts leaderInf = Base();
    leaderInf.leaderYardsFromStaging = inf;
    Check("an infinite leader distance is not measured, not cast",
          Has(OverseerDecisions::RunRecoveryFactsLine(leaderInf),
              "leader not measured against the staging point"));

    RunFailureFacts memberInf = Base();
    memberInf.farthestMemberYards = inf;
    Check("an infinite member distance is not measured, not cast",
          Has(OverseerDecisions::RunRecoveryFactsLine(memberInf), "farthest member not measured"));

    RunFailureFacts leaderNan = Base();
    leaderNan.leaderYardsFromStaging = nan;
    Check("a NaN leader distance stays not measured",
          Has(OverseerDecisions::RunRecoveryFactsLine(leaderNan),
              "leader not measured against the staging point"));

    if (failures)
        std::printf("%d recovery facts line checks failed\n", failures);
    return failures ? 1 : 0;
}
