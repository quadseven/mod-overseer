/*
 * A COMPLETE chosen quest is handed in, not walked toward for ever (#180).
 *
 * The family held quest 90 COMPLETE for half an hour while the quest drive
 * re-issued ChangeToDoQuest for it. The re-issue reset the walk back to the
 * giver, and the hand-in itself was left to upstream's own search for a
 * quest giver, which refuses a reward it cannot store and gives the quest up
 * after five minutes at the point of interest. So nothing in the module ever
 * handed the quest in, and the aim was only released once something else had.
 *
 * ChosenQuestStepFor is the whole decision DriveChosenQuest acts on:
 *   - a quest the character does not hold is not aimed at all;
 *   - a COMPLETE quest whose taker is in reach and whose reward the core will
 *     give is handed in, whatever the lease currently names;
 *   - otherwise a lease already naming the quest is left alone, unless the aim
 *     has just changed, when it is re-issued once so a stale objective pointer
 *     resets (the regression the old guard was written to prevent);
 *   - otherwise the quest is asserted.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ChosenQuestFacts;
using OverseerDecisions::ChosenQuestStep;
using OverseerDecisions::ChosenQuestStepFor;
using OverseerDecisions::ChosenQuestStepName;

namespace
{
int failures = 0;

constexpr int STATUS_NONE = 0;
constexpr int STATUS_COMPLETE = 1;
constexpr int STATUS_INCOMPLETE = 3;
constexpr int STATUS_REWARDED = 6;

ChosenQuestFacts Facts(int status, bool working, bool aimChanged, bool handInReady)
{
    ChosenQuestFacts facts;
    facts.status = status;
    facts.working = working;
    facts.aimChanged = aimChanged;
    facts.handInReady = handInReady;
    return facts;
}

void Expect(char const* what, ChosenQuestFacts const& facts, ChosenQuestStep want)
{
    ChosenQuestStep const got = ChosenQuestStepFor(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, ChosenQuestStepName(got),
                ChosenQuestStepName(want));
    ++failures;
}

// The measured case: COMPLETE, the lease names it, the aim did not move, and
// the character is standing at the giver. The old drive left it alone here and
// relied on upstream; the drive must hand it in.
void ACompleteQuestAtItsTakerIsHandedIn()
{
    Expect("complete, working, steady, at the taker",
           Facts(STATUS_COMPLETE, true, false, true), ChosenQuestStep::HandIn);
    Expect("complete, working, just aimed, at the taker",
           Facts(STATUS_COMPLETE, true, true, true), ChosenQuestStep::HandIn);
    Expect("complete, lease lapsed, at the taker",
           Facts(STATUS_COMPLETE, false, false, true), ChosenQuestStep::HandIn);
    Expect("complete, newly aimed, at the taker",
           Facts(STATUS_COMPLETE, false, true, true), ChosenQuestStep::HandIn);
}

// Not at the taker yet (or the core will not reward it): the walk back is
// upstream's DoQuest, and a steady lease is not re-issued, because every
// re-issue restarts that walk.
void ACompleteQuestAwayFromItsTakerKeepsWalking()
{
    Expect("complete, working, steady, away",
           Facts(STATUS_COMPLETE, true, false, false), ChosenQuestStep::Leave);
    Expect("complete, lease lapsed, away",
           Facts(STATUS_COMPLETE, false, false, false), ChosenQuestStep::Assert);
}

// The protection the old guard was written for stays: a NEW errand naming a
// quest the bot was already on by its own choice resets the stale pointer once.
void ANewErrandStillResetsAStalePointer()
{
    Expect("complete, working, just aimed, away",
           Facts(STATUS_COMPLETE, true, true, false), ChosenQuestStep::Reissue);
    Expect("incomplete, working, just aimed",
           Facts(STATUS_INCOMPLETE, true, true, false), ChosenQuestStep::Reissue);
}

void AnIncompleteQuestIsNeverHandedIn()
{
    Expect("incomplete, working, steady",
           Facts(STATUS_INCOMPLETE, true, false, false), ChosenQuestStep::Leave);
    Expect("incomplete, lease lapsed",
           Facts(STATUS_INCOMPLETE, false, false, false), ChosenQuestStep::Assert);
    // A stray `ready` on an incomplete quest must not reward it.
    Expect("incomplete, ready flag set",
           Facts(STATUS_INCOMPLETE, true, false, true), ChosenQuestStep::Leave);
}

void AQuestNotHeldIsNotAimed()
{
    Expect("not in the log", Facts(STATUS_NONE, false, true, false), ChosenQuestStep::NotHeld);
    Expect("already rewarded", Facts(STATUS_REWARDED, false, false, true),
           ChosenQuestStep::NotHeld);
}
}  // namespace

int main()
{
    ACompleteQuestAtItsTakerIsHandedIn();
    ACompleteQuestAwayFromItsTakerKeepsWalking();
    ANewErrandStillResetsAStalePointer();
    AnIncompleteQuestIsNeverHandedIn();
    AQuestNotHeldIsNotAimed();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a complete chosen quest at its taker is handed in\n");
    return EXIT_SUCCESS;
}
