/*
 * A family held by bag pressure still has its Stratholme yard episode polled
 * (#1221).
 *
 * A family that leaves Stratholme with full bags lands in the closed exit
 * yard while the bag-pressure answer is HoldOut. The hold returned before
 * DriveStratholmeYard was reached, so the yard episode was never polled: the
 * way out was never worked, and the episode closed only later, by its
 * 15-minute ceiling, with a warning that blamed the yard. An open episode is
 * now driven ahead of the hold; a family with no episode keeps the hold.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::StratholmeYardDrivesBeforeBagHold;

namespace
{
int failures = 0;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}
}

int main()
{
    CheckBool("an open yard episode is driven before the bag hold returns",
              StratholmeYardDrivesBeforeBagHold(true), true);
    CheckBool("a family with no yard episode keeps the bag hold as it was",
              StratholmeYardDrivesBeforeBagHold(false), false);
    return failures ? 1 : 0;
}
