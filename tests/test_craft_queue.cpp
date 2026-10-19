/*
 * The crafting queue: bags for every member first, then want-list items or
 * consumables, then skill-ups, with trainer visits when a crafter is at its
 * cap or does not yet know the recipe it would cast.
 *
 * This is the decision half of the crafting drive. The adapter's half
 * (DriveCraft, the observed trade, the item-creator read-back) needs a full
 * AzerothCore build and is not exercised here; this file pins the queue's
 * ordering and its trainer gate, which is everything the local check can
 * verify.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::CraftQueueStep;
using OverseerDecisions::CraftQueueStepKind;
using OverseerDecisions::CraftingMember;
using OverseerDecisions::FamilyWants;
using OverseerDecisions::NextCraftStep;

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

CraftingMember Member(uint32_t skill, uint32_t value, uint32_t cap, bool knows,
                    uint32_t nextRecipe, uint8_t bags, bool tailor)
{
    CraftingMember m;
    m.skill = skill;
    m.value = value;
    m.cap = cap;
    m.knowsRecipe = knows;
    m.nextRecipe = nextRecipe;
    m.bags = bags;
    m.isTailor = tailor;
    return m;
}

void ExpectStep(char const* what, CraftQueueStep const& got, CraftQueueStepKind kind,
                uint32_t skill, uint32_t spellId, std::size_t member)
{
    if (got.kind == kind && got.skill == skill && got.spellId == spellId &&
        got.member == member)
        return;
    ++failures;
    std::printf("FAIL %s: expected kind %u skill %u spell %u member %u, "
                "got kind %u skill %u spell %u member %u\n",
                what, static_cast<unsigned>(kind), skill, spellId,
                static_cast<unsigned>(member), static_cast<unsigned>(got.kind),
                got.skill, got.spellId, static_cast<unsigned>(got.member));
}

constexpr uint32_t TAILORING = 197;
constexpr uint32_t BLACKSMITHING = 164;
constexpr uint32_t LEATHERWORKING = 165;
constexpr uint32_t ALCHEMY = 171;

// ---------------------------------------------------------------- bags --

void BagsComeFirst()
{
    // Member 0 needs a bag (3 of 4) and is not a tailor; member 1 is the
    // tailor with four bags already. The tailor makes the bag: the crafter
    // is the tailor, and a want-list item must not jump the queue.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 200, 3, false), // needs a bag
        Member(TAILORING, 10, 75, true, 201, 4, true),      // the tailor
    };
    FamilyWants wants;
    wants.wantList.push_back(300);
    ExpectStep("bags before want list",
               NextCraftStep(family, wants), CraftQueueStepKind::Bag, 0, 0, 1);
}

void TheTailorMakesItsOwnBag()
{
    // The only member below four bags IS the tailor: it crafts its own bag.
    std::vector<CraftingMember> const family = {
        Member(TAILORING, 10, 75, true, 201, 3, true),
    };
    FamilyWants wants;
    ExpectStep("the tailor makes its own bag",
               NextCraftStep(family, wants), CraftQueueStepKind::Bag, 0, 0, 0);
}

void NoTailorMeansNoBag()
{
    // Someone needs a bag but nobody is a tailor: no bag is made, and with no
    // recipe chosen there is nothing else to do either.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 0, 3, false),
    };
    FamilyWants wants;
    ExpectStep("no tailor, no bag",
               NextCraftStep(family, wants), CraftQueueStepKind::Nothing, 0, 0, 0);
}

void EveryoneFullBagsMovesOn()
{
    // Everyone holds four bags, so bags step aside for the want list. The
    // blacksmith knows the wanted recipe (300), so it crafts it.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 300, 4, false),
        Member(TAILORING, 10, 75, true, 201, 4, true),
    };
    FamilyWants wants;
    wants.wantList.push_back(300);
    ExpectStep("full bags, want list next",
               NextCraftStep(family, wants), CraftQueueStepKind::Item, BLACKSMITHING, 300, 0);
}

// ----------------------------------------------------------- want list --

void WantListBeforeSkillUps()
{
    // Everyone holds four bags, the want list is non-empty, and the blacksmith
    // knows the wanted recipe (200). A skill-up is also available for the
    // leatherworker, but the want list wins.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 200, 4, false),
        Member(LEATHERWORKING, 5, 75, true, 250, 4, false),
    };
    FamilyWants wants;
    wants.wantList.push_back(200);
    ExpectStep("want list before skill up",
               NextCraftStep(family, wants), CraftQueueStepKind::Item, BLACKSMITHING, 200, 0);
}

void UnknownWantRecipeSkipsToTrainer()
{
    // The want list names a recipe nobody knows: no item is crafted, and the
    // crafter that does not know its recipe is sent to a trainer instead.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, false, 200, 4, false),
    };
    FamilyWants wants;
    wants.wantList.push_back(300);
    ExpectStep("unknown recipe goes to trainer",
               NextCraftStep(family, wants), CraftQueueStepKind::TrainerVisit, BLACKSMITHING, 0,
               0);
}

void EmptyWantListMovesOn()
{
    // No want list: the queue proceeds to skill-ups.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 200, 4, false),
    };
    FamilyWants wants;
    ExpectStep("empty want list, skill up next",
               NextCraftStep(family, wants), CraftQueueStepKind::SkillUp, BLACKSMITHING, 200, 0);
}

// ------------------------------------------------------------- skill-up --

void EveryCraftRises()
{
    // Every crafting profession below its cap that knows its recipe is a
    // skill-up candidate, taken in family order.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 200, 4, false),
        Member(LEATHERWORKING, 5, 75, true, 250, 4, false),
    };
    FamilyWants wants;
    ExpectStep("blacksmithing rises first",
               NextCraftStep(family, wants), CraftQueueStepKind::SkillUp, BLACKSMITHING, 200, 0);
}

void AtCapIsNotASkillUp()
{
    // A crafter at its cap is not asked to skill up.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 75, 75, true, 200, 4, false),
    };
    FamilyWants wants;
    ExpectStep("at cap, not a skill up",
               NextCraftStep(family, wants), CraftQueueStepKind::TrainerVisit, BLACKSMITHING, 0,
               0);
}

void UnknownRecipeIsNotASkillUp()
{
    // A crafter that does not know its recipe is not asked to skill up.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, false, 200, 4, false),
    };
    FamilyWants wants;
    ExpectStep("unknown recipe, not a skill up",
               NextCraftStep(family, wants), CraftQueueStepKind::TrainerVisit, BLACKSMITHING, 0,
               0);
}

// ----------------------------------------------------------- trainer --

void TrainerVisitWhenAtCap()
{
    std::vector<CraftingMember> const family = {
        Member(ALCHEMY, 75, 75, true, 0, 4, false),
    };
    FamilyWants wants;
    ExpectStep("at cap produces a trainer errand",
               NextCraftStep(family, wants), CraftQueueStepKind::TrainerVisit, ALCHEMY, 0, 0);
}

void TrainerVisitWhenRecipeUnknown()
{
    std::vector<CraftingMember> const family = {
        Member(ALCHEMY, 10, 75, false, 0, 4, false),
    };
    FamilyWants wants;
    ExpectStep("unknown recipe produces a trainer errand",
               NextCraftStep(family, wants), CraftQueueStepKind::TrainerVisit, ALCHEMY, 0, 0);
}

void NoTrainerWhenBelowCapAndKnown()
{
    std::vector<CraftingMember> const family = {
        Member(ALCHEMY, 10, 75, true, 400, 4, false),
    };
    FamilyWants wants;
    ExpectStep("below cap and known, no trainer",
               NextCraftStep(family, wants), CraftQueueStepKind::SkillUp, ALCHEMY, 400, 0);
}

// ----------------------------------------------------------------- nothing --

void NothingWhenQueueDrained()
{
    // Four bags each, no wants, every craft below cap and known but with no
    // recipe chosen: nothing to do.
    std::vector<CraftingMember> const family = {
        Member(BLACKSMITHING, 10, 75, true, 0, 4, false),
        Member(TAILORING, 10, 75, true, 0, 4, true),
    };
    FamilyWants wants;
    ExpectStep("queue drained",
               NextCraftStep(family, wants), CraftQueueStepKind::Nothing, 0, 0, 0);
}

void NothingForAnEmptyFamily()
{
    std::vector<CraftingMember> const family;
    FamilyWants wants;
    ExpectStep("empty family",
               NextCraftStep(family, wants), CraftQueueStepKind::Nothing, 0, 0, 0);
}

}  // namespace

int main()
{
    BagsComeFirst();
    TheTailorMakesItsOwnBag();
    NoTailorMeansNoBag();
    EveryoneFullBagsMovesOn();
    WantListBeforeSkillUps();
    UnknownWantRecipeSkipsToTrainer();
    EmptyWantListMovesOn();
    EveryCraftRises();
    AtCapIsNotASkillUp();
    UnknownRecipeIsNotASkillUp();
    TrainerVisitWhenAtCap();
    TrainerVisitWhenRecipeUnknown();
    NoTrainerWhenBelowCapAndKnown();
    NothingWhenQueueDrained();
    NothingForAnEmptyFamily();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the crafting queue holds\n");
    return EXIT_SUCCESS;
}
