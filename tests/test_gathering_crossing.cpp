/*
 * A run that gathers across an ocean crosses first, on a clock that waits.
 *
 * Measured 2026-10-09 on the dev realm: of 55 family campaign runs in 72
 * hours, 12 closed at GATHERING's twelve minutes with the leader on the other
 * continent ("on map 1 rather than map 0") or about 2,400 yards out. The log
 * of the latest one: the zeppelin docked at the far tower with the leader on
 * its deck, the crossing read WalkOff, and IDLE opened the run on that same
 * poll. Nothing read the crossing again, so its hold stayed on the leader and
 * he stood rooted at the foot of the tower until the twelve minutes ran out.
 * Earlier attempts left him on the deck as it sailed back, and GATHERING,
 * which re-armed its walk only on the door's own map, waited there instead.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CrossingAction;
using OverseerDecisions::CrossingHoldsTheFamily;
using OverseerDecisions::GatheringCrossing;
using OverseerDecisions::GatheringCrossingBlocker;
using OverseerDecisions::GatheringCrossingStep;
using OverseerDecisions::GatheringReadsCrossing;

namespace
{

int failures = 0;

// The adapter's own numbers: the staging backstop, the crossing's backstop
// (the budget) and the coordinator's poll.
constexpr std::uint32_t STAGING = 12 * 60;
constexpr std::uint32_t BUDGET = 1800;
constexpr std::uint32_t POLL = 5;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheRunWaitsForTheStepOffTheDeck()
{
    // The measured race: the leader on the deck at the far tower, docked.
    Check("a run does not open while the leader is being walked off the deck",
          CrossingHoldsTheFamily(CrossingAction::WalkOff));
    Check("nor while he rides", CrossingHoldsTheFamily(CrossingAction::Ride));
    Check("nor while a follower is still on the deck",
          CrossingHoldsTheFamily(CrossingAction::Disembark));
    Check("nor while he steps aboard", CrossingHoldsTheFamily(CrossingAction::Board));
    Check("nor while he steps back onto the berth",
          CrossingHoldsTheFamily(CrossingAction::StepBack));
    Check("ashore and off every transport opens it",
          !CrossingHoldsTheFamily(CrossingAction::Done));
    Check("a refusal is not a deck", !CrossingHoldsTheFamily(CrossingAction::Refuse));
    Check("a walk to the berth is not a deck", !CrossingHoldsTheFamily(CrossingAction::Walk));
}

void GatheringReadsTheCrossingIdleWould()
{
    Check("a leader on the other continent is read",
          GatheringReadsCrossing(1, 0, false, 0));
    Check("a leader on the door's map with nobody aboard is not",
          !GatheringReadsCrossing(0, 0, false, 1));
    Check("a family still aboard from a crossing out of map 1 is",
          GatheringReadsCrossing(0, 0, true, 1));
    Check("passengers of a crossing that started on the door's map are not",
          !GatheringReadsCrossing(0, 0, true, 0));
}

void EachCrossingAnswerHasOneGatheringAnswer()
{
    Check("a refusal leaves GATHERING to its own backstop",
          GatheringCrossingStep(CrossingAction::Refuse, 10, BUDGET) == GatheringCrossing::Gather);
    Check("ashore is a landing",
          GatheringCrossingStep(CrossingAction::Done, 10, BUDGET) == GatheringCrossing::Landed);
    Check("a split at the berth ends the attempt",
          GatheringCrossingStep(CrossingAction::Fetch, 10, BUDGET) == GatheringCrossing::Ended);
    Check("an unreadable member earns no clock",
          GatheringCrossingStep(CrossingAction::Wait, 10, BUDGET) ==
              GatheringCrossing::CrossTimed);
    Check("walking to the berth holds the clock",
          GatheringCrossingStep(CrossingAction::Walk, 10, BUDGET) == GatheringCrossing::CrossHeld);
    Check("riding at the last second of the budget holds it",
          GatheringCrossingStep(CrossingAction::Ride, BUDGET, BUDGET) ==
              GatheringCrossing::CrossHeld);
    Check("holding at the berth past the budget does not",
          GatheringCrossingStep(CrossingAction::Hold, BUDGET + 1, BUDGET) ==
              GatheringCrossing::CrossTimed);
}

// One attempt, polled every five seconds, the way GATHERING reads it: the
// clock restarts on CrossHeld and on Landed, and the attempt is written off
// when it is older than the staging backstop. Returns the second it was
// written off, or 0 if it never was.
template <typename ActionAt>
std::uint32_t WrittenOffAt(ActionAt actionAt, std::uint32_t until)
{
    std::uint32_t since = 0;
    for (std::uint32_t t = 0; t <= until; t += POLL)
    {
        GatheringCrossing const step = GatheringCrossingStep(actionAt(t), t, BUDGET);
        if (step == GatheringCrossing::CrossHeld || step == GatheringCrossing::Landed)
            since = t;
        if (t - since > STAGING)
            return t;
        if (step == GatheringCrossing::Landed)
            return 0;
    }
    return 0;
}

void ACrossingLongerThanTheTwelveMinutesLands()
{
    // Five minutes to the tower, seven waiting for the zeppelin, four aboard,
    // a minute off the deck: seventeen minutes, five more than the backstop.
    auto const zeppelin = [](std::uint32_t t) {
        if (t < 5 * 60)
            return CrossingAction::Walk;
        if (t < 12 * 60)
            return CrossingAction::Hold;
        if (t < 16 * 60)
            return CrossingAction::Ride;
        if (t < 17 * 60)
            return CrossingAction::WalkOff;
        return CrossingAction::Done;
    };
    Check("a seventeen-minute crossing is not written off on the way",
          WrittenOffAt(zeppelin, 20 * 60) == 0);
}

void ACrossingThatNeverLandsIsStillBounded()
{
    // A berth whose boat never docks: the hold lasts for ever.
    auto const neverDocks = [](std::uint32_t) { return CrossingAction::Hold; };
    std::uint32_t const at = WrittenOffAt(neverDocks, 2 * 3600);
    Check("a crossing that never lands is written off", at != 0);
    Check("but not before its own budget is spent", at > BUDGET);
    Check("and no later than the twelve minutes after it", at <= BUDGET + STAGING + POLL);
}

void TheBlockerSaysWhereAndWhat()
{
    std::string const away =
        GatheringCrossingBlocker("Lead", 1, 0, CrossingAction::Hold, 31 * 60);
    Check("the blocker names the wrong map",
          away.find("on map 1 rather than map 0") != std::string::npos);
    Check("the blocker names the minutes", away.find("crossing for 31 minutes") != std::string::npos);
    Check("the blocker names the reading", away.find("'hold'") != std::string::npos);
    std::string const aboard =
        GatheringCrossingBlocker("Lead", 0, 0, CrossingAction::Disembark, 1800);
    Check("a family landed but still aboard says so",
          aboard.find("on map 0 with the family still on a deck") != std::string::npos);

    // Each answer reads differently in the log, so a line about a clock that
    // waits cannot be mistaken for one about a clock that runs.
    std::string const held = OverseerDecisions::GatheringCrossingMeaning(
        GatheringCrossing::CrossHeld);
    std::string const timed = OverseerDecisions::GatheringCrossingMeaning(
        GatheringCrossing::CrossTimed);
    std::string const landed = OverseerDecisions::GatheringCrossingMeaning(
        GatheringCrossing::Landed);
    Check("a held clock says it waits", held.find("waits") != std::string::npos);
    Check("a running clock says it runs", timed.find("runs") != std::string::npos);
    Check("a landing says the walk is a new leg", landed.find("new leg") != std::string::npos);
}

}  // namespace

int main()
{
    TheRunWaitsForTheStepOffTheDeck();
    GatheringReadsTheCrossingIdleWould();
    EachCrossingAnswerHasOneGatheringAnswer();
    ACrossingLongerThanTheTwelveMinutesLands();
    ACrossingThatNeverLandsIsStillBounded();
    TheBlockerSaysWhereAndWhat();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
