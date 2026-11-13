/*
 * Room for what a cast makes, and the sentence a cast that never started writes
 * (#1531).
 *
 * Measured on the dev realm 2026-11-13: guild crafters' Linen Bandage and Bolt
 * of Linen Cloth rows came back `unchanged` with `casting_after_call: false`,
 * `queued_after_call: false` and `the cast did not start and every wall this row
 * can read was clear`, reagent in hand and held still. Every one of them had
 * full bags and no part stack of the product. Spell::CheckItems refuses that
 * with SPELL_FAILED_DONT_REPORT and tells only a client.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::CastMakesFacts;
using OverseerDecisions::CastNothingDetail;
using OverseerDecisions::CastProductHasRoom;
using OverseerDecisions::CastRefusalRetry;
using OverseerDecisions::TownRetry;
namespace CastRefusal = OverseerDecisions::CastRefusal;
namespace CastWall = OverseerDecisions::CastWall;

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

void Same(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

char const* const POST = "the cast went out and it cost the character nothing";

void ASpellThatMakesNothingAlwaysHasRoom()
{
    CastMakesFacts facts;
    facts.fits = false;
    Check("a spell with no item effect is never refused for room", CastProductHasRoom(facts));
}

// THE CRAFTERS' CASE: a full bag with no part stack of the bandage.
void AnItemThatDoesNotFitIsNoRoom()
{
    CastMakesFacts facts;
    facts.makesItem = true;
    facts.fits = false;
    facts.freeSlots = 0;
    Check("a Linen Bandage into full bags has no room", !CastProductHasRoom(facts));
}

// A part stack takes the product with no free slot, and the core says so.
void AnItemThatFitsHasRoomWithNoFreeSlot()
{
    CastMakesFacts facts;
    facts.makesItem = true;
    facts.fits = true;
    facts.freeSlots = 0;
    Check("a product that stacks onto a part stack has room", CastProductHasRoom(facts));
}

void ARandomPickNeedsAFreeSlotWhateverItPicks()
{
    CastMakesFacts facts;
    facts.makesItem = true;
    facts.randomPool = true;
    facts.fits = true;
    facts.freeSlots = 0;
    Check("a random pick with no free slot has no room", !CastProductHasRoom(facts));
    facts.freeSlots = 1;
    Check("a random pick with one free slot has room", CastProductHasRoom(facts));
}

// The mage conjure branch is the core's own; refusing it here would refuse a
// refill the core turns into a different cast.
void AnItemLimitCategoryIsLeftToTheCore()
{
    CastMakesFacts facts;
    facts.makesItem = true;
    facts.limitCategory = true;
    facts.fits = false;
    Check("a limit-category item is not refused here", CastProductHasRoom(facts));
}

void BagsAndToolsAreAnsweredElsewhere()
{
    Check("no bag room retries elsewhere",
          CastRefusalRetry(CastRefusal::NoBagRoom) == TownRetry::Elsewhere);
    Check("a missing tool retries elsewhere",
          CastRefusalRetry(CastRefusal::MissingTool) == TownRetry::Elsewhere);
    Check("the two new refusals read differently",
          std::string(CastRefusal::NoBagRoom) != CastRefusal::MissingTool);
    Check("the bag refusal carries no quote",
          std::string(CastRefusal::NoBagRoom).find('\'') == std::string::npos);
    Check("the tool refusal carries no quote",
          std::string(CastRefusal::MissingTool).find('\'') == std::string::npos);
}

// THE ROW STOPS SAYING `the cast went out` ABOUT A CAST THAT DID NOT.
void ACastThatNeverStartedNamesTheCoreAnswer()
{
    Same("the core's answer is named",
         CastNothingDetail(false, false, CastWall::CoreRefused, "SPELL_FAILED_DONT_REPORT", POST),
         "the cast did not start: the core answered SPELL_FAILED_DONT_REPORT");
}

void ACastThatNeverStartedWithNoAnswerNamesTheWall()
{
    Same("the wall is named when the core said nothing",
         CastNothingDetail(false, false, CastWall::Mounted, "", POST), CastWall::Mounted);
    Same("the honest unknown is still honest",
         CastNothingDetail(false, false, CastWall::NoneNamed, "", POST), CastWall::NoneNamed);
}

void ACastSeenOrQueuedKeepsThePostCondition()
{
    Same("a cast seen running keeps the post-condition sentence",
         CastNothingDetail(true, false, "", "", POST), POST);
    Same("a parked cast keeps the post-condition sentence",
         CastNothingDetail(false, true, CastWall::Queued, "", POST), POST);
}

void AnEmptyWallAndNoAnswerFallsBackToThePostCondition()
{
    Same("nothing read at all keeps the old sentence", CastNothingDetail(false, false, "", "", POST),
         POST);
}

} // namespace

int main()
{
    ASpellThatMakesNothingAlwaysHasRoom();
    AnItemThatDoesNotFitIsNoRoom();
    AnItemThatFitsHasRoomWithNoFreeSlot();
    ARandomPickNeedsAFreeSlotWhateverItPicks();
    AnItemLimitCategoryIsLeftToTheCore();
    BagsAndToolsAreAnsweredElsewhere();
    ACastThatNeverStartedNamesTheCoreAnswer();
    ACastThatNeverStartedWithNoAnswerNamesTheWall();
    ACastSeenOrQueuedKeepsThePostCondition();
    AnEmptyWallAndNoAnswerFallsBackToThePostCondition();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok - a cast knows when the bags cannot take what it makes\n");
    return 0;
}
