/*
 * Whether the walk a travel poll is about to issue is already running.
 *
 * The live failure this pins is ten minutes of silence. On the dev realm
 * 2026-09-07 a follower cut off from its leader was granted `new rpg` for its
 * errand at 02:06:49 and sent to an innkeeper 2,040 yards away. Seventy-six
 * seconds later the goal supervisor, which runs outside this module and writes
 * a whole strategy set per character on its own cycle, took the strategy back:
 *
 *   02:08:08 INFO command 92255 ('nc -new rpg' for 'Og') applied
 *   02:08:10 INFO command 92256 ('nc +follow' for 'Og') applied
 *
 * and the travel drive then said nothing about that character at all. Not a
 * refusal, not a re-aim, not a release. It was online, at full health, on the
 * errand's own map, with the errand column still set, three yards from where it
 * had stopped.
 *
 * Removing a strategy does not touch the rpg state that names the destination,
 * so the guard that asks "am I already walking there" read a motionless
 * character as one in mid-stride and skipped past the grant that would have
 * given the walk back. The grant cannot move: a freshly granted `new rpg`
 * starts at RPG_IDLE and the next tick rolls a random status, so it has to be
 * the statement before the aim is written. So the guard is what changed, and
 * this is the reading it now takes.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::WalkAlreadyInFlight;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// THE ONE THE ISSUE IS ABOUT. The destination still matches, because the bot's
// own state was never cleared, and the character cannot act on it. That is not
// a walk in flight, and reading it as one is what left a character standing
// still until a twenty-minute backstop gave up on it.
void ARevokedStrategyIsNotAWalkInFlight()
{
    Check("revoked mid-walk", WalkAlreadyInFlight(false, false, true), false);
}

// ...AND THE GUARD STILL DOES THE JOB IT WAS WRITTEN FOR. A character that
// really is walking to the destination it is about to be given is not re-issued,
// because ChangeToWanderNpc resets `lastReach` and `startT` and a walk re-issued
// every fifteen seconds never arrives.
void ARunningWalkIsStillProtected()
{
    Check("walking there already", WalkAlreadyInFlight(false, true, true), true);
}

// A DIFFERENT DESTINATION IS ALWAYS A RE-ISSUE, whether or not the character can
// act on the walk. This is the ordinary case for a stepped walk, whose aim point
// moves with the character on purpose.
void ADifferentDestinationIsAlwaysReIssued()
{
    Check("elsewhere, able", WalkAlreadyInFlight(false, true, false), false);
    Check("elsewhere, unable", WalkAlreadyInFlight(false, false, false), false);
}

// THE WATCHDOG'S OVERRIDE BEATS EVERYTHING. `reissue` means "whatever you think
// you can see, issue it anyway", which is the second rung of the staging
// watchdog and the older half of this same failure (#164).
void TheForcedReIssueBeatsEveryOtherReading()
{
    for (int canAct = 0; canAct < 2; ++canAct)
        for (int same = 0; same < 2; ++same)
            Check("forced re-issue", WalkAlreadyInFlight(true, canAct != 0, same != 0),
                  false);
}

// THE WHOLE TRUTH TABLE, WRITTEN OUT. Three booleans is eight rows and there is
// no reason to leave any of them to inference: this predicate decides whether a
// character keeps moving, and a reader should be able to see every answer it can
// give without running it.
void EveryCombinationIsWhatItSays()
{
    struct Row { bool forced; bool canAct; bool same; bool want; char const* what; };
    Row const rows[] = {
        {false, false, false, false, "idle, elsewhere"},
        {false, false, true,  false, "REVOKED mid-walk - the defect"},
        {false, true,  false, false, "able, elsewhere"},
        {false, true,  true,  true,  "genuinely walking there"},
        {true,  false, false, false, "forced, idle, elsewhere"},
        {true,  false, true,  false, "forced, revoked"},
        {true,  true,  false, false, "forced, able, elsewhere"},
        {true,  true,  true,  false, "forced over a running walk"},
    };
    for (Row const& row : rows)
        Check(row.what, WalkAlreadyInFlight(row.forced, row.canAct, row.same), row.want);
}

// ONLY ONE COMBINATION SAYS "DO NOT ISSUE", which is the property that makes
// this safe to have got wrong in the cheap direction. A redundant re-issue costs
// a reset clock; a wrongly withheld one costs a character that stands still and
// says nothing.
void ExactlyOneRowRefusesTheWalk()
{
    int refused = 0;
    for (int forced = 0; forced < 2; ++forced)
        for (int canAct = 0; canAct < 2; ++canAct)
            for (int same = 0; same < 2; ++same)
                if (WalkAlreadyInFlight(forced != 0, canAct != 0, same != 0))
                    ++refused;
    if (refused == 1)
        return;
    std::printf("FAIL exactly one row refuses the walk: got %d\n", refused);
    ++failures;
}

}  // namespace

int main()
{
    ARevokedStrategyIsNotAWalkInFlight();
    ARunningWalkIsStillProtected();
    ADifferentDestinationIsAlwaysReIssued();
    TheForcedReIssueBeatsEveryOtherReading();
    EveryCombinationIsWhatItSays();
    ExactlyOneRowRefusesTheWalk();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a character that cannot walk is not walking\n");
    return EXIT_SUCCESS;
}
