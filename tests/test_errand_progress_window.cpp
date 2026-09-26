/*
 * An errand is stuck only when it has stopped getting anywhere for a window.
 *
 * Measured on the dev realm, 2026-09-26: the family leader was sent to a
 * banker 2,933 yards away at 02:11:41 and released at 02:11:56 for "no
 * progress in 5 tries". Upstream's MoveFarTo counts a try on every AI
 * re-entry that is not five yards nearer, so five tries is seconds of a path
 * that bends away from the goal. The errand now reads distance, route and
 * ground covered over about a minute before it gives up.
 *
 * Also here: a walk already going where it is sent is not sent again.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ErrandProgress;
using OverseerDecisions::ErrandProgressLimits;
using OverseerDecisions::ErrandProgressReading;
using OverseerDecisions::ErrandProgressWindow;
using OverseerDecisions::ReadErrandProgress;
using OverseerDecisions::RouteMark;
using OverseerDecisions::WalkNeedsRetarget;
using OverseerDecisions::WalkRetargetLimits;

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

ErrandProgressReading At(float distance, float x, float y, std::uint64_t route = 0, long at = -1)
{
    ErrandProgressReading reading;
    reading.distance = distance;
    reading.x = x;
    reading.y = y;
    reading.route = RouteMark{route, at};
    return reading;
}

ErrandProgressLimits const LIMITS{};  // 60 s, 10 yd nearer, 25 yd moved

void FifteenSecondsIsNotAVerdict()
{
    ErrandProgressWindow window;
    Check("the first reading opens the window",
          ReadErrandProgress(window, At(2933.f, 0.f, 0.f), 100, LIMITS) ==
              ErrandProgress::Settling);
    // The measured release came here, 15 s in, on a walk that had not moved.
    Check("fifteen seconds with nothing is still settling",
          ReadErrandProgress(window, At(2933.f, 1.f, 0.f), 115, LIMITS) ==
              ErrandProgress::Settling);
    Check("so is fifty-nine",
          ReadErrandProgress(window, At(2933.f, 1.f, 0.f), 159, LIMITS) ==
              ErrandProgress::Settling);
    Check("a full window with nothing is no progress",
          ReadErrandProgress(window, At(2933.f, 2.f, 1.f), 160, LIMITS) ==
              ErrandProgress::NoProgress);
}

void APathThatBendsAwayIsProgress()
{
    ErrandProgressWindow window;
    ReadErrandProgress(window, At(2933.f, 0.f, 0.f), 0, LIMITS);
    // Around a lake: 300 yards of ground covered and the straight line to the
    // banker a little LONGER at the end of the minute than at its start.
    Check("covering ground while the goal gets further is progress",
          ReadErrandProgress(window, At(2950.f, 210.f, 214.f), 60, LIMITS) ==
              ErrandProgress::Progressing);
    Check("a new window opens after a verdict",
          ReadErrandProgress(window, At(2940.f, 215.f, 215.f), 70, LIMITS) ==
              ErrandProgress::Settling);
}

void GettingNearerIsProgress()
{
    ErrandProgressWindow window;
    ReadErrandProgress(window, At(500.f, 0.f, 0.f), 0, LIMITS);
    Check("twelve yards nearer on a tight switchback is progress",
          ReadErrandProgress(window, At(488.f, 10.f, 5.f), 61, LIMITS) ==
              ErrandProgress::Progressing);
}

void MovingOnAlongTheRouteIsProgress()
{
    ErrandProgressWindow window;
    ReadErrandProgress(window, At(400.f, 0.f, 0.f, 7, 12), 0, LIMITS);
    Check("the cursor moving on the same route is progress",
          ReadErrandProgress(window, At(401.f, 10.f, 10.f, 7, 13), 60, LIMITS) ==
              ErrandProgress::Progressing);
    ReadErrandProgress(window, At(401.f, 10.f, 10.f, 7, 13), 60, LIMITS);
    Check("a replanned route starting over is not progress by itself",
          ReadErrandProgress(window, At(401.f, 12.f, 10.f, 8, 0), 125, LIMITS) ==
              ErrandProgress::NoProgress);
}

void JitterIsNotProgress()
{
    ErrandProgressWindow window;
    ReadErrandProgress(window, At(300.f, 0.f, 0.f), 0, LIMITS);
    Check("a character jammed on scenery is stuck",
          ReadErrandProgress(window, At(296.f, 8.f, -6.f), 60, LIMITS) ==
              ErrandProgress::NoProgress);
}

WalkRetargetLimits const WALK{};  // 15 yd materially different, 8 yd close enough

void ARunningWalkIsLeftAlone()
{
    // Running to (100,0) from (40,0); the drive's new step is 6 yards off it.
    Check("a step a few yards from the running one is not re-sent",
          !WalkNeedsRetarget(100.f, 0.f, 104.f, 4.f, 40.f, 0.f, WALK));
    Check("a step far from the running one is re-sent",
          WalkNeedsRetarget(100.f, 0.f, 130.f, 0.f, 40.f, 0.f, WALK));
    Check("a running leg about to end is re-sent",
          WalkNeedsRetarget(100.f, 0.f, 104.f, 4.f, 95.f, 0.f, WALK));
}

}  // namespace

int main()
{
    FifteenSecondsIsNotAVerdict();
    APathThatBendsAwayIsProgress();
    GettingNearerIsProgress();
    MovingOnAlongTheRouteIsProgress();
    JitterIsNotProgress();
    ARunningWalkIsLeftAlone();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("errand progress is read over a window\n");
    return EXIT_SUCCESS;
}
