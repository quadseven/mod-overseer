/*
 * The GATHERING clock counts time without progress, not time since staging.
 *
 * Measured 2026-09-23 on the dev realm. A Ragefire Chasm run was staged with
 * the Horde family about 5,000 yards from the door. The twelve-minute
 * whole-run backstop ran out while the leader was still walking toward it
 * ("GATHERING has held for over 12 minutes"), and three such attempts in a
 * row stop a campaign. A leader who keeps closing the gap must not be written
 * off; one who stops still is, twelve minutes after he stopped.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::StagingClock;
using OverseerDecisions::StagingClockAfterReading;

namespace
{

int failures = 0;
constexpr float PROGRESS = 50.f;
constexpr time_t BACKSTOP = 12 * 60;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheFirstReadingIsABaseline()
{
    StagingClock c{1000, -1.f};
    c = StagingClockAfterReading(c, true, 5000.f, 1010, PROGRESS);
    Check("the first reading does not restart the clock", c.since == 1000);
    Check("the first reading becomes the best", c.bestYards == 5000.f);
}

void NoReadingChangesNothing()
{
    StagingClock c{1000, 800.f};
    c = StagingClockAfterReading(c, false, 10.f, 1500, PROGRESS);
    Check("an unmeasured poll keeps the clock", c.since == 1000 && c.bestYards == 800.f);
}

void AWalkingLeaderIsNeverWrittenOff()
{
    // Five yards a second from 5,000 yards out, about seventeen minutes, read every
    // five seconds. The clock is never older than the backstop.
    StagingClock c{0, -1.f};
    time_t const start = 100000;
    c.since = start;
    bool writtenOff = false;
    for (int t = 0; t <= 20 * 60; t += 5)
    {
        float const yards = 5000.f - 5.f * static_cast<float>(t);
        if (yards < 0.f)
            break;
        c = StagingClockAfterReading(c, true, yards, start + t, PROGRESS);
        if ((start + t) - c.since > BACKSTOP)
            writtenOff = true;
    }
    Check("a leader closing the gap is not written off", !writtenOff);
}

void AStoppedLeaderIsWrittenOffAsBefore()
{
    StagingClock c{0, -1.f};
    time_t const start = 200000;
    c.since = start;
    c = StagingClockAfterReading(c, true, 375.f, start, PROGRESS);
    bool writtenOff = false;
    for (int t = 5; t <= 13 * 60; t += 5)
    {
        // Jitter on the spot, as a leader stuck on the level above does.
        float const yards = 375.f + ((t / 5) % 2 ? 3.f : -3.f);
        c = StagingClockAfterReading(c, true, yards, start + t, PROGRESS);
        if ((start + t) - c.since > BACKSTOP)
            writtenOff = true;
    }
    Check("a leader standing still is written off after the backstop", writtenOff);
}

void SmallStepsDoNotRestartIt()
{
    StagingClock c{3000, 400.f};
    c = StagingClockAfterReading(c, true, 360.f, 3100, PROGRESS);
    Check("forty yards is not progress enough", c.since == 3000 && c.bestYards == 400.f);
    c = StagingClockAfterReading(c, true, 349.f, 3200, PROGRESS);
    Check("fifty-one yards restarts it", c.since == 3200 && c.bestYards == 349.f);
}

}  // namespace

int main()
{
    TheFirstReadingIsABaseline();
    NoReadingChangesNothing();
    AWalkingLeaderIsNeverWrittenOff();
    AStoppedLeaderIsWrittenOffAsBefore();
    SmallStepsDoNotRestartIt();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
