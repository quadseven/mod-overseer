/*
 * The town trip's repair decisions, exercised without a world.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - The command grammar is `all` or `item guid:<n>` and NOTHING ELSE. A bare
 *     guid is refused so that the two forms can never be confused by a typo,
 *     and a guid of ZERO is refused hardest of all: zero is exactly what the
 *     core's own repair path reads as "no item named, repair everything", so a
 *     row that meant one bracer and carried a 0 would quietly become a
 *     repair-all and spend the whole purse. That is the single most expensive
 *     mistake this grammar can make and it is the first case below.
 *   - Among several repairers in reach, the CHEAPEST wins, not the nearest.
 *     Every candidate the executor builds has already passed the core's own
 *     interaction gate, so all of them are within 5.5 yards and the yards buy
 *     nothing; the reputation discount is the only thing that differs and it
 *     is measured in gold. Distance breaks a tie in the discount and the index
 *     breaks a tie in both, so the answer never depends on the order the grid
 *     was walked in.
 *   - A refusal's retry class is decided from the `detail` literal alone,
 *     because that literal is the one string a row carries that both sides of
 *     the queue read. Renaming one in the executor without renaming it here
 *     turns a permanent refusal into an endlessly retried one.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ChooseRepairer;
using OverseerDecisions::ParseRepairRequest;
using OverseerDecisions::RepairerCandidate;
using OverseerDecisions::RepairRefusalRetry;
using OverseerDecisions::RepairRequest;
using OverseerDecisions::RepairVerb;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

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

bool IsAll(std::string const& command)
{
    return ParseRepairRequest(command).verb == RepairVerb::All;
}

bool IsMalformed(std::string const& command)
{
    RepairRequest const request = ParseRepairRequest(command);
    return request.verb == RepairVerb::None && !request.error.empty();
}

// ---- the grammar -----------------------------------------------------------

void AZeroGuidIsNeverARepairAll()
{
    // The expensive one. `item guid:0` reaching the core unchanged would be
    // read as "repair everything", so it is refused here and named as a
    // malformed guid rather than passed on.
    Check("item guid:0 is refused", IsMalformed("item guid:0"), true);
    CheckWord("and says the guid is the problem", ParseRepairRequest("item guid:0").error,
              "malformed repair: item must be guid:<item_instance.guid>, not 0");
    Check("and is not silently turned into a repair-all", IsAll("item guid:0"), false);
}

void EverythingWornIsTheWordAll()
{
    RepairRequest const request = ParseRepairRequest("all");
    Check("all is valid", request.verb == RepairVerb::All, true);
    CheckNumber("and names no item", request.itemGuid, 0);
    Check("and carries no error", request.error.empty(), true);
}

void OneItemIsTheWordItemThenAGuid()
{
    RepairRequest const request = ParseRepairRequest("item guid:494263");
    Check("item guid:<n> is valid", request.verb == RepairVerb::One, true);
    CheckNumber("and names the item", request.itemGuid, 494263);
    Check("and carries no error", request.error.empty(), true);
}

void SurplusBlanksAreNotWords()
{
    Check("leading, doubled and trailing blanks are tolerated",
          ParseRepairRequest("  item   guid:7  ").verb == RepairVerb::One, true);
    Check("and a tab is a blank too", ParseRepairRequest("\tall\t").verb == RepairVerb::All, true);
}

void EverythingElseIsMalformed()
{
    Check("an empty command", IsMalformed(""), true);
    Check("a bare guid, with no verb", IsMalformed("guid:5"), true);
    Check("all with an argument", IsMalformed("all now"), true);
    Check("item with no guid", IsMalformed("item"), true);
    Check("item with a bare number", IsMalformed("item 5"), true);
    Check("item with an entry, which names a type and not an item",
          IsMalformed("item entry:4562"), true);
    Check("a guid with letters in it", IsMalformed("item guid:12ab"), true);
    Check("a guid with nothing after the colon", IsMalformed("item guid:"), true);
    Check("a third word", IsMalformed("item guid:5 now"), true);
    Check("an unknown verb", IsMalformed("repair guid:5"), true);
    Check("ALL, in capitals, is not the verb", IsMalformed("ALL"), true);
    Check("a guid that does not fit in 32 bits", IsMalformed("item guid:4294967296"), true);
    Check("a guid that only just does",
          ParseRepairRequest("item guid:4294967295").verb == RepairVerb::One, true);
}

// ---- which repairer --------------------------------------------------------

void NoRepairerIsNoChoice()
{
    CheckNumber("an empty list chooses nobody", ChooseRepairer({}), -1);
}

void TheCheapestWinsEvenWhenItIsFurther()
{
    // Two repairers about five yards apart, which is what the family's town
    // actually looks like. The one that charges less wins.
    std::vector<RepairerCandidate> const candidates = {
        RepairerCandidate{1.0f, 1.00f},
        RepairerCandidate{5.2f, 0.90f},
    };
    CheckNumber("the cheaper repairer wins", ChooseRepairer(candidates), 1);
}

void DistanceBreaksATieInThePrice()
{
    std::vector<RepairerCandidate> const candidates = {
        RepairerCandidate{5.2f, 0.90f},
        RepairerCandidate{1.0f, 0.90f},
    };
    CheckNumber("same price, the nearer one wins", ChooseRepairer(candidates), 1);
}

void ATieGoesToTheLowerIndex()
{
    std::vector<RepairerCandidate> const candidates = {
        RepairerCandidate{3.0f, 1.0f},
        RepairerCandidate{3.0f, 1.0f},
        RepairerCandidate{3.0f, 1.0f},
    };
    CheckNumber("identical repairers resolve to the first", ChooseRepairer(candidates), 0);
}

void OneRepairerIsTheAnswerWhateverItCharges()
{
    std::vector<RepairerCandidate> const candidates = {RepairerCandidate{5.4f, 1.0f}};
    CheckNumber("the only repairer in reach is chosen", ChooseRepairer(candidates), 0);
}

// ---- whether a refusal is worth asking again -------------------------------

void TheCommandAndTheItemAreNeverWorthRetrying()
{
    for (char const* detail : {"malformed repair: want all, or item guid:<item_instance.guid>",
                               "malformed repair: all takes no arguments",
                               "malformed repair: want item guid:<item_instance.guid>",
                               "malformed repair: item must be guid:<item_instance.guid>, not 0",
                               "malformed repair: unknown verb (want all, or item guid:<n>)",
                               "malformed repair request",
                               "item not carried", "item has no template",
                               "item cannot be damaged", "item is not damaged",
                               "nothing is damaged"})
        Check(detail, RepairRefusalRetry(detail) == TownRetry::Never, true);
}

void ThisSpotIsWorthRetryingElsewhere()
{
    Check("no repairer here", RepairRefusalRetry("repairer not in range") == TownRetry::Elsewhere,
          true);
}

void ThePurseAndTheBodyAreWorthRetryingLater()
{
    // Money arrives from the sale that is queued behind this row, and a dead
    // character gets up. Both are the same spot, later.
    for (char const* detail : {"cannot afford the repair", "character is dead",
                               "character is in flight", "character is not in the world",
                               "character has no session"})
        Check(detail, RepairRefusalRetry(detail) == TownRetry::Later, true);
}

void AnUnknownRefusalIsRetriedLater()
{
    Check("a literal this table has never heard of",
          RepairRefusalRetry("something nobody wrote down") == TownRetry::Later, true);
    Check("and the empty string, which is what a success carries",
          RepairRefusalRetry("") == TownRetry::Later, true);
}

void TheWordsAreTheOnesTheRowCarries()
{
    CheckWord("never", TownRetryWord(TownRetry::Never), "never");
    CheckWord("elsewhere", TownRetryWord(TownRetry::Elsewhere), "elsewhere");
    CheckWord("later", TownRetryWord(TownRetry::Later), "later");
}

}  // namespace

int main()
{
    AZeroGuidIsNeverARepairAll();
    EverythingWornIsTheWordAll();
    OneItemIsTheWordItemThenAGuid();
    SurplusBlanksAreNotWords();
    EverythingElseIsMalformed();

    NoRepairerIsNoChoice();
    TheCheapestWinsEvenWhenItIsFurther();
    DistanceBreaksATieInThePrice();
    ATieGoesToTheLowerIndex();
    OneRepairerIsTheAnswerWhateverItCharges();

    TheCommandAndTheItemAreNeverWorthRetrying();
    ThisSpotIsWorthRetryingElsewhere();
    ThePurseAndTheBodyAreWorthRetryingLater();
    AnUnknownRefusalIsRetriedLater();
    TheWordsAreTheOnesTheRowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the repair decides what it should\n");
    return EXIT_SUCCESS;
}
