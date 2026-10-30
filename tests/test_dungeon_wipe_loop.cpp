/*
 * A wiped dungeon party is not walked straight back in while its members still
 * carry Resurrection Sickness, and a second wipe of the same run at the same
 * place inside 600 s ends the attempt (#1165).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

using OverseerDecisions::RaidRunFacts;
using OverseerDecisions::RaidRunPhase;
using OverseerDecisions::RaidRunPhaseName;
using OverseerDecisions::RaidRunStep;
using OverseerDecisions::RaidWiped;
using OverseerDecisions::StepRaidRun;

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
    Check(what, RaidRunPhaseName(got) == RaidRunPhaseName(want));
    if (RaidRunPhaseName(got) != RaidRunPhaseName(want))
    {
        std::printf("    got %s, wanted %s\n", RaidRunPhaseName(got), RaidRunPhaseName(want));
    }
}

// The party wiped inside: the head among them, everyone dead.
RaidRunFacts Wiped()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.raidFormed = true;
    facts.headInside = true;
    facts.headAlive = false;
    facts.inWorld = 5;
    facts.inside = 5;
    facts.insideAlive = 0;
    facts.insideDead = 5;
    facts.heldSeconds = 0;
    facts.now = 1000;
    return facts;
}

// The party after the spirit healer: every member alive at the graveyard
// outside, nobody inside, no ghosts, and the run in RECOVER with one wipe on
// its book. This is the only state in which anybody carries Resurrection
// Sickness: the spirit healer gives it, and he revives outside.
RaidRunFacts RevivedOutside()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.raidFormed = true;
    facts.headInside = false;
    facts.headAlive = true;
    facts.headAssembled = false;
    facts.inWorld = 5;
    facts.inside = 0;
    facts.insideAlive = 0;
    facts.insideDead = 0;
    facts.ghosts = 0;
    facts.wipes = 1;
    facts.lastWipeAt = 1000;
    facts.samePlace = true;
    facts.now = 1100;
    facts.graveyard = "The Great Lift";
    return facts;
}

void AWipedPartyStillSickIsNotSentBack()
{
    // Revived sick at the graveyard after the wipe: the run stays in RECOVER
    // and nobody walks back to the door.
    RaidRunFacts facts = RevivedOutside();
    facts.membersSick = true;
    Check("revived outside is not a wipe", !RaidWiped(facts));
    RaidRunStep const sick = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("still sick stays in RECOVER", sick.phase, RaidRunPhase::Recover);
    Check("and the head does not walk to the door", !sick.aimStaging && !sick.aimDoor);
    Check("and nobody is knocked back in", !sick.knockMembers && !sick.knockHead);
    Check("and nobody is walked back", !sick.runBack);
    Check("and the order stands", !sick.release);

    // Once the sickness has ended the same run walks back to the door.
    facts.membersSick = false;
    facts.now = 1700;
    RaidRunStep const well = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("no longer sick stays in RECOVER", well.phase, RaidRunPhase::Recover);
    Check("and the head walks back to the door", well.aimStaging);

    // Sick with corpses still inside: the corpses are still released.
    RaidRunFacts mixed = Wiped();
    mixed.wipes = 1;
    mixed.lastWipeAt = 1000;
    mixed.samePlace = true;
    mixed.now = 1050;
    mixed.membersSick = true;
    RaidRunStep const both = StepRaidRun(RaidRunPhase::Recover, mixed);
    CheckPhase("sick with corpses inside stays in RECOVER", both.phase,
               RaidRunPhase::Recover);
    Check("the corpses are released", both.releaseDead);
    Check("and nobody is walked back while sick", !both.runBack);
}

void ASecondWipeAtTheSamePlaceEndsTheAttempt()
{
    // The run wiped once already (wipes == 1) and is wiping again at the same
    // place 599 s later, out of CLEAR: a new wipe inside the window, the
    // attempt ends.
    RaidRunFacts facts = Wiped();
    facts.wipes = 1;
    facts.lastWipeAt = 400;
    facts.now = 999; // 599 s after the last wipe, inside the window
    facts.samePlace = true;
    facts.graveyard = "The Great Lift";
    RaidRunStep const second = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("a second wipe at the same place ends the run", second.phase,
               RaidRunPhase::Idle);
    Check("and releases the order", second.release);
    Check("and releases the corpses first", second.releaseDead);
    Check("and never walks the party back in", !second.runBack);

    // Exactly 600 s after the last wipe is no longer "inside" the window, so
    // the attempt is not ended for place alone.
    facts.now = 1000; // 600 s after the last wipe
    RaidRunStep const atBound = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("exactly on the bound the run is not ended for place", atBound.phase,
               RaidRunPhase::Recover);
}

void ASamePlaceSecondWipeOutsideTheWindowWalksBack()
{
    // Same place but more than 600 s since the last wipe is a fresh attempt:
    // the party is walked back in.
    RaidRunFacts facts = Wiped();
    facts.wipes = 1;
    facts.lastWipeAt = 300;
    facts.now = 1000; // 700 s after the last wipe
    facts.samePlace = true;
    facts.graveyard = "The Great Lift";
    RaidRunStep const fresh = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("past the window the run walks back in", fresh.phase,
               RaidRunPhase::Recover);
    Check("and is sent back in", fresh.runBack);
}

// The adapter's sequence, poll by poll, with the wipe book kept the way
// mod_overseer.cpp keeps it: RaidWipeFacts fills the facts, StepRaidRun
// decides, RaidRunCountsWipe books a new wipe.
struct Book
{
    unsigned wipes{0};
    std::time_t lastWipeAt{0};
    std::uint32_t lastWipeMap{0};
};

constexpr std::uint32_t DOOR_MAP = 70;

RaidRunStep Poll(Book& book, RaidRunPhase& phase, RaidRunFacts facts, std::time_t now)
{
    OverseerDecisions::RaidWipeFacts(facts, book.wipes, book.lastWipeAt, book.lastWipeMap,
                                     DOOR_MAP, now);
    RaidRunStep const step = StepRaidRun(phase, facts);
    if (step.release)
    {
        phase = RaidRunPhase::Idle;
        return step;
    }
    if (OverseerDecisions::RaidRunCountsWipe(phase, step, facts))
    {
        ++book.wipes;
        book.lastWipeAt = now;
        book.lastWipeMap = DOOR_MAP;
    }
    phase = step.phase;
    return step;
}

void TheAdapterSequenceKeepsTheFirstWipe()
{
    Book book;
    RaidRunPhase phase = RaidRunPhase::Clear;

    // Poll 1: the party wipes out of CLEAR. One wipe is booked, the corpses
    // are released.
    RaidRunStep const first = Poll(book, phase, Wiped(), 1000);
    CheckPhase("the first wipe moves to RECOVER", first.phase, RaidRunPhase::Recover);
    Check("and releases the corpses", first.releaseDead);
    Check("one wipe is booked", book.wipes == 1);

    // Poll 2, 30 s later and inside the 600 s window: the corpses are still
    // inside (the release has not landed yet). This is the same wipe, not a
    // second one: the run stays in RECOVER and releases them again.
    RaidRunStep const again = Poll(book, phase, Wiped(), 1030);
    CheckPhase("a later RECOVER poll with the same corpses stays in RECOVER", again.phase,
               RaidRunPhase::Recover);
    Check("and does not end the run", !again.release);
    Check("and still releases the corpses", again.releaseDead);
    Check("and books no second wipe", book.wipes == 1);

    // Poll 3: the spirit healer revived them outside, sick. They hold.
    RaidRunFacts revived = RevivedOutside();
    revived.membersSick = true;
    RaidRunStep const sick = Poll(book, phase, revived, 1100);
    CheckPhase("revived sick holds in RECOVER", sick.phase, RaidRunPhase::Recover);
    Check("and the head does not walk back yet", !sick.aimStaging && !sick.aimDoor);

    // Poll 4: the sickness is over; the head walks back to the door.
    revived.membersSick = false;
    RaidRunStep const well = Poll(book, phase, revived, 1700);
    Check("well again, the head walks back", well.aimStaging);

    // Poll 5: back inside, alive: HOLD. Poll 6: a second wipe out of HOLD,
    // 800 s after the first, is a fresh attempt and is booked.
    RaidRunFacts in = Wiped();
    in.headAlive = true;
    in.insideAlive = 5;
    in.insideDead = 0;
    RaidRunStep const back = Poll(book, phase, in, 1750);
    CheckPhase("back inside the run holds", back.phase, RaidRunPhase::Hold);
    RaidRunStep const late = Poll(book, phase, Wiped(), 1800);
    CheckPhase("a wipe 800 s after the first is a fresh attempt", late.phase,
               RaidRunPhase::Recover);
    Check("and is booked as the second", book.wipes == 2);

    // Poll 7: back in at once and a third wipe 100 s after the second ends it,
    // with its corpses released first.
    Poll(book, phase, in, 1850);
    RaidRunStep const third = Poll(book, phase, Wiped(), 1900);
    Check("a wipe 100 s after the last at the same place ends the run", third.release);
    Check("and releases its corpses", third.releaseDead);
}

void ASingleMemberDeathIsNotAffected()
{
    // One member died; the run is not wiped. The sickness rule only applies to
    // a wipe, so a sick member's death does not hold the run.
    RaidRunFacts facts = Wiped();
    facts.headAlive = true;
    facts.insideAlive = 4;
    facts.insideDead = 1;
    facts.membersSick = true;
    facts.graveyard = "The Great Lift";
    Check("one dead of five is not a wipe", !RaidWiped(facts));
    RaidRunStep const notWiped = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("not a wipe holds HOLD", notWiped.phase, RaidRunPhase::Hold);
    Check("and is not blocked by the sickness rule", !notWiped.release);
    Check("the member is not released", !notWiped.releaseDead);
    Check("nobody is walked back", !notWiped.runBack);

    // The same death with nobody sick decides exactly the same step.
    facts.membersSick = false;
    RaidRunStep const notSick = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("the sickness does not change the phase", notWiped.phase, notSick.phase);
    Check("or the release", notWiped.release == notSick.release);
    Check("or the run-back", notWiped.runBack == notSick.runBack);
    Check("or the reason", std::string(notWiped.why) == std::string(notSick.why));

    // A run with no wipe on its book is not held by one sick member either:
    // the head outside in RECOVER walks back to the door.
    RaidRunFacts outside = RevivedOutside();
    outside.wipes = 0;
    outside.lastWipeAt = 0;
    outside.samePlace = false;
    outside.membersSick = true;
    RaidRunStep const noWipe = StepRaidRun(RaidRunPhase::Recover, outside);
    Check("with no wipe, a sick member does not hold the head", noWipe.aimStaging);
}

} // namespace

int main()
{
    AWipedPartyStillSickIsNotSentBack();
    ASecondWipeAtTheSamePlaceEndsTheAttempt();
    ASamePlaceSecondWipeOutsideTheWindowWalksBack();
    ASingleMemberDeathIsNotAffected();
    TheAdapterSequenceKeepsTheFirstWipe();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the dungeon wipe loop decisions hold\n");
    return EXIT_SUCCESS;
}
