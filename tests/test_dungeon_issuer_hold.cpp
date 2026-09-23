/*
 * The dungeon run while the family's head is away (#618), and who leads the
 * family party when he comes back (#607).
 *
 * The head is the only member with a client, and the dungeon module refuses a
 * true bot as the issuer of a dungeon-clear command. So while he is offline
 * nobody may arm the brain or skip an objective. These pin what the arming
 * drive and the CLEARING stall clock do about that: hold, say so once, and arm
 * again the moment he is back.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

using OverseerDecisions::ClearingClock;
using OverseerDecisions::ClearingClockHoldReason;
using OverseerDecisions::ClearingClockState;
using OverseerDecisions::DcArmingStep;
using OverseerDecisions::DecideDcArming;
using OverseerDecisions::ForgetDcOnRecord;
using OverseerDecisions::HeadTakesTheLead;

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

// (issuerAvailable, recordIsForThisRun, recordAccepted, retryDue)
void NoIssuerHoldsBeforeAnythingElse()
{
    // The measured case: an armed run, the head offline. Still a hold, not
    // "armed", because nothing may be sent under anybody's name.
    Check("no issuer, armed record",
          DecideDcArming(false, true, true, true) == DcArmingStep::HoldNoIssuer);
    Check("no issuer, no record",
          DecideDcArming(false, false, false, true) == DcArmingStep::HoldNoIssuer);
    Check("no issuer, refused record",
          DecideDcArming(false, true, false, true) == DcArmingStep::HoldNoIssuer);
}

void AcceptedIsDoneRefusedWaits()
{
    Check("accepted for this run",
          DecideDcArming(true, true, true, false) == DcArmingStep::Armed);
    Check("refused, retry not due",
          DecideDcArming(true, true, false, false) == DcArmingStep::WaitRetry);
    Check("refused, retry due",
          DecideDcArming(true, true, false, true) == DcArmingStep::Issue);
    Check("record for an older run",
          DecideDcArming(true, false, true, false) == DcArmingStep::Issue);
    Check("no record at all",
          DecideDcArming(true, false, false, false) == DcArmingStep::Issue);
}

void ARecordDoesNotOutliveTheCharacter()
{
    // THE REGRESSION. The head logged out with an accepted `dc on` on record,
    // the brain it had armed died with his PlayerbotAI, and on his return the
    // record still said "armed", so nothing re-armed the run.
    Check("logged out forgets", ForgetDcOnRecord(false, true));
    Check("off the dungeon map forgets", ForgetDcOnRecord(true, false));
    Check("inside keeps", !ForgetDcOnRecord(true, true));
}

// (acceptedOnEveryoneInside, leaderVisible, issuerAvailable)
void TheStallClockRunsOnlyWhileTheBrainCanBeDriven()
{
    Check("all three runs", ClearingClockState(true, true, true) == ClearingClock::Runs);
    Check("not armed holds",
          ClearingClockState(false, true, true) == ClearingClock::HeldNotArmed);
    Check("leader away holds",
          ClearingClockState(true, false, true) == ClearingClock::HeldLeaderAway);
    Check("no issuer holds",
          ClearingClockState(true, true, false) == ClearingClock::HeldNoIssuer);
    // The head offline is all three at once; the first missing thing names it.
    Check("head offline",
          ClearingClockState(false, false, false) == ClearingClock::HeldNotArmed);

    Check("a running clock has no hold reason",
          std::strlen(ClearingClockHoldReason(ClearingClock::Runs)) == 0);
    Check("every hold has a reason",
          std::strlen(ClearingClockHoldReason(ClearingClock::HeldNotArmed)) > 0 &&
              std::strlen(ClearingClockHoldReason(ClearingClock::HeldLeaderAway)) > 0 &&
              std::strlen(ClearingClockHoldReason(ClearingClock::HeldNoIssuer)) > 0);
}

// (headNamed, headPresent, headInThisGroup, headLeads)
void TheHeadTakesTheLeadBackWhenHeReturns()
{
    Check("back, in the party, a member leads", HeadTakesTheLead(true, true, true, false));
    Check("already leads", !HeadTakesTheLead(true, true, true, true));
    Check("still away", !HeadTakesTheLead(true, false, true, false));
    Check("present but not in this party yet", !HeadTakesTheLead(true, true, false, false));
    Check("the roster names no head", !HeadTakesTheLead(false, true, true, false));
}

}  // namespace

int main()
{
    NoIssuerHoldsBeforeAnythingElse();
    AcceptedIsDoneRefusedWaits();
    ARecordDoesNotOutliveTheCharacter();
    TheStallClockRunsOnlyWhileTheBrainCanBeDriven();
    TheHeadTakesTheLeadBackWhenHeReturns();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("dungeon issuer hold: all passed\n");
    return 0;
}
