/*
 * The town trip that follows a dungeon run, and the read-back that says whether
 * it worked.
 *
 * WHAT THIS PINS, MEASURED RATHER THAN IMAGINED. Every `kind='repair'` row ever
 * written on the dev realm, 67 of them: 47 refused "repairer not in range", 15
 * delivered, 3 found nothing damaged, 2 found nobody online. The last one of any
 * kind was written hours before a dungeon run that then completed, and no repair
 * was attempted afterwards - because nothing in the module has ever asked for
 * one. `INSERT INTO overseer_command` has zero matches in the adapter.
 *
 * The consequence, read off the five characters those rows belong to: each
 * carries twelve to fourteen equipped items and three or four of them at
 * durability ZERO. A broken item gives no armour at all and a broken weapon does
 * minimal damage, so the family has been running a dungeon nearly unarmoured.
 *
 * Two decisions are worth being unable to get wrong, and they are the two here.
 *
 * WHO IS OWED A TRIP, on each poll of the leg. The ordering is the content: a
 * member that is not in the world has no durability to read and must not be
 * declared finished; a member with nothing damaged is finished wherever it
 * stands; and a member already in reach of a repairer is repaired on THAT poll
 * rather than walked again, which is the same lesson the inn bind and the
 * counter hold each paid for at the other end of this trip.
 *
 * WHAT THE READ-BACK PROVED, which the command table cannot say. `DoRepair`
 * writes the same EMPTY `detail` for a full repair and for a partial one; the
 * difference lives only inside the `result` JSON. That is not hypothetical:
 * Player::DurabilityRepair charges per item and simply returns when the purse is
 * short, so a repair-all against a thin purse restores the earlier slots,
 * silently leaves the rest, and reports `delivered`. Fifteen delivered rows and
 * eighteen broken items across five characters are not in contradiction today,
 * and they should be.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadRepairBack;
using OverseerDecisions::REPAIR_CANNOT_AFFORD;
using OverseerDecisions::RepairLegMayTryAgain;
using OverseerDecisions::RepairLegFacts;
using OverseerDecisions::RepairLegMemberStep;
using OverseerDecisions::RepairLegStatus;
using OverseerDecisions::RepairLegStep;
using OverseerDecisions::RepairLegStepWord;
using OverseerDecisions::RepairLegVerdict;
using OverseerDecisions::RepairReadBack;
using OverseerDecisions::RepairReadBackWord;

namespace
{

int failures = 0;

char const* Name(RepairLegVerdict verdict)
{
    switch (verdict)
    {
        case RepairLegVerdict::Working:  return "working";
        case RepairLegVerdict::Finished: return "finished";
        case RepairLegVerdict::Overdue:  return "overdue";
    }
    return "unknown";
}

RepairLegFacts Member(bool present, bool damaged, bool atARepairer)
{
    RepairLegFacts facts;
    facts.present = present;
    facts.damaged = damaged;
    facts.atARepairer = atARepairer;
    return facts;
}

void Step(char const* what, RepairLegFacts const& facts, RepairLegStep want)
{
    RepairLegStep const got = RepairLegMemberStep(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: wanted '%s', got '%s'\n", what, RepairLegStepWord(want),
                RepairLegStepWord(got));
    ++failures;
}

void Verdict(char const* what, unsigned outstanding, time_t held, time_t bound,
             RepairLegVerdict want)
{
    RepairLegVerdict const got = RepairLegStatus(outstanding, held, bound);
    if (got == want)
        return;
    std::printf("FAIL %s: wanted '%s', got '%s'\n", what, Name(want), Name(got));
    ++failures;
}

void Back(char const* what, unsigned damaged, unsigned broken, RepairReadBack want)
{
    RepairReadBack const got = ReadRepairBack(damaged, broken);
    if (got == want)
        return;
    std::printf("FAIL %s: wanted '%s', got '%s'\n", what, RepairReadBackWord(want),
                RepairReadBackWord(got));
    ++failures;
}

// ------------------------------------------------------------- who is owed --

// The whole point of the leg: a damaged member is walked to a repairer.
void ADamagedMemberIsWalkedToARepairer()
{
    Step("damaged and nowhere near a counter", Member(true, true, false),
         RepairLegStep::Walk);
}

// AND THE POLL IT ARRIVES ON IS THE POLL TO SPEND. 47 of the 67 repair rows ever
// written died because the character was no longer in reach when the row got to
// it; a leg that saw one in reach and re-issued the walk anyway would be
// re-running that experiment.
void AMemberAlreadyAtARepairerIsRepairedNow()
{
    Step("damaged and standing at a counter the core accepts",
         Member(true, true, true), RepairLegStep::Repair);
}

// A REPAIRER AT THE FEET IS STILL A REPAIRER, and this ordering is what makes a
// portable repair bot work without a line of code about portable repair bots.
// The adapter's gate and the executor's own sweep both test the repair npcflag
// over whatever creatures are actually near the character, and neither looks up
// a stored entry - so a bot somebody summons at the party's feet passes exactly
// the test a town blacksmith passes. Asking "is one in reach" BEFORE deciding to
// walk is the whole of the preference: with a bot out, no member ever walks.
//
// Pinned as its own case so that reordering these two tests, which would look
// like tidying, is a test failure rather than a silent hundred-run regression
// into walking to town after every one of a hundred runs.
void ARepairerInReachIsPreferredToAnyWalk()
{
    Step("a repairer within the core's own interact gate",
         Member(true, true, true), RepairLegStep::Repair);
    Step("no repairer in reach", Member(true, true, false), RepairLegStep::Walk);
}

// A member with nothing damaged is finished, and it is finished WHEREVER it is
// standing. The other order would walk an undamaged character to a repairer to
// discover there was nothing to do when it got there.
void AnUndamagedMemberIsFinishedWhereverItStands()
{
    Step("undamaged, nowhere near a counter", Member(true, false, false),
         RepairLegStep::Done);
    Step("undamaged, standing at one anyway", Member(true, false, true),
         RepairLegStep::Done);
}

// AND ABSENT IS NOT UNDAMAGED. Durability lives on a live Player and nowhere
// else, so a member that is mid-login, logged out or crossing a map has an
// UNKNOWN reading rather than a clean one. Answering 'done' here would finish
// the leg for a character that logs back in ten seconds later with broken
// armour, which is exactly the silent hole this leg exists to close.
void AMemberNobodyCanReadIsNotDeclaredFinished()
{
    Step("offline, reading as undamaged", Member(false, false, false),
         RepairLegStep::Wait);
    Step("offline, reading as damaged", Member(false, true, false),
         RepairLegStep::Wait);
    Step("offline, reading as at a counter", Member(false, true, true),
         RepairLegStep::Wait);
}

// Every combination, so the table above is the whole table and not a sample.
void EveryCombinationIsWhatItSays()
{
    for (int present = 0; present <= 1; ++present)
        for (int damaged = 0; damaged <= 1; ++damaged)
            for (int at = 0; at <= 1; ++at)
            {
                RepairLegFacts const facts = Member(present != 0, damaged != 0, at != 0);
                RepairLegStep const want =
                    !present ? RepairLegStep::Wait
                             : !damaged ? RepairLegStep::Done
                                        : at ? RepairLegStep::Repair : RepairLegStep::Walk;
                Step("combination", facts, want);
            }
}

// A member that is not present is never given a walk or a packet: both of those
// need a live character and would be this module acting on a name.
void NothingIsAskedOfACharacterThatIsNotThere()
{
    for (int damaged = 0; damaged <= 1; ++damaged)
        for (int at = 0; at <= 1; ++at)
        {
            RepairLegStep const got =
                RepairLegMemberStep(Member(false, damaged != 0, at != 0));
            if (got == RepairLegStep::Wait)
                continue;
            std::printf("FAIL an absent member was asked to '%s'\n",
                        RepairLegStepWord(got));
            ++failures;
        }
}

// Every step has a word, and no word is empty - a log line that says nothing
// about which branch it took is the narration this module keeps replacing.
void EveryStepHasAWord()
{
    RepairLegStep const steps[] = {RepairLegStep::Wait, RepairLegStep::Done,
                                   RepairLegStep::Repair, RepairLegStep::Walk};
    for (RepairLegStep step : steps)
    {
        if (*RepairLegStepWord(step))
            continue;
        std::printf("FAIL a repair leg step has no word\n");
        ++failures;
    }
}

// ---------------------------------------------------- when the leg is over --

void ALegWithNobodyOutstandingIsFinished()
{
    Verdict("nobody outstanding, well inside the bound", 0, 5, 600,
            RepairLegVerdict::Finished);
}

void ALegWithSomebodyOutstandingKeepsWorking()
{
    Verdict("one member still walking", 1, 5, 600, RepairLegVerdict::Working);
    Verdict("one second short of the bound", 1, 599, 600, RepairLegVerdict::Working);
}

// PAST THE BOUND THE RUN OPENS ANYWAY, and the leg says who it could not repair.
// The direction to fail in is the one the run's own maintenance hold already
// argues for: a missed repair costs one run's durability, a campaign that
// silently stopped costs the campaign.
void ALegThatRunsPastItsBoundIsOverdue()
{
    Verdict("at the bound with somebody outstanding", 1, 600, 600,
            RepairLegVerdict::Overdue);
    Verdict("long past it", 3, 4000, 600, RepairLegVerdict::Overdue);
}

// FINISHED OUTRANKS OVERDUE. A leg whose last member was repaired on the very
// poll the bound fired did the thing it exists for, and an ERROR that fires on
// success is how a real one gets ignored.
void AFinishedLegIsNeverReportedAsOverdue()
{
    Verdict("finished exactly at the bound", 0, 600, 600, RepairLegVerdict::Finished);
    Verdict("finished long past it", 0, 99999, 600, RepairLegVerdict::Finished);
}

// A BOUND OF ZERO IS NOT AN UNBOUNDED LEG. Reading a zero as "no bound" is how a
// wait somebody meant to disable becomes a wait that never ends.
void ABoundOfZeroGivesTheLegNoTimeAtAll()
{
    Verdict("zero bound, somebody outstanding", 1, 0, 0, RepairLegVerdict::Overdue);
    Verdict("zero bound, nobody outstanding", 0, 0, 0, RepairLegVerdict::Finished);
}

// ------------------------------------------- what the read-back proved --

void NothingDamagedReadsWhole()
{
    Back("nothing damaged", 0, 0, RepairReadBack::Whole);
}

void SomethingShortOfMaximumReadsStillDamaged()
{
    Back("one scuffed item", 1, 0, RepairReadBack::StillDamaged);
    Back("a whole scuffed set", 14, 0, RepairReadBack::StillDamaged);
}

// BROKEN OUTRANKS DAMAGED, because they are not degrees of one thing. Three or
// four items at zero is what was measured on every one of the five characters,
// and a party sent back in like that is fighting nearly unarmoured. It gets its
// own word so the log can say so.
void SomethingAtZeroReadsStillBroken()
{
    Back("one broken item among the scuffed", 4, 1, RepairReadBack::StillBroken);
    Back("the measured state of one character", 4, 4, RepairReadBack::StillBroken);
}

// The three are exhaustive and ordered, so no pair of counts falls through.
void EveryCountPairHasAnAnswer()
{
    for (unsigned damaged = 0; damaged <= 20; ++damaged)
        for (unsigned broken = 0; broken <= damaged; ++broken)
        {
            RepairReadBack const got = ReadRepairBack(damaged, broken);
            RepairReadBack const want =
                broken ? RepairReadBack::StillBroken
                       : damaged ? RepairReadBack::StillDamaged : RepairReadBack::Whole;
            if (got == want)
                continue;
            std::printf("FAIL %u damaged / %u broken: wanted '%s', got '%s'\n", damaged,
                        broken, RepairReadBackWord(want), RepairReadBackWord(got));
            ++failures;
        }
}

// Only a genuinely clean read is allowed to say 'whole'. This is the assertion
// the command row cannot make and the one the operator asked for: after a run,
// every member reads zero items at durability 0.
void OnlyACleanReadIsWhole()
{
    for (unsigned damaged = 0; damaged <= 20; ++damaged)
        for (unsigned broken = 0; broken <= damaged; ++broken)
        {
            bool const whole = ReadRepairBack(damaged, broken) == RepairReadBack::Whole;
            if (whole == (damaged == 0 && broken == 0))
                continue;
            std::printf("FAIL %u damaged / %u broken read as %s\n", damaged, broken,
                        whole ? "whole" : "not whole");
            ++failures;
        }
}

void EveryReadBackHasAWord()
{
    RepairReadBack const all[] = {RepairReadBack::Whole, RepairReadBack::StillDamaged,
                                  RepairReadBack::StillBroken};
    for (RepairReadBack readBack : all)
    {
        if (*RepairReadBackWord(readBack))
            continue;
        std::printf("FAIL a repair read-back has no word\n");
        ++failures;
    }
}


// ------------------------------- what the leg may try again, and what it may not --

void TryAgain(char const* detail, bool want)
{
    bool const got = RepairLegMayTryAgain(detail);
    if (got == want)
        return;
    std::printf("FAIL '%s': wanted may-try-again %s, got %s\n", detail,
                want ? "true" : "false", got ? "true" : "false");
    ++failures;
}

// AN EMPTY PURSE IS THE ONE WALL THE LEG CANNOT GET PAST BY ASKING AGAIN, and it
// is the whole reason this is a second question rather than a second reading of
// RepairRefusalRetry. To a sender that can go and sell something, an empty purse
// is worth another row later. To a leg that does not earn, the next poll finds
// exactly the same money and exactly the same gear, so spinning on it spends the
// bound to reach the identical refusal, once per run, a hundred times.
void AnEmptyPurseIsFinalForTheLeg()
{
    TryAgain(REPAIR_CANNOT_AFFORD, false);
    // And the literal is the one the executor actually returns. A copy that
    // drifted would silently turn this back into a spin.
    if (std::string(REPAIR_CANNOT_AFFORD) != "cannot afford the repair")
    {
        std::printf("FAIL the shared refusal literal has drifted: %s\n",
                    REPAIR_CANNOT_AFFORD);
        ++failures;
    }
}

// A refusal about the ITEM, the CLASS or the COMMAND is permanent for both
// callers, so the leg defers to the table rather than keeping an opinion.
void APermanentRefusalIsPermanentForTheLegToo()
{
    TryAgain("nothing is damaged", false);
    TryAgain("item is not damaged", false);
    TryAgain("item not carried", false);
    TryAgain("malformed repair request", false);
}

// A counter that slipped out of reach between the poll that saw it and the
// packet is the ordinary last few yards of a walk, not a fault. Walk again.
void ACounterThatSlippedOutOfReachIsWorthAnotherPoll()
{
    TryAgain("repairer not in range", true);
}

// A wall on the CHARACTER clears on its own: it revives, it lands, it logs in.
// Giving up on one would end the leg for a member the very next poll could have
// repaired.
void AWallOnTheCharacterIsWorthAnotherPoll()
{
    TryAgain("character is dead", true);
    TryAgain("character is in flight", true);
    TryAgain("character is not in the world", true);
    TryAgain("character has no session", true);
}

// AND A REFUSAL NOBODY HAS HEARD OF IS WORTH ONE MORE POLL, the same direction
// RepairRefusalRetry guesses in and for the same reason: an unrecognised wall is
// more likely a new transient than a new permanent, retrying a permanent costs a
// poll, and giving up on a transient costs the repair.
void AnUnknownRefusalIsWorthAnotherPoll()
{
    TryAgain("the core repaired nothing", true);
    TryAgain("something nobody has ever written down", true);
}

}  // namespace

int main()
{
    ADamagedMemberIsWalkedToARepairer();
    AMemberAlreadyAtARepairerIsRepairedNow();
    ARepairerInReachIsPreferredToAnyWalk();
    AnUndamagedMemberIsFinishedWhereverItStands();
    AMemberNobodyCanReadIsNotDeclaredFinished();
    EveryCombinationIsWhatItSays();
    NothingIsAskedOfACharacterThatIsNotThere();
    EveryStepHasAWord();

    ALegWithNobodyOutstandingIsFinished();
    ALegWithSomebodyOutstandingKeepsWorking();
    ALegThatRunsPastItsBoundIsOverdue();
    AFinishedLegIsNeverReportedAsOverdue();
    ABoundOfZeroGivesTheLegNoTimeAtAll();

    NothingDamagedReadsWhole();
    SomethingShortOfMaximumReadsStillDamaged();
    SomethingAtZeroReadsStillBroken();
    EveryCountPairHasAnAnswer();
    OnlyACleanReadIsWhole();
    EveryReadBackHasAWord();

    AnEmptyPurseIsFinalForTheLeg();
    APermanentRefusalIsPermanentForTheLegToo();
    ACounterThatSlippedOutOfReachIsWorthAnotherPoll();
    AWallOnTheCharacterIsWorthAnotherPoll();
    AnUnknownRefusalIsWorthAnotherPoll();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a repair nobody asks for is a repair that never happens\n");
    return EXIT_SUCCESS;
}
