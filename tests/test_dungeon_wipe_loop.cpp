/*
 * A wiped dungeon party is not walked straight back in while its members still
 * carry Resurrection Sickness, and a second wipe of the same run at the same
 * place inside 600 s ends the attempt (#1165).
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
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

void AWipedPartyStillSickIsNotSentBack()
{
    // The wipe just happened and the party is still sick: the run stays in
    // RECOVER but does not walk anybody back in.
    RaidRunFacts facts = Wiped();
    facts.membersSick = true;
    facts.graveyard = "The Great Lift";
    RaidRunStep const sick = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("still sick stays in RECOVER", sick.phase, RaidRunPhase::Recover);
    Check("and is not sent back in", !sick.runBack);
    Check("the dead are still released", sick.releaseDead);

    // Once the sickness has ended the same run is walked back in.
    facts.membersSick = false;
    RaidRunStep const well = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("no longer sick returns to RECOVER", well.phase, RaidRunPhase::Recover);
    Check("and is sent back in", well.runBack);
    Check("the dead are still released", well.releaseDead);
}

void ASecondWipeAtTheSamePlaceEndsTheAttempt()
{
    // The run wiped once already (wipes == 1), is wiping again at the same
    // place five minutes and one second later, inside the 600 s window: the
    // attempt ends.
    RaidRunFacts facts = Wiped();
    facts.wipes = 1;
    facts.lastWipeAt = 400;
    facts.now = 999; // 599 s after the last wipe, inside the window
    facts.samePlace = true;
    facts.graveyard = "The Great Lift";
    RaidRunStep const second = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("a second wipe at the same place ends the run", second.phase,
               RaidRunPhase::Idle);
    Check("and releases the order", second.release);
    Check("and never walks the party back in", !second.runBack);

    // Exactly 600 s after the last wipe is no longer "inside" the window, so
    // the attempt is not ended for place alone.
    facts.now = 1000; // 600 s after the last wipe
    RaidRunStep const atBound = StepRaidRun(RaidRunPhase::Recover, facts);
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
    RaidRunStep const fresh = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("past the window the run walks back in", fresh.phase,
               RaidRunPhase::Recover);
    Check("and is sent back in", fresh.runBack);
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
}

} // namespace

int main()
{
    AWipedPartyStillSickIsNotSentBack();
    ASecondWipeAtTheSamePlaceEndsTheAttempt();
    ASamePlaceSecondWipeOutsideTheWindowWalksBack();
    ASingleMemberDeathIsNotAffected();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the dungeon wipe loop decisions hold\n");
    return EXIT_SUCCESS;
}
