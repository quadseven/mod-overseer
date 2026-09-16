/* The quest-log saturation rule is pure so its destructive boundary can be
 * tested without a worldserver. */
#include "overseer_decisions.h"
#include <cstdio>

using OverseerDecisions::QuestIsStale;

int main()
{
    int failures = 0;
    auto check = [&](char const* name, bool got, bool want) {
        if (got != want) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    };

    check("large level gap and no progress", QuestIsStale(20, 7, false, false, 10), true);
    check("exact level gap", QuestIsStale(20, 10, false, false, 10), true);
    check("small level gap", QuestIsStale(20, 11, false, false, 10), false);
    check("objective progress preserves quest", QuestIsStale(20, 7, true, false, 10), false);
    check("active aim preserves quest", QuestIsStale(20, 7, false, true, 10), false);
    check("invalid threshold is safe", QuestIsStale(20, 7, false, false, 0), false);
    return failures;
}
