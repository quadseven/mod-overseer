/*
 * Learning a recipe off an item, decided without a world.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - THE TWO GRAMMARS SHARE ONE `kind` AND MUST NOT COLLIDE. A `cast` row
 *     begins with a spell id, which is digits; a learn row begins with the
 *     literal word `use`. IsLearnRow is the whole of the dispatch's knowledge
 *     of this verb, so a row it misclassifies is either a cast that never
 *     happens or an item that never gets used. Both directions are pinned,
 *     including the malformed spell ids that must KEEP reaching
 *     ParseCastRequest so its own sentences still answer them.
 *   - `use guid:<n>` and `use entry:<n>` are both accepted and are different
 *     requests. Unlike `give`, neither is preferred: every copy of a Pattern
 *     teaches the same recipe, so an entry has no ambiguity to resolve.
 *   - A trailing word is REFUSED and not ignored, because a silently dropped
 *     option runs a command nobody wrote.
 *   - The verdict is taken on Player::HasSpell and on nothing else. Whether the
 *     item survived is carried for the log and must NOT move the verdict: a
 *     recipe that is not known is a failure whether or not the Pattern was
 *     burned, and treating a consumed item as evidence of learning is exactly
 *     the `delivered is not done` mistake.
 *   - `the character skill is too low to use that item` is ELSEWHERE and not
 *     LATER. This is the refusal a pass that shops for recipes plans around -
 *     the family holds Patterns needing Tailoring 165 while its tailor is at
 *     50 - and a LATER classification would have something re-ask on a timer
 *     forever while the skill sits where it is.
 *   - `the character already knows that recipe` is NEVER, and it is the
 *     refusal that saves the item: the core destroys a recipe item on use
 *     whether or not anything was learned, and asks no such question itself.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::IsLearnRow;
using OverseerDecisions::JudgeLearn;
using OverseerDecisions::LearnOutcome;
using OverseerDecisions::LearnOutcomeWord;
using OverseerDecisions::LearnReadBack;
using OverseerDecisions::LearnRefusalRetry;
using OverseerDecisions::LearnRequest;
using OverseerDecisions::ParseCastRequest;
using OverseerDecisions::ParseLearnRequest;
using OverseerDecisions::TownRetry;

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

void CheckNumber(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, got, want);
    ++failures;
}

void CheckWord(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

LearnReadBack Read(bool readable, bool knowsNow, bool stillCarried)
{
    LearnReadBack read;
    read.learnerReadable = readable;
    read.knowsItNow = knowsNow;
    read.itemStillCarried = stillCarried;
    return read;
}

// ---- telling the two grammars apart ----------------------------------------

void AUseRowIsALearnRow()
{
    Check("use with a guid", IsLearnRow("use guid:1339293"), true);
    Check("use with an entry", IsLearnRow("use entry:27686"), true);
    Check("leading blanks do not hide the verb", IsLearnRow("   use entry:1"), true);
    Check("a tab is a blank too", IsLearnRow("\tuse\tentry:1"), true);
    Check("the word alone is still a learn row, and a malformed one",
          IsLearnRow("use"), true);
}

void ASpellIdIsNotALearnRow()
{
    Check("a bare spell id", IsLearnRow("483"), false);
    Check("a spell id with a target", IsLearnRow("8690 on:self"), false);
    Check("an empty row", IsLearnRow(""), false);
    Check("blanks only", IsLearnRow("   \t "), false);
}

void ONLY_THE_EXACT_WORD_ROUTES_HERE()
{
    // A cast row whose first word merely STARTS with the letters is not this
    // verb. Routing it here would answer it with a learn sentence, sending a
    // reader looking for an item in a row that named a spell.
    Check("used", IsLearnRow("used entry:1"), false);
    Check("useful", IsLearnRow("useful"), false);
    Check("capitalised", IsLearnRow("Use entry:1"), false);
    Check("the word second", IsLearnRow("483 use"), false);
}

void AMalformedSpellIdStillReachesTheCastParser()
{
    // THE HALF THAT WOULD BREAK SILENTLY. If IsLearnRow claimed these, the cast
    // verb's own sentences - which say what a cast row looks like - would never
    // be reached, and a typo in a spell id would be answered by a verb the
    // sender never asked for.
    for (char const* row : {"48x3", "-483", "entry:483", "guid:1", "portal"})
    {
        Check(row, IsLearnRow(row), false);
        Check("and the cast parser refuses it with its own sentence",
              ParseCastRequest(row).ok, false);
    }
}

// ---- the grammar -----------------------------------------------------------

void AGuidNamesOneItem()
{
    LearnRequest const request = ParseLearnRequest("use guid:1339293");
    Check("a guid row is valid", request.ok, true);
    Check("and is addressed by guid", request.byGuid, true);
    CheckNumber("and carries the number", request.key, 1339293);
}

void AnEntryNamesAType()
{
    LearnRequest const request = ParseLearnRequest("use entry:27686");
    Check("an entry row is valid", request.ok, true);
    Check("and is not addressed by guid", request.byGuid, false);
    CheckNumber("and carries the number", request.key, 27686);
}

void SurplusBlanksAreNotWords()
{
    Check("leading, doubled and trailing blanks are tolerated",
          ParseLearnRequest("  use   entry:27686 ").ok, true);
    Check("and tabs are blanks", ParseLearnRequest("\tuse\tguid:7\t").ok, true);
}

void EverythingElseIsMalformed()
{
    Check("an empty row", ParseLearnRequest("").ok, false);
    Check("the verb alone", ParseLearnRequest("use").ok, false);
    Check("an item with no verb", ParseLearnRequest("entry:27686").ok, false);
    Check("a bare number after the verb", ParseLearnRequest("use 27686").ok, false);
    Check("an unknown key", ParseLearnRequest("use item:27686").ok, false);
    Check("a guid of zero", ParseLearnRequest("use guid:0").ok, false);
    Check("an entry of zero", ParseLearnRequest("use entry:0").ok, false);
    Check("letters in the number", ParseLearnRequest("use entry:12ab").ok, false);
    Check("nothing after the colon", ParseLearnRequest("use entry:").ok, false);
    Check("a trailing word", ParseLearnRequest("use entry:27686 now").ok, false);
    Check("both forms at once", ParseLearnRequest("use guid:1 entry:2").ok, false);
    Check("a number that does not fit in 32 bits",
          ParseLearnRequest("use entry:4294967296").ok, false);
    Check("and one that only just does",
          ParseLearnRequest("use entry:4294967295").ok, true);
}

void AMalformedRequestSaysWhichWordWasWrong()
{
    CheckWord("no verb names the verb", ParseLearnRequest("entry:1").error,
              "a learn row must begin with the word use");
    CheckWord("the verb alone names the grammar", ParseLearnRequest("use").error,
              "a learn row takes use guid:<n> or use entry:<n>");
    CheckWord("an unknown key names the grammar too",
              ParseLearnRequest("use item:1").error,
              "a learn row takes use guid:<n> or use entry:<n>");
    CheckWord("a zero names the zero", ParseLearnRequest("use entry:0").error,
              "the item must be guid:<digits> or entry:<digits>, not 0");
    CheckWord("a trailing word says there is exactly one item",
              ParseLearnRequest("use entry:1 now").error,
              "a learn row takes exactly the word use and one item");
}

// ---- the verdict -----------------------------------------------------------

void KnowingItAfterwardsIsTheWholePoint()
{
    Check("the recipe is known at the verdict",
          JudgeLearn(Read(true, true, false)) == LearnOutcome::Learned, true);
}

void NotKnowingItIsAFailureEvenThoughTheItemIsGone()
{
    // THE ROW THIS VERB EXISTS TO BE ABLE TO WRITE. The core destroys the item
    // on use whether or not anything was learned, so a missing Pattern is not
    // evidence of anything except that the use ran.
    Check("item consumed, recipe not known",
          JudgeLearn(Read(true, false, false)) == LearnOutcome::NotLearned, true);
    Check("item still there, recipe not known",
          JudgeLearn(Read(true, false, true)) == LearnOutcome::NotLearned, true);
}

void TheItemDoesNotMoveTheVerdictInEitherDirection()
{
    // Stated as its own case rather than left implicit in the two above,
    // because "the item went, so it must have worked" is the exact inference
    // this verb must never make.
    Check("a surviving item does not downgrade a learn",
          JudgeLearn(Read(true, true, true)) == LearnOutcome::Learned, true);
    Check("and a consumed one does not upgrade a failure",
          JudgeLearn(Read(true, false, false)) == LearnOutcome::NotLearned, true);
}

void AnUnreadableCharacterIsNotAFailure()
{
    // It is not a success either. Saying `not learned` about a character that
    // logged out mid-cast would be a claim nobody measured.
    Check("nobody to ask", JudgeLearn(Read(false, false, false))
                               == LearnOutcome::Unreadable, true);
    Check("and a stale true from before does not survive it",
          JudgeLearn(Read(false, true, false)) == LearnOutcome::Unreadable, true);
}

void TheWordsAreTheOnesTheRowCarries()
{
    CheckWord("learned", LearnOutcomeWord(LearnOutcome::Learned), "learned");
    CheckWord("not learned", LearnOutcomeWord(LearnOutcome::NotLearned), "not learned");
    CheckWord("unreadable", LearnOutcomeWord(LearnOutcome::Unreadable), "unreadable");
}

// ---- whether a refusal is worth asking again -------------------------------

void TheRowAndTheItemAreNeverWorthRetrying()
{
    for (char const* detail :
         {"a learn row must begin with the word use",
          "a learn row takes use guid:<n> or use entry:<n>",
          "the item must be guid:<digits> or entry:<digits>, not 0",
          "a learn row takes exactly the word use and one item",
          "no carried item matches that guid or entry",
          "the item has no template",
          "that item does not teach a recipe when it is used",
          "that item names no spell to teach",
          "the core does not know the spell that item teaches",
          "the core does not know the generic learning spell",
          "the character already knows that recipe",
          "that item is soulbound to somebody else",
          "character has no bot AI to hold it still"})
        Check(detail, LearnRefusalRetry(detail) == TownRetry::Never, true);
}

void ASkillTooLowIsAnswered_Elsewhere_AndNotByWaiting()
{
    // The one a shopping pass keys on. LATER would have it re-asked on a timer
    // while the trade sits where it is; NEVER would be wrong too, because the
    // character really can train the skill up and then use the very same item.
    Check("skill too low",
          LearnRefusalRetry("the character skill is too low to use that item")
              == TownRetry::Elsewhere, true);
    Check("and any other refusal the core makes about using it",
          LearnRefusalRetry("the core refuses this character the use of that item")
              == TownRetry::Elsewhere, true);
}

void TheCharacterStateIsWorthRetryingLater()
{
    for (char const* detail : {"character is moving", "character is in combat",
                               "character is dead", "character is already casting",
                               "a learn row is already running for that character",
                               "character is in flight", "character is in a trade"})
        Check(detail, LearnRefusalRetry(detail) == TownRetry::Later, true);
}

void AnUnknownRefusalIsRetriedLater()
{
    Check("a literal this table has never heard of",
          LearnRefusalRetry("something nobody wrote down") == TownRetry::Later, true);
    Check("and the empty string, which is what a success carries",
          LearnRefusalRetry("") == TownRetry::Later, true);
}

}  // namespace

int main()
{
    AUseRowIsALearnRow();
    ASpellIdIsNotALearnRow();
    ONLY_THE_EXACT_WORD_ROUTES_HERE();
    AMalformedSpellIdStillReachesTheCastParser();

    AGuidNamesOneItem();
    AnEntryNamesAType();
    SurplusBlanksAreNotWords();
    EverythingElseIsMalformed();
    AMalformedRequestSaysWhichWordWasWrong();

    KnowingItAfterwardsIsTheWholePoint();
    NotKnowingItIsAFailureEvenThoughTheItemIsGone();
    TheItemDoesNotMoveTheVerdictInEitherDirection();
    AnUnreadableCharacterIsNotAFailure();
    TheWordsAreTheOnesTheRowCarries();

    TheRowAndTheItemAreNeverWorthRetrying();
    ASkillTooLowIsAnswered_Elsewhere_AndNotByWaiting();
    TheCharacterStateIsWorthRetryingLater();
    AnUnknownRefusalIsRetriedLater();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a recipe is learned off an item, or the row says why not\n");
    return EXIT_SUCCESS;
}
