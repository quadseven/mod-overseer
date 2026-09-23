/*
 * The trainer and vendor walks' pure half (#621), decided without a world.
 *
 * WHAT IT IS FOR. The mailbox walk (#569) walks a guild bot off the roster to a
 * mailbox. The site's guild crafting corps needs the same bot at a profession
 * trainer, to buy the recipes its skill now allows, and at a vendor, to buy the
 * thread every tailored bag takes. These walks reuse the mailbox walk's gate,
 * hold, legs and verdict; what is new, and pinned here, is:
 *
 *   - The two grammars, and that the dispatch knows each row by its first word
 *     so a malformed one still reaches its own parser.
 *   - Which refusals and endings name the trainer or the vendor instead of the
 *     mailbox, and that the mailbox walk's own literals are untouched.
 *   - Which refusals are worth asking again.
 *   - The verdict of a trainer visit, from readings on both sides of it.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace OverseerDecisions;
namespace E = OverseerDecisions::ErrandWalkRefusal;
namespace M = OverseerDecisions::MailWalkRefusal;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    if (std::fabs(got - want) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

void EachRowIsKnownByItsFirstWord()
{
    Check("a trainer walk", IsTrainerWalkRow("walk-to-trainer skill:197"), true);
    Check("a malformed trainer walk still routes", IsTrainerWalkRow("walk-to-trainer now"), true);
    Check("a cast row is not a trainer walk", IsTrainerWalkRow("26745"), false);
    Check("a learn row is not a trainer walk", IsTrainerWalkRow("use item:12"), false);
    Check("a prefix is not a trainer walk", IsTrainerWalkRow("walk-to-trainers skill:1"), false);
    Check("a vendor walk", IsVendorWalkRow("walk-to-vendor item:14341"), true);
    Check("a buy row is not a vendor walk", IsVendorWalkRow("entry:14341 count:1"), false);
    Check("a mailbox walk is neither", IsTrainerWalkRow("walk-to-mailbox")
                                           || IsVendorWalkRow("walk-to-mailbox"),
          false);
    Check("a trainer walk is not a mailbox walk", IsMailWalkRow("walk-to-trainer skill:197"),
          false);
}

void TheTrainerGrammar()
{
    TrainerWalkRequest plain = ParseTrainerWalkRequest("walk-to-trainer skill:197");
    CheckText("the skill alone parses", plain.error, "");
    CheckNumber("the skill is read", plain.skill, 197);
    CheckNumber("no spells named", plain.learn.size(), 0);
    CheckNear("the default cap", plain.maxYards, 1000.f);

    TrainerWalkRequest full =
        ParseTrainerWalkRequest("walk-to-trainer learn:26745,26746 max:750 skill:197");
    CheckText("keys in any order", full.error, "");
    CheckNumber("two spells", full.learn.size(), 2);
    if (full.learn.size() == 2)
    {
        CheckNumber("first spell", full.learn[0], 26745);
        CheckNumber("second spell", full.learn[1], 26746);
    }
    CheckNear("the cap is read", full.maxYards, 750.f);

    char const* const bad[] = {
        "walk-to-trainer",                            // no skill
        "walk-to-trainer skill:0",
        "walk-to-trainer skill:tailoring",
        "walk-to-trainer skill:197 skill:197",
        "walk-to-trainer skill:197 learn:",
        "walk-to-trainer skill:197 learn:1,,2",
        "walk-to-trainer skill:197 learn:5,5",        // a repeat is a planner bug
        "walk-to-trainer skill:197 learn:1,2,3,4,5,6,7,8,9",
        "walk-to-trainer skill:197 max:1001",         // a row cannot raise the cap
        "walk-to-trainer skill:197 max:0",
        "walk-to-trainer skill:197 at:1:2,3,4",
        "walk-to-trainer skill:4294967296",
        "walk-to-trainer skill:197 learn:1 max:5 extra:1",
        "",
    };
    for (char const* row : bad)
    {
        TrainerWalkRequest r = ParseTrainerWalkRequest(row);
        if (std::string(r.error) != E::MalformedTrainer || r.skill != 0 || !r.learn.empty())
        {
            std::printf("FAIL '%s' should be malformed and empty, got '%s'\n", row, r.error);
            ++failures;
        }
    }

    TrainerWalkRequest eight =
        ParseTrainerWalkRequest("walk-to-trainer skill:197 learn:1,2,3,4,5,6,7,8");
    CheckText("eight spells are allowed", eight.error, "");
}

void TheVendorGrammar()
{
    VendorWalkRequest plain = ParseVendorWalkRequest("walk-to-vendor item:14341");
    CheckText("the item alone parses", plain.error, "");
    CheckNumber("the item is read", plain.item, 14341);
    CheckNear("the default cap", plain.maxYards, 1000.f);

    VendorWalkRequest capped = ParseVendorWalkRequest("walk-to-vendor max:400 item:14468");
    CheckText("keys in any order", capped.error, "");
    CheckNear("the cap is read", capped.maxYards, 400.f);

    char const* const bad[] = {
        "walk-to-vendor",
        "walk-to-vendor item:0",
        "walk-to-vendor item:thread",
        "walk-to-vendor entry:14341",
        "walk-to-vendor item:1 item:2",
        "walk-to-vendor item:1 max:2000",
        "walk-to-vendor item:1 count:3",
    };
    for (char const* row : bad)
    {
        VendorWalkRequest r = ParseVendorWalkRequest(row);
        if (std::string(r.error) != E::MalformedVendor)
        {
            std::printf("FAIL '%s' should be malformed, got '%s'\n", row, r.error);
            ++failures;
        }
    }
}

void RefusalsNameTheirDestination()
{
    // The mailbox walk keeps every literal it had.
    CheckText("mailbox too far unchanged", WalkRefusalFor(WalkGoal::Mailbox, M::MailboxTooFar),
              M::MailboxTooFar);
    CheckText("mailbox ending unchanged",
              WalkEndReasonFor(WalkGoal::Mailbox, MailWalkState::TimedOut), M::TimedOut);

    CheckText("no trainer", WalkRefusalFor(WalkGoal::Trainer, M::NoMailboxOnMap),
              E::NoTrainerOnMap);
    CheckText("trainer too far", WalkRefusalFor(WalkGoal::Trainer, M::MailboxTooFar),
              E::TrainerTooFar);
    CheckText("trainer across the line", WalkRefusalFor(WalkGoal::Trainer, M::OtherSidesGround),
              E::TrainerOtherSide);
    CheckText("vendor too far", WalkRefusalFor(WalkGoal::Vendor, M::MailboxTooFar),
              E::VendorTooFar);
    CheckText("any walk under way", WalkRefusalFor(WalkGoal::Vendor, M::AlreadyWalking),
              E::AlreadyWalking);
    // A wall about the character is the character's, whatever the destination.
    CheckText("combat passes through", WalkRefusalFor(WalkGoal::Trainer, M::InCombat),
              M::InCombat);
    CheckText("the roster passes through", WalkRefusalFor(WalkGoal::Vendor, M::OnRoster),
              M::OnRoster);

    CheckText("trainer timeout", WalkEndReasonFor(WalkGoal::Trainer, MailWalkState::TimedOut),
              E::TrainerTimedOut);
    CheckText("vendor combat", WalkEndReasonFor(WalkGoal::Vendor, MailWalkState::EnteredCombat),
              E::VendorCombat);
    CheckText("an arrival has no reason", WalkEndReasonFor(WalkGoal::Trainer,
                                                           MailWalkState::Arrived), "");
    CheckText("goal words", std::string(WalkGoalWord(WalkGoal::Trainer)) + "/"
                                + WalkGoalWord(WalkGoal::Vendor),
              "trainer/vendor");
}

void WhichRefusalsMove()
{
    Check("too far moves: a random bot wanders into town",
          ErrandWalkRefusalRetryable(E::TrainerTooFar), true);
    Check("a vendor too far moves", ErrandWalkRefusalRetryable(E::VendorTooFar), true);
    Check("combat on the way moves", ErrandWalkRefusalRetryable(E::TrainerCombat), true);
    Check("no money moves", ErrandWalkRefusalRetryable(E::TaughtNothing), true);
    Check("the character's own walls keep their answer",
          ErrandWalkRefusalRetryable(M::InCombat), true);
    Check("no trainer on the map does not", ErrandWalkRefusalRetryable(E::NoTrainerOnMap), false);
    Check("no vendor on the map does not", ErrandWalkRefusalRetryable(E::NoVendorOnMap), false);
    Check("nothing left to learn does not", ErrandWalkRefusalRetryable(E::NothingToLearn), false);
    Check("malformed does not", ErrandWalkRefusalRetryable(E::MalformedTrainer), false);
    Check("the roster does not", ErrandWalkRefusalRetryable(M::OnRoster), false);
}

void ATrainerVisitIsJudgedOnWhatChanged()
{
    TrainerVisitFacts recipe;
    recipe.asked = 1;
    recipe.learned = 1;
    Check("a recipe learned", JudgeTrainerVisit(recipe) == TrainerVisitOutcome::Learned, true);

    TrainerVisitFacts rank;
    rank.rankOffered = true;
    rank.rankLearned = true;
    Check("a rank learned", JudgeTrainerVisit(rank) == TrainerVisitOutcome::Learned, true);

    TrainerVisitFacts known;
    known.asked = 2;
    known.alreadyKnown = 2;
    Check("everything already known",
          JudgeTrainerVisit(known) == TrainerVisitOutcome::NothingToLearn, true);

    TrainerVisitFacts broke;
    broke.rankOffered = true;
    broke.asked = 1;
    Check("offered and not taught",
          JudgeTrainerVisit(broke) == TrainerVisitOutcome::TaughtNothing, true);

    TrainerVisitFacts halfKnown;
    halfKnown.asked = 2;
    halfKnown.alreadyKnown = 1;
    Check("one new spell asked and not taught",
          JudgeTrainerVisit(halfKnown) == TrainerVisitOutcome::TaughtNothing, true);

    CheckText("verdict words", std::string(TrainerVisitWord(TrainerVisitOutcome::Learned)) + "/"
                                   + TrainerVisitWord(TrainerVisitOutcome::NothingToLearn) + "/"
                                   + TrainerVisitWord(TrainerVisitOutcome::TaughtNothing),
              "learned/nothing_to_learn/taught_nothing");
}

}  // namespace

int main()
{
    EachRowIsKnownByItsFirstWord();
    TheTrainerGrammar();
    TheVendorGrammar();
    RefusalsNameTheirDestination();
    WhichRefusalsMove();
    ATrainerVisitIsJudgedOnWhatChanged();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the trainer and vendor walk decisions hold\n");
    return EXIT_SUCCESS;
}
