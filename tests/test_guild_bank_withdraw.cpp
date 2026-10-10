/*
 * Taking one stack out of a guild bank tab, decided without a world.
 *
 * WHAT IT IS FOR. `bank withdraw-item guid:<n> tab:<t>` is what a member with
 * withdraw rights does at a guild vault: it opens the tab and drags one stack
 * into an empty bag slot. The executor finds the stack, picks the slot, and
 * hands the move to the core's own Guild::SwapItemsWithInventory, which is
 * where the rank's withdraw right and its daily allowance are enforced. That
 * call is void and refuses in silence, so the answer is read back from the
 * world: the tab and the bags, before and after.
 *
 * WHAT IS PINNED, and why each is worth a case:
 *
 *   - THE STACK IS FOUND BY GUID, IN THE TAB THE ROW NAMED. The same entry
 *     can fill fourteen slots of one tab, and the bridge chose one of them.
 *     A guid that is not in that tab finds nothing rather than a neighbour.
 *   - A MOVE IS WITHDRAWN ONLY WHEN BOTH SIDES SAY SO. The guid is out of the
 *     tab, in the bags, and the tab holds exactly the stack's count less of
 *     its entry. Anything short of all three is not reported as a withdraw.
 *   - NOTHING MOVED IS A CLEAN REFUSAL. The rank had no withdraw right on
 *     that tab, or its allowance for the day was spent: the core said nothing
 *     and changed nothing, and the row says so.
 *   - SOMETHING ELSE IS NEVER EITHER. A stack that left the tab and did not
 *     reach the bags is the case a retry must not paper over.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::GuildBankSlotHolding;
using OverseerDecisions::GuildBankSlotItem;
using OverseerDecisions::GuildItemWithdraw;
using OverseerDecisions::GuildItemWithdrawFacts;
using OverseerDecisions::GuildItemWithdrawVerdict;

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

char const* Name(GuildItemWithdraw verdict)
{
    switch (verdict)
    {
        case GuildItemWithdraw::Withdrawn:   return "Withdrawn";
        case GuildItemWithdraw::Refused:     return "Refused";
        case GuildItemWithdraw::Unexplained: return "Unexplained";
    }
    return "?";
}

void CheckVerdict(char const* what, GuildItemWithdrawFacts const& facts, GuildItemWithdraw want)
{
    GuildItemWithdraw const got = GuildItemWithdrawVerdict(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

// Cave's one tab as it stood: Linen Cloth in full stacks of twenty, and other
// things between them.
std::vector<GuildBankSlotItem> CavesTab()
{
    return {
        {0, 9001, 2589, 20},
        {1, 9002, 2589, 20},
        {2, 9100, 8401, 1},
        {5, 9003, 2589, 20},
        {97, 9200, 818, 13},
    };
}

void TheStackIsFoundByItsGuidAndNothingElse()
{
    std::vector<GuildBankSlotItem> const tab = CavesTab();
    Check("the first linen stack is in slot 0", GuildBankSlotHolding(tab, 9001) == 0);
    Check("the third is in slot 5, past a gap", GuildBankSlotHolding(tab, 9003) == 5);
    Check("the last slot of a tab is found", GuildBankSlotHolding(tab, 9200) == 97);
    Check("a guid the tab does not hold is not found, never a neighbour of its entry",
          GuildBankSlotHolding(tab, 9004) == -1);
    Check("an entry is not a guid", GuildBankSlotHolding(tab, 2589) == -1);
    Check("an empty tab holds nothing", GuildBankSlotHolding({}, 9001) == -1);
    Check("guid 0 is never found", GuildBankSlotHolding({{3, 0, 2589, 20}}, 0) == -1);
}

void AWithdrawIsOnlyCalledOneWhenTheTabAndTheBagsAgree()
{
    GuildItemWithdrawFacts taken;
    taken.stackCount = 20;
    taken.tabHeldBefore = 280;
    taken.tabHeldAfter = 260;
    taken.stillInTab = false;
    taken.carriedAfter = true;
    CheckVerdict("out of the tab, into the bags, the tab twenty lighter", taken,
                 GuildItemWithdraw::Withdrawn);

    GuildItemWithdrawFacts refused;
    refused.stackCount = 20;
    refused.tabHeldBefore = 280;
    refused.tabHeldAfter = 280;
    refused.stillInTab = true;
    refused.carriedAfter = false;
    CheckVerdict("nothing moved is the core's silent refusal", refused,
                 GuildItemWithdraw::Refused);

    GuildItemWithdrawFacts lost = taken;
    lost.carriedAfter = false;
    CheckVerdict("out of the tab and not in the bags is never a withdraw", lost,
                 GuildItemWithdraw::Unexplained);

    GuildItemWithdrawFacts doubled = taken;
    doubled.stillInTab = true;
    CheckVerdict("in the bags and still in the tab is never a withdraw", doubled,
                 GuildItemWithdraw::Unexplained);

    GuildItemWithdrawFacts short_ = taken;
    short_.tabHeldAfter = 270;
    CheckVerdict("a tab that lost less than the stack is not a whole withdraw", short_,
                 GuildItemWithdraw::Unexplained);

    GuildItemWithdrawFacts grew = taken;
    grew.tabHeldAfter = 300;
    CheckVerdict("a tab that gained is not a withdraw", grew, GuildItemWithdraw::Unexplained);

    GuildItemWithdrawFacts empty = taken;
    empty.stackCount = 0;
    empty.tabHeldAfter = 280;
    CheckVerdict("a stack of nothing is never a withdraw", empty,
                 GuildItemWithdraw::Unexplained);
}

}  // namespace

int main()
{
    TheStackIsFoundByItsGuidAndNothingElse();
    AWithdrawIsOnlyCalledOneWhenTheTabAndTheBagsAgree();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the guild bank withdraw holds\n");
    return EXIT_SUCCESS;
}
