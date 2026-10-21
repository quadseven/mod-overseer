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
 *
 * The replay at the end runs that decision poll after poll for thirty minutes,
 * behind QuestAimsAfterRead and a loader that fails one read in four, so the
 * re-issue count and the single hand-in are held over the sequence too.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using OverseerDecisions::ChosenQuestFacts;
using OverseerDecisions::ChosenQuestStep;
using OverseerDecisions::ChosenQuestStepFor;
using OverseerDecisions::ChosenQuestStepName;
using OverseerDecisions::QuestAimsAfterRead;

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

// THE POLL SEQUENCE, REPLAYED. The decisions above are each one poll; the bug
// was in the sequence. DriveQuests reads the aims through QuestAimsAfterRead,
// calls an aim that differs from the last one it saw a change, releases a
// rewarded aim, and asks ChosenQuestStepFor about the rest. This replays that
// chain at QUEST_POLL_MS (twenty seconds) for thirty minutes against a loader
// that fails one read in four, about every 75 seconds as measured, which gave
// 24 re-issues of quest 90 when a failed read was taken as an empty column.
constexpr int POLLS_IN_THIRTY_MINUTES = 30 * 60 / 20;
constexpr std::uint32_t QUEST = 90;
char const* const NAME = "bork";

struct Replay
{
    std::map<std::string, std::uint32_t> column{{NAME, QUEST}};
    std::map<std::string, std::uint32_t> aims;
    std::uint32_t lastSeen{0};
    bool working{false};
    bool rewarded{false};
    int reissues{0};
    int handIns{0};
};

bool ReadFails(int poll)
{
    return poll % 4 == 3;
}

// One DriveQuests pass for the one character; `atTaker` is whether its walk
// back has reached the giver yet.
void Poll(Replay& replay, int poll, bool atTaker)
{
    bool const readOk = !ReadFails(poll);
    std::map<std::string, std::uint32_t> const loaded =
        readOk ? replay.column : std::map<std::string, std::uint32_t>();
    replay.aims = QuestAimsAfterRead(replay.aims, loaded, readOk);
    auto const it = replay.aims.find(NAME);
    std::uint32_t const aim = it == replay.aims.end() ? 0u : it->second;
    bool const aimChanged = aim != replay.lastSeen;
    replay.lastSeen = aim;
    if (!aim)
        return;
    if (replay.rewarded)
    {
        // The primary release: ClearAim zeroes the column.
        replay.column.erase(NAME);
        replay.lastSeen = 0;
        return;
    }
    ChosenQuestStep const step = ChosenQuestStepFor(
        Facts(STATUS_COMPLETE, replay.working, aimChanged, atTaker));
    if (step == ChosenQuestStep::Reissue)
        ++replay.reissues;
    if (step == ChosenQuestStep::HandIn)
    {
        ++replay.handIns;
        replay.rewarded = true;
    }
    if (step == ChosenQuestStep::Assert)
        replay.working = true;
}

// The measured case: COMPLETE, the lease already on it by its own choice, the
// giver out of reach for the whole half hour. One reset for the new errand is
// allowed; the flaky loader must not add any.
void AFlakyLoaderDoesNotReissueASteadyAim()
{
    Replay replay;
    replay.working = true;
    for (int poll = 0; poll < POLLS_IN_THIRTY_MINUTES; ++poll)
        Poll(replay, poll, false);
    if (replay.reissues >= 10)
    {
        std::printf("FAIL flaky loader: %d re-issues of quest %u in thirty minutes\n",
                    replay.reissues, QUEST);
        ++failures;
    }
}

// The walk back reaches the giver ten minutes in: the quest is handed in once,
// the next poll that reads the aim releases it, and nothing re-aims it.
void ACompleteAimIsHandedInOnceThenCleared()
{
    Replay replay;
    int const arrives = POLLS_IN_THIRTY_MINUTES / 3;
    for (int poll = 0; poll < POLLS_IN_THIRTY_MINUTES; ++poll)
        Poll(replay, poll, poll >= arrives);
    bool const cleared = replay.column.empty() && replay.lastSeen == 0;
    if (replay.handIns != 1 || !cleared || replay.reissues)
    {
        std::printf("FAIL hand-in replay: %d hand-in(s), %d re-issue(s), aim %s\n",
                    replay.handIns, replay.reissues, cleared ? "cleared" : "still set");
        ++failures;
    }
}
}  // namespace

int main()
{
    ACompleteQuestAtItsTakerIsHandedIn();
    ACompleteQuestAwayFromItsTakerKeepsWalking();
    ANewErrandStillResetsAStalePointer();
    AnIncompleteQuestIsNeverHandedIn();
    AQuestNotHeldIsNotAimed();
    AFlakyLoaderDoesNotReissueASteadyAim();
    ACompleteAimIsHandedInOnceThenCleared();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a complete chosen quest at its taker is handed in\n");
    return EXIT_SUCCESS;
}
