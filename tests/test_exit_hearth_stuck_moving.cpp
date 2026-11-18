/*
 * A stranded member whose "moving" flag never clears is never hearthed.
 *
 * Measured on the dev realm: a family's walk out of a dungeon failed and every
 * member hearthed out except one, which stood alone at the same coordinates for
 * the whole five minute episode while the pass read it as moving, held it
 * still first, and read it as moving again, six times. The episode then ended
 * with the member still inside and nothing came back for it. A hold that has
 * been in place for a poll or two and has not made the member stand is not
 * going to, so the pass has to clear what it can and say what it saw.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ExitHearthStopIsStuck;

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    Check(!ExitHearthStopIsStuck(0), "the first poll of a hold is not stuck");
    Check(!ExitHearthStopIsStuck(OverseerDecisions::EXIT_HEARTH_STOP_STUCK_SECONDS - 1),
          "one second short is not stuck");
    Check(ExitHearthStopIsStuck(OverseerDecisions::EXIT_HEARTH_STOP_STUCK_SECONDS),
          "a hold that has not made the member stand by now is stuck");
    // The measured case: six polls of about 45 seconds each.
    Check(ExitHearthStopIsStuck(45), "a 45 second hold that never took is stuck");
    Check(OverseerDecisions::EXIT_HEARTH_STOP_STUCK_SECONDS < 45,
          "the limit is inside one hold's own ceiling, or it could never be reached");

    if (failures)
        return EXIT_FAILURE;
    std::printf("exit hearth stuck moving: ok\n");
    return EXIT_SUCCESS;
}
