/*
 * An evacuation the ground cannot finish hearths out instead of walking for ever.
 *
 * THE READING THIS EXISTS FOR, measured on the dev realm 2026-10-08. A family
 * stood inside Shadowfang Keep (map 33) 116 yards from the exit door with no
 * route to it. The walk out was released as unreachable after eight refused
 * polls and the REPAIRING leg aimed all four members at the same door again on
 * the next poll, for eleven hours. A failed EXIT already hearths out; the
 * evacuation by REPAIRING, RESET and the split-copy walk did not.
 *
 * What is pinned here: a give-up counts only when it came after the walk began;
 * a member whose walk gave up is hearthed when it can be and walked as before
 * when it cannot; and a member whose walk has not given up keeps walking.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DecideEvacuationWay;
using OverseerDecisions::EvacuationWay;
using OverseerDecisions::EvacuationWayWord;
using OverseerDecisions::ExitHearthStep;
using OverseerDecisions::GroundGaveUpDuringWalk;

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

void CheckWay(char const* what, EvacuationWay got, EvacuationWay want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, EvacuationWayWord(got),
                EvacuationWayWord(want));
    ++failures;
}

}  // namespace

int main()
{
    // The give-up has to belong to THIS evacuation.
    CheckBool("a give-up after the walk began counts", GroundGaveUpDuringWalk(1000, 1040), true);
    CheckBool("a give-up on the poll the walk began counts", GroundGaveUpDuringWalk(1000, 1000),
              true);
    CheckBool("a give-up from before the walk does not", GroundGaveUpDuringWalk(1000, 900), false);
    CheckBool("no give-up at all does not", GroundGaveUpDuringWalk(1000, 0), false);
    CheckBool("a walk that never began has nothing to give up", GroundGaveUpDuringWalk(0, 50),
              false);

    // The measured family: walk released as unreachable, stone in the bag.
    CheckWay("a walk that gave up hearths when it can cast",
             DecideEvacuationWay(true, ExitHearthStep::Cast), EvacuationWay::Hearth);
    CheckWay("a moving member is stopped for the hearth, not walked on",
             DecideEvacuationWay(true, ExitHearthStep::StopFirst), EvacuationWay::Hearth);
    CheckWay("a hearth in flight is not walked over",
             DecideEvacuationWay(true, ExitHearthStep::Waiting), EvacuationWay::Hearth);

    // Nothing else to try: the walk goes on exactly as it did.
    CheckWay("a member that cannot hearth keeps its walk",
             DecideEvacuationWay(true, ExitHearthStep::Impossible), EvacuationWay::Walk);
    CheckWay("a member already outside is not hearthed",
             DecideEvacuationWay(true, ExitHearthStep::NotInside), EvacuationWay::Walk);

    // A walk that has not given up is never interrupted for a stone.
    CheckWay("a walk still making ground is not interrupted",
             DecideEvacuationWay(false, ExitHearthStep::Cast), EvacuationWay::Walk);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_evacuation_hearth: all passed\n");
    return 0;
}
