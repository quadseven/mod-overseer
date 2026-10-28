/*
 * The wipe loop on the paths #1165's fix did not reach (#1201).
 *
 *   - A FAMILY 5-man run that wipes twice at the same place inside 600 s ends
 *     the attempt instead of walking back in, and IDLE holds the next attempt
 *     for the window and while anybody is still sick.
 *   - The RAID path keeps its wipe book across a release, so the sickness hold
 *     still applies to the attempt after it.
 *   - A raid release that leaves a few members alive inside still ends the
 *     attempt: the next poll lets go again rather than walking anybody back.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using OverseerDecisions::DungeonWipeBook;
using OverseerDecisions::DungeonWipeBookEndsAttempt;
using OverseerDecisions::DungeonWipeHoldsRunStart;
using OverseerDecisions::RaidRunFacts;
using OverseerDecisions::RaidRunPhase;
using OverseerDecisions::RaidRunPhaseName;
using OverseerDecisions::RaidRunStep;
using OverseerDecisions::StepRaidRun;
using OverseerDecisions::WIPE_LOOP_SECONDS;
using OverseerDecisions::WIPE_SICK_HOLD_MAX_SECONDS;

namespace
{

int failures = 0;

void Check(char const* what, bool held)
{
    if (held)
        return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

void CheckPhase(char const* what, RaidRunPhase got, RaidRunPhase want)
{
    Check(what, got == want);
    if (got != want)
        std::printf("    got %s, wanted %s\n", RaidRunPhaseName(got), RaidRunPhaseName(want));
}

constexpr std::uint32_t RAZORFEN = 47;
constexpr std::uint32_t DEADMINES = 36;

// The loop #1165 measured: the party revived at one graveyard about 525 s
// apart, each time after walking back in and wiping at the same pull.
void AFamilyThatWipesTwiceAtOnePlaceEndsTheAttempt()
{
    DungeonWipeBook book;
    Check("the first wipe does not end the attempt",
          !DungeonWipeBookEndsAttempt(book, RAZORFEN, 10000));
    Check("and is on the book", book.wipes == 1 && book.lastWipeAt == 10000 &&
                                    book.lastWipeMap == RAZORFEN && book.endedAt == 0);
    Check("nothing holds the next attempt after one wipe",
          !DungeonWipeHoldsRunStart(book, RAZORFEN, true, 10010));

    Check("the second wipe there 525 s later ends the attempt",
          DungeonWipeBookEndsAttempt(book, RAZORFEN, 10525));
    Check("and the ending is on the book", book.wipes == 2 && book.endedAt == 10525 &&
                                               book.endedMap == RAZORFEN);

    // IDLE does not walk them straight back in.
    Check("the next attempt is held inside the window",
          DungeonWipeHoldsRunStart(book, RAZORFEN, false, 10525 + 1));
    Check("and up to its last second",
          DungeonWipeHoldsRunStart(book, RAZORFEN, false, 10525 + WIPE_LOOP_SECONDS - 1));
    Check("past the window, a sick member still holds it",
          DungeonWipeHoldsRunStart(book, RAZORFEN, true, 10525 + WIPE_LOOP_SECONDS + 60));
    Check("past the window with nobody sick, the attempt opens",
          !DungeonWipeHoldsRunStart(book, RAZORFEN, false, 10525 + WIPE_LOOP_SECONDS));
    Check("the sickness hold is bounded",
          !DungeonWipeHoldsRunStart(book, RAZORFEN, true, 10525 + WIPE_SICK_HOLD_MAX_SECONDS));
    Check("another dungeon is never held",
          !DungeonWipeHoldsRunStart(book, DEADMINES, true, 10530));
}

void WipesFarApartOrElsewhereDoNotEndIt()
{
    DungeonWipeBook book;
    DungeonWipeBookEndsAttempt(book, RAZORFEN, 10000);
    Check("a second wipe 600 s later is outside the window",
          !DungeonWipeBookEndsAttempt(book, RAZORFEN, 10000 + WIPE_LOOP_SECONDS));
    Check("and holds nothing", !DungeonWipeHoldsRunStart(book, RAZORFEN, true, 10700));

    DungeonWipeBook other;
    DungeonWipeBookEndsAttempt(other, RAZORFEN, 10000);
    Check("a wipe in another dungeon inside the window is not the same place",
          !DungeonWipeBookEndsAttempt(other, DEADMINES, 10100));
    // The window is measured from the last wipe, not the first.
    Check("but the next one there is, measured from the last wipe",
          DungeonWipeBookEndsAttempt(other, DEADMINES, 10100 + WIPE_LOOP_SECONDS - 1));
}

// The raid after an attempt ended by a wipe was released: the order stands,
// so the run is at IDLE with the head outside and the raid disbanded.
RaidRunFacts ReleasedAfterAWipe()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.headAlive = true;
    facts.raidFormed = false;
    facts.inWorld = 5;
    facts.wipes = 2;
    facts.lastWipeAt = 1000;
    facts.samePlace = true;
    facts.now = 1100;
    return facts;
}

void TheRaidSicknessHoldOutlivesTheRelease()
{
    RaidRunFacts facts = ReleasedAfterAWipe();
    facts.membersSick = true;
    RaidRunStep const sick = StepRaidRun(RaidRunPhase::Idle, facts);
    CheckPhase("a sick raid after a released wipe stays at IDLE", sick.phase,
               RaidRunPhase::Idle);
    Check("and does not form the next attempt", !sick.form);
    Check("and walks nobody to the door", !sick.aimStaging && !sick.aimDoor);
    Check("and the order is not let go a second time", !sick.release);

    facts.membersSick = false;
    RaidRunStep const well = StepRaidRun(RaidRunPhase::Idle, facts);
    CheckPhase("once nobody is sick the next attempt forms", well.phase, RaidRunPhase::Form);
    Check("and forms", well.form);

    // A raid that never wiped is not held by somebody's sickness.
    RaidRunFacts fresh = ReleasedAfterAWipe();
    fresh.wipes = 0;
    fresh.lastWipeAt = 0;
    fresh.membersSick = true;
    CheckPhase("no wipe on the book, no hold",
               StepRaidRun(RaidRunPhase::Idle, fresh).phase, RaidRunPhase::Form);

    // And the hold is bounded from the last wipe.
    RaidRunFacts later = ReleasedAfterAWipe();
    later.membersSick = true;
    later.now = later.lastWipeAt + WIPE_SICK_HOLD_MAX_SECONDS;
    CheckPhase("a sickness long after the wipe does not hold",
               StepRaidRun(RaidRunPhase::Idle, later).phase, RaidRunPhase::Form);
}

// The release that ended the attempt kept the raid: one member is alive
// inside, the dead have been released and their ghosts are outside, and the
// head is outside. Before #1201 the next poll was RECOVER's walk-back.
RaidRunFacts EndingWithOneAliveInside()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.headAlive = false;
    facts.headInside = false;
    facts.raidFormed = true;
    facts.inWorld = 10;
    facts.inside = 1;
    facts.insideAlive = 1;
    facts.insideDead = 0;
    facts.ghosts = 9;
    facts.wipes = 2;
    facts.lastWipeAt = 1000;
    facts.samePlace = true;
    facts.now = 1010;
    return facts;
}

void AReleaseThatLeavesTheLivingInsideStillEndsTheAttempt()
{
    RaidRunFacts facts = EndingWithOneAliveInside();

    // Without the marker this is exactly the state RECOVER walks back from,
    // which is the fall-back the ticket names.
    RaidRunStep const unmarked = StepRaidRun(RaidRunPhase::Recover, facts);
    Check("unmarked, the state is RECOVER's walk-back",
          unmarked.phase == RaidRunPhase::Recover && unmarked.runBack);

    facts.attemptEnding = true;
    RaidRunStep const ending = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("an ending attempt goes to IDLE", ending.phase, RaidRunPhase::Idle);
    Check("and lets go again", ending.release);
    Check("and releases any corpse left inside", ending.releaseDead);
    Check("and walks no ghost back", !ending.runBack);
    Check("and knocks nobody through the door",
          !ending.knockMembers && !ending.knockHead);
    Check("and aims nobody at the door", !ending.aimStaging && !ending.aimDoor);

    // From any phase the run happens to be holding.
    CheckPhase("from CLEAR too", StepRaidRun(RaidRunPhase::Clear, facts).phase,
               RaidRunPhase::Idle);
    facts.headInside = true;
    facts.headAlive = true;
    Check("and with the head still inside",
          StepRaidRun(RaidRunPhase::Hold, facts).release);
}

}  // namespace

int main()
{
    AFamilyThatWipesTwiceAtOnePlaceEndsTheAttempt();
    WipesFarApartOrElsewhereDoNotEndIt();
    TheRaidSicknessHoldOutlivesTheRelease();
    AReleaseThatLeavesTheLivingInsideStillEndsTheAttempt();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
