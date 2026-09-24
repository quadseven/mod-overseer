#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DamageHistory;
using OverseerDecisions::DamageTaken;
using OverseerDecisions::RememberDamage;

namespace
{
int failures = 0;

void Check(char const* what, bool value)
{
    if (!value)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

void RetainsTheLastThreeDamageEvents()
{
    DamageHistory history;
    Check("starts empty", history.empty());

    RememberDamage(history, DamageTaken{100, 12, "wolf"});
    RememberDamage(history, DamageTaken{40, 8, "trap"});
    RememberDamage(history, DamageTaken{25, 4, "unknown"});
    Check("has three samples", history.size() == 3);
    Check("keeps oldest while below capacity", history[0].source == "wolf");
    Check("keeps newest", history[2].amount == 25 && history[2].secondsBeforeDeath == 4);

    RememberDamage(history, DamageTaken{900, 1, "dragon"});
    Check("stays bounded", history.size() == 3);
    Check("evicts only oldest", history[0].source == "trap");
    Check("keeps final source", history[2].source == "dragon");
}

void EmptySourceIsAllowed()
{
    DamageHistory history;
    RememberDamage(history, DamageTaken{5, 2, {}});
    Check("empty source remains empty", history[0].source.empty());
}
}

int main()
{
    RetainsTheLastThreeDamageEvents();
    EmptySourceIsAllowed();
    return failures;
}
