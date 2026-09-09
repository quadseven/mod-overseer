/*
 * The last few yards to a summoning stone.
 *
 * mod-overseer#355. `kind='summon'` is the module's own stated answer for a
 * character stranded on another continent, and an operator could not land one
 * in seven attempts over forty minutes on the dev realm on 2026-09-09. The
 * reason is structural rather than luck: ONLY THE LEADER EVER REACHES THE
 * STONE. A leader can be aimed and on its aim it reached three yards; a
 * FOLLOWER walks to the LEADER and stops at follow distance, which parked the
 * two best candidates 15 and 16 yards from a stone whose gate is about five.
 *
 *   07:00:34  Bork 15y, Og 16y  ->  error | summoner is moving
 *   07:00:43  Bork 55y          ->  error | no meeting stone within reach
 *
 * The hold #335 and #338 built was already on both of them and #350's dismount
 * was working inside it. A hold pins a character WHERE IT ALREADY STANDS, so
 * none of that was ever going to close fifteen yards.
 *
 * What is pinned here is the decision the verb now makes on every poll: click,
 * walk, give up, or say there is no stone. The walking itself is a MovePoint
 * under the hold and lives in the adapter, where it can be read against the
 * pinned core; the rule about WHEN to walk is a decision and lives here.
 *
 * The refusal literals the walk can produce are pinned too, because their retry
 * class is what a sender acts on: ground that does not hold is a place problem
 * and says `elsewhere`, a walk that ran out of time is not and says `later`.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadSummonApproach;
using OverseerDecisions::SummonApproach;
using OverseerDecisions::SummonApproachWord;
using OverseerDecisions::SummonRefusalRetry;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

namespace
{

int failures = 0;

void CheckApproach(char const* what, SummonApproach got, SummonApproach want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, SummonApproachWord(got),
                SummonApproachWord(want));
    ++failures;
}

void CheckRetry(char const* what, TownRetry got, TownRetry want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, TownRetryWord(got), TownRetryWord(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

// The numbers mod_overseer.cpp passes in, named here so a failure reads as the
// case it is rather than as four bare floats. SUMMON_APPROACH_YARDS is the
// sweep's own width and SUMMON_APPROACH_CEILING_MS is twenty seconds.
constexpr float WALK_YARDS = 40.f;
constexpr uint32_t CEILING_MS = 20000;

// -------------------------------------------------------------------------
// THE MEASUREMENT ITSELF. Both clickers fifteen and sixteen yards out, well
// inside the sweep, with the clock barely started. Every attempt in the issue
// was this state, and every attempt answered it with a refusal.
void TheMeasuredPairIsWalked()
{
    CheckApproach("fifteen yards out is a walk",
                  ReadSummonApproach(false, 15.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::Walk);
    CheckApproach("sixteen yards out is a walk",
                  ReadSummonApproach(false, 16.f, WALK_YARDS, 2000, CEILING_MS),
                  SummonApproach::Walk);
}

// THE GATE IS AN INPUT AND NOT A DISTANCE READ OFF A FLOAT. The comparison the
// core's handler makes is WorldObject::IsWithinDistInMap against the object's
// interaction distance - three dimensions, both bounding radii subtracted, map
// and phase checked - so the executor asks the core and hands the answer in. A
// decision that re-derived it here would disagree with the handler by a hand's
// breadth, and a row that believes it has arrived while the handler drops the
// click in silence is a row that clicks for ever.
void TheCoresOwnVerdictDecidesClicking()
{
    CheckApproach("in reach is a click whatever the distance reads",
                  ReadSummonApproach(true, 6.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::Click);
    CheckApproach("out of reach is not a click at a flattering distance",
                  ReadSummonApproach(false, 4.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::Walk);
}

// A READING NOBODY TOOK IS NOT A DISTANCE. The sweep leaves the distance
// negative when it found nothing stone-shaped at all, and that is asked before
// the reach verdict so a caller that hands in `true` about a stone it never
// found cannot be answered `Click`.
void NoStoneSeenIsNeverAClick()
{
    CheckApproach("nothing swept is no stone",
                  ReadSummonApproach(false, -1.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::NoStone);
    CheckApproach("a reach verdict about nothing is still no stone",
                  ReadSummonApproach(true, -1.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::NoStone);
}

// FURTHER THAN THIS VERB WILL WALK IS THE SAME ANSWER AS NO STONE AT ALL. The
// sweep width and the walk are the same number in the adapter today, so this
// branch is what keeps the rule true if either of them ever moves - and it is
// the reason the two are not merged into one comparison.
void AStoneBeyondTheWalkReadsAsNoStone()
{
    CheckApproach("a yard inside the walk is walked",
                  ReadSummonApproach(false, 39.9f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::Walk);
    CheckApproach("exactly the walk is still walked",
                  ReadSummonApproach(false, WALK_YARDS, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::Walk);
    CheckApproach("a yard beyond the walk is no stone",
                  ReadSummonApproach(false, 41.f, WALK_YARDS, 0, CEILING_MS),
                  SummonApproach::NoStone);
}

// ARRIVAL IS ASKED BEFORE THE CLOCK, WHICH IS THE ONE ORDERING THAT MATTERS
// HERE. A clicker that reaches the stone on the very poll its walk runs out has
// arrived, and answering `OutOfTime` about a character standing on the stone
// would throw away a summon that was about to work - a whole ritual, and the
// forty minutes of asking that produced this issue.
void ArrivingOnTheLastPollStillCounts()
{
    CheckApproach("in reach beats an expired clock",
                  ReadSummonApproach(true, 3.f, WALK_YARDS, CEILING_MS, CEILING_MS),
                  SummonApproach::Click);
    CheckApproach("in reach beats a clock long past",
                  ReadSummonApproach(true, 3.f, WALK_YARDS, CEILING_MS * 10, CEILING_MS),
                  SummonApproach::Click);
    CheckApproach("still walking when the clock runs out gives up",
                  ReadSummonApproach(false, 12.f, WALK_YARDS, CEILING_MS, CEILING_MS),
                  SummonApproach::OutOfTime);
    CheckApproach("one millisecond short of the ceiling still walks",
                  ReadSummonApproach(false, 12.f, WALK_YARDS, CEILING_MS - 1, CEILING_MS),
                  SummonApproach::Walk);
}

// A ceiling of zero is not a walk with no time, it is a caller that has said
// "do not walk", and the answer has to be an honest give-up rather than a row
// that walks one poll anyway. Reachable through a mis-set constant, which is
// exactly the kind of thing that should fail loudly rather than half work.
void AZeroCeilingWalksNobody()
{
    CheckApproach("a zero ceiling gives up at once",
                  ReadSummonApproach(false, 12.f, WALK_YARDS, 0, 0), SummonApproach::OutOfTime);
    CheckApproach("...but a clicker already there is still a click",
                  ReadSummonApproach(true, 2.f, WALK_YARDS, 0, 0), SummonApproach::Click);
}

// The words go into no row of their own, but they are what a log line and a
// failure message read, and a word that drifts from its enum is the kind of
// thing that is only noticed while diagnosing something else.
void TheWordsAreTheOnesARowCarries()
{
    CheckWord("no-stone", SummonApproachWord(SummonApproach::NoStone), "no-stone");
    CheckWord("click", SummonApproachWord(SummonApproach::Click), "click");
    CheckWord("walk", SummonApproachWord(SummonApproach::Walk), "walk");
    CheckWord("out-of-time", SummonApproachWord(SummonApproach::OutOfTime), "out-of-time");
}

// THE RETRY CLASS IS WHAT A SENDER ACTS ON, and the walk adds refusals in both
// classes, which is the whole reason they are worth pinning together.
//
// Ground that does not hold is a statement about a PLACE: the same clicker
// standing on the other side of the stone would be walked without an argument
// and waiting where it is changes nothing, so it is `elsewhere` beside the
// three refusals that were already about place.
//
// A walk that ran out of time is not. The clickers ended the row nearer the
// stone than they started it, so the next row walks a shorter distance from a
// better place - which is what `later` means and what `elsewhere` would have
// told the sender to stop doing.
void TheWalksRefusalsAskForTheRightThing()
{
    CheckRetry("ground under the summoner is a place",
               SummonRefusalRetry("the ground between the summoner and the meeting stone does "
                                  "not hold"),
               TownRetry::Elsewhere);
    CheckRetry("ground under the second clicker is a place",
               SummonRefusalRetry("the ground between the second clicker and the meeting stone "
                                  "does not hold"),
               TownRetry::Elsewhere);
    CheckRetry("a walk that ran out of time is asked again",
               SummonRefusalRetry("the clickers did not reach the meeting stone in time"),
               TownRetry::Later);

    // AND THE THREE THAT WERE ALREADY THERE KEEP THEIR CLASS. The approach
    // reordered DoSummon and rewrote what "no second party member is at the
    // stone" means - the scan no longer skips a candidate for merely walking or
    // merely being far away - but what a sender should DO about each of them is
    // unchanged, and readers outside this repository key on that.
    CheckRetry("no stone swept is still a place",
               SummonRefusalRetry("no meeting stone within reach of the summoner"),
               TownRetry::Elsewhere);
    CheckRetry("no clicker at the portal is still a place",
               SummonRefusalRetry("no meeting stone within reach of the second clicker"),
               TownRetry::Elsewhere);
    CheckRetry("nobody in the party near the stone is still a place",
               SummonRefusalRetry("no second party member is at the stone"),
               TownRetry::Elsewhere);
    CheckRetry("a moving summoner is still asked again",
               SummonRefusalRetry("summoner is moving"), TownRetry::Later);
    CheckRetry("a moving second clicker is still asked again",
               SummonRefusalRetry("the second clicker is moving"), TownRetry::Later);
}

// THE WHOLE WALK, POLL BY POLL, because the property that matters is not any
// one answer but that the sequence TERMINATES. A row that could answer `Walk`
// for ever would hold two characters beside a stone their party is trying to
// use until the claim lease reaped it with nothing to say.
//
// Two runs over the same clock: one where the clickers close the distance and
// one where nothing moves at all. The first must end in a click and the second
// must end in a give-up, and neither may run past the ceiling.
void EveryWalkEnds()
{
    constexpr uint32_t POLL_MS = 2000;  // COMMAND_POLL_MS

    // A walk that closes: fifteen yards, and about seven yards a poll.
    {
        float yards = 15.f;
        uint32_t walked = 0;
        SummonApproach answer = SummonApproach::Walk;
        int polls = 0;
        while (answer == SummonApproach::Walk && polls < 100)
        {
            ++polls;
            walked += POLL_MS;
            yards = yards > 7.f ? yards - 7.f : 0.f;
            answer = ReadSummonApproach(yards <= 5.f, yards, WALK_YARDS, walked, CEILING_MS);
        }
        CheckApproach("a closing walk ends in a click", answer, SummonApproach::Click);
        if (walked > CEILING_MS)
        {
            std::printf("FAIL a closing walk ran past the ceiling: %ums\n", walked);
            ++failures;
        }
    }

    // A walk that does not close at all: something outside this module is
    // holding the character where it is.
    {
        uint32_t walked = 0;
        SummonApproach answer = SummonApproach::Walk;
        int polls = 0;
        while (answer == SummonApproach::Walk && polls < 100)
        {
            ++polls;
            walked += POLL_MS;
            answer = ReadSummonApproach(false, 15.f, WALK_YARDS, walked, CEILING_MS);
        }
        CheckApproach("a stuck walk gives up", answer, SummonApproach::OutOfTime);
        if (polls >= 100)
        {
            std::printf("FAIL a stuck walk never ended\n");
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    TheMeasuredPairIsWalked();
    TheCoresOwnVerdictDecidesClicking();
    NoStoneSeenIsNeverAClick();
    AStoneBeyondTheWalkReadsAsNoStone();
    ArrivingOnTheLastPollStillCounts();
    AZeroCeilingWalksNobody();
    TheWordsAreTheOnesARowCarries();
    TheWalksRefusalsAskForTheRightThing();
    EveryWalkEnds();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the verb closes the last few yards, and every walk ends\n");
    return EXIT_SUCCESS;
}
