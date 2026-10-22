/*
 * Recoveries per hour — a health metric.
 *
 * A recovery that fires fifteen times an hour is an alarm, not a fix, and the
 * page needs a count it can show. This pins the pure counting rule: how many
 * remedy applications fall in the half-open interval (now - window, now].
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::RecoveriesPerHour;

namespace
{

int failures = 0;

void Check(char const* what, std::size_t got, std::size_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %zu, wanted %zu\n", what, got, want);
    ++failures;
}

}  // namespace

int main()
{
    time_t const now = 100000;
    time_t const hour = 3600;

    // Remedies oldest first. One is exactly an hour old (excluded), one is
    // just inside the window, one is right now.
    std::vector<time_t> times = {now - hour - 1, now - hour, now - hour + 1, now};

    Check("empty history", RecoveriesPerHour({}, now, hour), 0u);
    Check("a zero window counts nothing", RecoveriesPerHour(times, now, 0), 0u);
    Check("a negative window counts nothing", RecoveriesPerHour(times, now, -1), 0u);

    // Exactly one hour old is outside the half-open interval.
    Check("one hour old is excluded",
          RecoveriesPerHour({now - hour}, now, hour), 0u);
    // One second past the hour is inside.
    Check("one second inside the hour is counted",
          RecoveriesPerHour({now - hour + 1}, now, hour), 1u);
    Check("four remedies, one at the boundary and one now",
          RecoveriesPerHour(times, now, hour), 2u);

    // A shorter window excludes the one just inside the hour.
    Check("a ten-minute window keeps only the one now",
          RecoveriesPerHour(times, now, 600), 1u);

    // The default window is one hour.
    Check("default window is one hour",
          RecoveriesPerHour(times, now), 2u);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("recoveries per hour: ok\n");
    return 0;
}
