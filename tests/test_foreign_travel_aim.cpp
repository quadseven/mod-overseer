/*
 * Whose errand is in `travel_npc`, and therefore which aims the travel book is
 * allowed to overwrite or erase.
 *
 * The live failure this pins. TravelAimBook has two guards - one in Claim()
 * that refuses to overwrite somebody else's outstanding errand, one in
 * Release() that skips the column write for the same reason. Both asked
 * IsMaintenanceErrand, which is only ever true for the four counter keywords.
 * So a positional `at:` aim - the shape the bridge writes to send the family to
 * a surveyed spawn - answered false, the guard collapsed, and the column was
 * cleared out from under a walk already in progress.
 *
 * Measured on the dev realm before the fix:
 *
 *     gathering aim    erased ~21s after it was written
 *     guild-vault aim  erased with ~239s of a 300s lease still to run,
 *                      with the leader 37 yards short of the vault
 *
 * and in the same windows every keyword errand - vendor, repair, auctioneer -
 * completed normally. It was never about how far the walk was. It was which
 * SHAPE the aim had, and only one of the two shapes was in the vocabulary the
 * guards asked.
 *
 * The consequence was that nothing needing a positional aim could ever finish:
 * no gathering trip reached a node, and no deposit reached the guild vault.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CounterRoleForAim;
using OverseerDecisions::CounterRole;
using OverseerDecisions::IsForeignTravelAim;
using OverseerDecisions::IsMaintenanceErrand;

namespace
{

int failures = 0;

void Check(char const* aim, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL '%s': foreign=%s, wanted %s\n",
                aim, got ? "true" : "false", want ? "true" : "false");
    ++failures;
}

// THE DEFECT ITSELF. A positional aim is somebody's outstanding errand, and
// answering false here is what let both guards erase one mid-walk. These are
// the exact two aims measured being destroyed on the dev realm.
void APositionalAimIsSomebodysErrand()
{
    char const* const positional[] = {
        "at:1:-1175.1,-2532.8,123.9",   // the gathering aim, erased after ~21s
        "at:1:-7203.1,-3821.1,8.6",     // the guild-vault aim, erased mid-lease
        "at:0:-8913.2,554.6,93.8",
        "at:",                          // malformed, still not this book's to erase
    };
    for (char const* aim : positional)
        Check(aim, IsForeignTravelAim(aim), true);
}

// THE HALF THAT ALREADY WORKED KEEPS WORKING. These were protected before the
// fix and must stay protected, or the fix has traded one erasure for another.
void EveryCounterKeywordIsStillProtected()
{
    char const* const keywords[] = { "vendor", "banker", "repair", "auctioneer" };
    for (char const* aim : keywords)
    {
        Check(aim, IsForeignTravelAim(aim), true);
        // And the shared vocabulary is genuinely the thing being asked, rather
        // than a second copy of the same four strings that could drift.
        if (!IsMaintenanceErrand(aim))
        {
            std::printf("FAIL '%s': the counter vocabulary disagrees with itself\n", aim);
            ++failures;
        }
    }
}

// AN EMPTY COLUMN IS NOBODY'S ERRAND. Answered first in the implementation for
// the same reason it is asserted first here: every other test below would
// otherwise have to think about the empty string.
void AnEmptyColumnIsNotAnErrand()
{
    Check("", IsForeignTravelAim(""), false);
}

// `trigger:` IS DELIBERATELY EXCLUDED, and that is a decision rather than an
// oversight - so it is pinned, and a future edit that adds it has to come here
// and say why. This module writes its own door and portal aims and Claims them,
// so `_claimed` already answers for them; the only `trigger:` this would newly
// protect is one nobody claims, where clearing a stale door aim is the safer
// reading. ReadSplitErrand groups `at:` and `trigger:` together, so the two
// differ here ON PURPOSE.
void ADoorAimIsNotProtectedByThisPredicate()
{
    char const* const doors[] = { "trigger:78", "trigger:4083" };
    for (char const* aim : doors)
        Check(aim, IsForeignTravelAim(aim), false);
}

// AND NOTHING ELSE IS SWEPT UP. A trainer errand is somebody's profession and
// is answered by LearnSkillPending beside this, not by widening this; a bare
// creature entry is a self-contained errand. Neither is a counter and neither
// is positional, so neither belongs here - this is what stops the predicate
// quietly becoming "anything non-empty".
void NothingElseIsForeign()
{
    char const* const others[] = {
        "trainer", "class trainer", "profession trainer", "guild banker",
        "innkeeper", "flight master", "stable master", "petitioner",
        "tabard designer", "1234", "nothing",
    };
    for (char const* aim : others)
        Check(aim, IsForeignTravelAim(aim), false);
}

// THE PREDICATE IS A SUPERSET OF THE ONE IT REPLACED, BY CONSTRUCTION. Every
// aim the old guard protected must still be protected - that is the property
// that makes this change safe to reason about, and asserting it over the same
// aim list both readers already share is cheaper than promising it in a
// comment.
void ItProtectsEverythingTheOldGuardDid()
{
    char const* const everyAim[] = {
        "", "vendor", "banker", "repair", "trainer", "class trainer",
        "profession trainer", "guild banker", "auctioneer", "petitioner",
        "tabard designer", "innkeeper", "flight master", "stable master",
        "at:0:-8913.2,554.6,93.8", "trigger:78", "1234",
    };
    for (char const* aim : everyAim)
    {
        if (IsMaintenanceErrand(aim) && !IsForeignTravelAim(aim))
        {
            std::printf("FAIL '%s': the old guard protected it and the new one does not\n", aim);
            ++failures;
        }
    }
}

// AND WIDENING THIS DID NOT WIDEN THE OTHER. IsMaintenanceErrand feeds the run
// gate and DungeonRunMaintenanceHold, where a wider answer would change when a
// dungeon run is held rather than when a column is protected. The two questions
// are now genuinely different, and a positional aim is exactly where they part.
void TheRunGateWasNotWidened()
{
    char const* const positional[] = {
        "at:1:-1175.1,-2532.8,123.9", "at:0:-8913.2,554.6,93.8",
    };
    for (char const* aim : positional)
    {
        if (IsMaintenanceErrand(aim))
        {
            std::printf("FAIL '%s': a positional aim must not read as an economy errand\n", aim);
            ++failures;
        }
        if (CounterRoleForAim(aim) != CounterRole::None)
        {
            std::printf("FAIL '%s': a positional aim must not name a counter role\n", aim);
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    APositionalAimIsSomebodysErrand();
    EveryCounterKeywordIsStillProtected();
    AnEmptyColumnIsNotAnErrand();
    ADoorAimIsNotProtectedByThisPredicate();
    NothingElseIsForeign();
    ItProtectsEverythingTheOldGuardDid();
    TheRunGateWasNotWidened();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("an aim nobody claimed is still somebody's errand\n");
    return EXIT_SUCCESS;
}
