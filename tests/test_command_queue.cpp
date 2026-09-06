/*
 * The command queue's claim lease and its voice, decided without a world.
 *
 * WHAT IT IS FOR. On 2026-09-05 the command queue consumed nothing for twenty
 * five minutes while the module tick ran perfectly normally beside it. The
 * drain was healthy; it was spending its whole per-poll budget re-attempting
 * the same twenty rows, the same twenty ids every two seconds, all of them
 * sales refused with "vendor not in range" because the sellers were nowhere
 * near a shop. Twenty is COMMANDS_PER_POLL, so the 322 rows queued behind them
 * were never read at all (mod-overseer#230).
 *
 * The retry that did that is DELETED at the call site rather than tuned, so
 * there is no retry rule here to test. What is here is the two things deleting
 * it does not fix.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - A PERMANENTLY FAILING HEAD IS CALLED OUT, NOT TOLERATED. A poll that had
 *     rows in its hands and executed none of them is the exact shape of #230,
 *     and it is said on the FIRST such poll rather than after an age has
 *     accumulated. This is the case the whole incident is about: the condition
 *     was true eight hundred polls running and the log never once mentioned it.
 *   - STUCK OUTRANKS DEEP. A wedged queue is also a deep one, and reporting the
 *     deep half of that would be a true sentence that reads as reassurance.
 *   - THE COMPLAINT IS RATE LIMITED, BUT THE FIRST ONE IS NOT. A per-poll
 *     condition given a per-poll line is its own outage (the give backoff next
 *     door was written for that exact lesson), and a first complaint delayed by
 *     a rate limit is a beginning nobody sees.
 *   - IT ENDS. A queue that recovers says so once, so the log has an end as
 *     well as a beginning and a reader is not left assuming the worst.
 *   - A CLAIM THIS RUN HOLDS IS NEVER ABANDONED. The lease exists to collect
 *     rows left behind by a run that went away; firing it on a live claim would
 *     have the drain cancelling its own in-flight work.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ClaimIsAbandoned;
using OverseerDecisions::CommandQueueSay;
using OverseerDecisions::CommandQueueSnapshot;
using OverseerDecisions::CommandQueueVoice;
using OverseerDecisions::CommandQueueVoiceLimits;
using OverseerDecisions::CommandQueueVoiceState;

namespace
{

// The numbers mod_overseer.cpp passes in, named here so a failure reads as the
// rule rather than as a handful of integers.
CommandQueueVoiceLimits const LIMITS = []
{
    CommandQueueVoiceLimits l;
    l.stuckSeconds = 120;
    l.deepRows = 100;
    l.repeatSeconds = 60;
    return l;
}();

time_t const LEASE = 120;

// An arbitrary but fixed wall clock, so every `now` below reads as an offset
// from one moment rather than as an epoch second nobody can check.
time_t const T0 = 1000000;

int failures = 0;

char const* Word(CommandQueueVoice voice)
{
    switch (voice)
    {
        case CommandQueueVoice::Silent:
            return "Silent";
        case CommandQueueVoice::Deep:
            return "Deep";
        case CommandQueueVoice::Stuck:
            return "Stuck";
        case CommandQueueVoice::Recovered:
            return "Recovered";
    }
    return "?";
}

void CheckVoice(char const* what, CommandQueueVoice got, CommandQueueVoice want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Word(got), Word(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// The poll #230 was measured on: 337 rows waiting, twenty of them in the
// drain's hands, and not one of them executed.
CommandQueueSnapshot TheWedgedPoll()
{
    CommandQueueSnapshot s;
    s.pending = 337;
    s.executed = 0;
    s.held = 20;
    s.oldestPendingAge = 26 * 60;
    return s;
}

// A poll doing exactly what it should: work in hand, work done, nothing behind.
CommandQueueSnapshot AHealthyPoll()
{
    CommandQueueSnapshot s;
    s.pending = 3;
    s.executed = 12;
    s.held = 0;
    s.oldestPendingAge = 2;
    return s;
}

// ------------------------------------------------------------------------

// THE ONE THIS ISSUE IS ABOUT. A drain that selects rows and runs none of them
// is starving whatever is behind them, and it must say so on the first poll it
// does it, not after an age has built up. On 2026-09-05 this condition held for
// twenty five minutes and produced no log line at any level.
void APermanentlyFailingHeadIsCalledOut()
{
    CommandQueueVoiceState state;
    CheckVoice("a poll that ran nothing it selected is called stuck at once",
               CommandQueueSay(state, TheWedgedPoll(), T0, LIMITS), CommandQueueVoice::Stuck);
    Check("and the complaint is now standing", state.complaining, true);

    // ...and it is the RUNNING NOTHING that does it, not the age. A shallow,
    // young queue that the drain still could not move is the same defect on its
    // first poll, which is the moment worth catching it at.
    CommandQueueVoiceState fresh;
    CommandQueueSnapshot young;
    young.pending = 20;
    young.executed = 0;
    young.held = 20;
    young.oldestPendingAge = 0;
    CheckVoice("a young queue the drain cannot move is stuck too",
               CommandQueueSay(fresh, young, T0, LIMITS), CommandQueueVoice::Stuck);
}

// The other half of "not being reached": rows that are simply never selected.
// Nothing is held, nothing is running, and the oldest row keeps aging.
void ARowNothingIsReachingIsCalledOut()
{
    CommandQueueVoiceState state;
    CommandQueueSnapshot aging;
    aging.pending = 8;
    aging.executed = 0;
    aging.held = 0;
    aging.oldestPendingAge = LIMITS.stuckSeconds - 1;
    CheckVoice("inside the age limit a small idle queue is not a complaint",
               CommandQueueSay(state, aging, T0, LIMITS), CommandQueueVoice::Silent);

    aging.oldestPendingAge = LIMITS.stuckSeconds;
    CheckVoice("at the age limit it is",
               CommandQueueSay(state, aging, T0, LIMITS), CommandQueueVoice::Stuck);
}

// A queue that is moving well is not worth a word, however ordinary the poll.
void AHealthyQueueSaysNothing()
{
    CommandQueueVoiceState state;
    CheckVoice("a healthy poll is silent", CommandQueueSay(state, AHealthyPoll(), T0, LIMITS),
               CommandQueueVoice::Silent);
    Check("and nothing is standing", state.complaining, false);

    CommandQueueSnapshot empty;
    CheckVoice("an empty queue is silent", CommandQueueSay(state, empty, T0 + 2, LIMITS),
               CommandQueueVoice::Silent);
}

// A deep queue that IS draining is worth one line, because it is the difference
// between "slow" and "wedged" and a reader should not have to guess.
void ADeepQueueThatIsMovingIsMentioned()
{
    CommandQueueVoiceState state;
    CommandQueueSnapshot busy;
    busy.pending = LIMITS.deepRows;
    busy.executed = 20;
    busy.held = 0;
    busy.oldestPendingAge = 4;
    CheckVoice("a deep queue that is moving is mentioned",
               CommandQueueSay(state, busy, T0, LIMITS), CommandQueueVoice::Deep);

    busy.pending = LIMITS.deepRows - 1;
    CommandQueueVoiceState quiet;
    CheckVoice("one row shallower it is not",
               CommandQueueSay(quiet, busy, T0, LIMITS), CommandQueueVoice::Silent);
}

// STUCK OUTRANKS DEEP. The measured poll was both; calling it deep would be a
// true sentence that reads as reassurance.
void StuckOutranksDeep()
{
    CommandQueueVoiceState state;
    CommandQueueSnapshot both = TheWedgedPoll();
    Check("the measured poll really is deep as well as stuck",
          both.pending >= LIMITS.deepRows, true);
    CheckVoice("and it is reported as stuck", CommandQueueSay(state, both, T0, LIMITS),
               CommandQueueVoice::Stuck);
}

// The poll is every two seconds. A complaint per poll is its own outage.
void TheComplaintIsRateLimited()
{
    CommandQueueVoiceState state;
    CheckVoice("the first bad poll speaks", CommandQueueSay(state, TheWedgedPoll(), T0, LIMITS),
               CommandQueueVoice::Stuck);
    CheckVoice("the next one, two seconds later, does not",
               CommandQueueSay(state, TheWedgedPoll(), T0 + 2, LIMITS),
               CommandQueueVoice::Silent);
    CheckVoice("nor does one a second before the window is up",
               CommandQueueSay(state, TheWedgedPoll(), T0 + LIMITS.repeatSeconds - 1, LIMITS),
               CommandQueueVoice::Silent);
    CheckVoice("and then it says so again",
               CommandQueueSay(state, TheWedgedPoll(), T0 + LIMITS.repeatSeconds, LIMITS),
               CommandQueueVoice::Stuck);
}

// ...but a rate limit must never delay the START of an outage, which is the one
// moment worth being loud at.
void TheFirstComplaintIsNeverHeldBack()
{
    CommandQueueVoiceState state;
    // A queue that has just recovered, so `lastSaid` is a second old and no
    // complaint is standing.
    CommandQueueSay(state, TheWedgedPoll(), T0, LIMITS);
    CheckVoice("it recovers", CommandQueueSay(state, AHealthyPoll(), T0 + 1, LIMITS),
               CommandQueueVoice::Recovered);
    CheckVoice("and going wrong again one second later is said immediately",
               CommandQueueSay(state, TheWedgedPoll(), T0 + 2, LIMITS),
               CommandQueueVoice::Stuck);
}

// A complaint that never ends leaves a reader assuming the worst forever.
void RecoveryIsSaidOnceAndOnlyOnce()
{
    CommandQueueVoiceState state;
    CommandQueueSay(state, TheWedgedPoll(), T0, LIMITS);
    CheckVoice("recovery is said", CommandQueueSay(state, AHealthyPoll(), T0 + 10, LIMITS),
               CommandQueueVoice::Recovered);
    CheckVoice("and not said again", CommandQueueSay(state, AHealthyPoll(), T0 + 12, LIMITS),
               CommandQueueVoice::Silent);
    CheckVoice("nor much later", CommandQueueSay(state, AHealthyPoll(), T0 + 6000, LIMITS),
               CommandQueueVoice::Silent);
    Check("and nothing is standing any more", state.complaining, false);
}

// A queue with nothing in it cannot be starving anybody, whatever the other
// numbers happen to read.
void AnEmptyQueueIsNeverStuck()
{
    CommandQueueVoiceState state;
    CommandQueueSnapshot drained;
    drained.pending = 0;
    drained.executed = 20;
    drained.held = 0;
    drained.oldestPendingAge = 99999;  // stale reading of a queue that just emptied
    CheckVoice("no rows waiting is never a complaint",
               CommandQueueSay(state, drained, T0, LIMITS), CommandQueueVoice::Silent);
}

// ------------------------------------------------------------------------

void AClaimThisRunHoldsIsNeverAbandoned()
{
    std::string const ours = "0123456789abcdef";
    Check("our own claim is live however old it looks",
          ClaimIsAbandoned(ours, ours, 10 * LEASE, LEASE), false);
    Check("and a fresh one of ours obviously is",
          ClaimIsAbandoned(ours, ours, 1, LEASE), false);
}

void AClaimYoungerThanTheLeaseIsLive()
{
    std::string const ours = "0123456789abcdef";
    std::string const theirs = "fedcba9876543210";
    Check("another run's claim inside the lease is left alone",
          ClaimIsAbandoned(theirs, ours, LEASE - 1, LEASE), false);
    Check("at the lease it is abandoned",
          ClaimIsAbandoned(theirs, ours, LEASE, LEASE), true);
    Check("and well past it, certainly",
          ClaimIsAbandoned(theirs, ours, 10 * LEASE, LEASE), true);
}

void AClaimWithNoHolderIsAbandonedOnceItIsOld()
{
    std::string const ours = "0123456789abcdef";
    Check("an empty holder inside the lease is still left alone",
          ClaimIsAbandoned("", ours, LEASE - 1, LEASE), false);
    Check("but past the lease nothing can be holding it",
          ClaimIsAbandoned("", ours, LEASE, LEASE), true);
    // The run token is empty only before it has been made, which cannot happen
    // from the drain. Asserted anyway so an empty token never reads as "every
    // unheld row is mine" and quietly disables the lease for the whole table.
    Check("an empty run token does not adopt every unheld row",
          ClaimIsAbandoned("", "", LEASE, LEASE), true);
}

}  // namespace

int main()
{
    APermanentlyFailingHeadIsCalledOut();
    ARowNothingIsReachingIsCalledOut();
    AHealthyQueueSaysNothing();
    ADeepQueueThatIsMovingIsMentioned();
    StuckOutranksDeep();
    TheComplaintIsRateLimited();
    TheFirstComplaintIsNeverHeldBack();
    RecoveryIsSaidOnceAndOnlyOnce();
    AnEmptyQueueIsNeverStuck();
    AClaimThisRunHoldsIsNeverAbandoned();
    AClaimYoungerThanTheLeaseIsLive();
    AClaimWithNoHolderIsAbandonedOnceItIsOld();

    if (failures != 0)
    {
        std::printf("%d command queue check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the command queue lease and voice hold\n");
    return EXIT_SUCCESS;
}
