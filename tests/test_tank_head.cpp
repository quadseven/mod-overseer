/*
 * The head of each family is a protection warrior and the main tank (#626).
 *
 * The roster names a talent tree and TrainRoster spends free points in it, but
 * nothing ever took a point back. The Horde head had 16 points in fury under a
 * row that called him the tank, his playerbot strategy fell back to `arms`, and
 * the dungeon module, which leads a run only with a bot that answers IsTank,
 * elected nobody. This file pins the three decisions that turn such a character
 * into a tank the in-game way:
 *
 *   - when to walk to a class trainer of the character's own class and buy a
 *     talent reset, and when not to (no tree, too low, already in the tree,
 *     short of the price, busy, or resting after a miss);
 *   - when the combat engine gets `tank` and `tank assist`, which is only once
 *     the talents are really in a tank tree;
 *   - that a tank holding a two-hander weighs a one-hander and a shield as a
 *     pair against it, which the sweep never did.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ClassTrainerServes;
using OverseerDecisions::DominantTree;
using OverseerDecisions::GearCompare;
using OverseerDecisions::GearComparison;
using OverseerDecisions::GearConfidence;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearShieldPair;
using OverseerDecisions::GearTankWeighsShieldPair;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWorn;
using OverseerDecisions::JudgeRespec;
using OverseerDecisions::PointsOutsideTree;
using OverseerDecisions::RESPEC_MIN_LEVEL;
using OverseerDecisions::RESPEC_RETRY_SECONDS;
using OverseerDecisions::RespecFacts;
using OverseerDecisions::RespecStep;
using OverseerDecisions::RespecStepWord;
using OverseerDecisions::RespecTook;
using OverseerDecisions::TankStrategyChange;
using OverseerDecisions::TankStrategyFacts;
using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelOwner;

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

void CheckStep(char const* what, RespecStep got, RespecStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, RespecStepWord(got),
                RespecStepWord(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

constexpr uint32_t GOLD = 10000;

// The Horde head as measured: level 25, 16 points in fury, protection asked
// for, holding a few silver against a first reset of one gold.
RespecFacts FuryHeadAskedForProtection()
{
    RespecFacts facts;
    facts.specTab = 2;
    facts.level = 25;
    facts.pointsByTree[1] = 16;
    facts.money = 3791;
    facts.cost = 1 * GOLD;
    facts.available = true;
    facts.columnFree = true;
    return facts;
}

void ACharacterShortOfThePriceDoesNotWalk()
{
    CheckStep("the measured head cannot pay one gold with 37 silver",
              JudgeRespec(FuryHeadAskedForProtection()), RespecStep::CannotAfford);
}

void APaidUpCharacterInTheWrongTreeWalks()
{
    RespecFacts facts = FuryHeadAskedForProtection();
    facts.money = 1 * GOLD;
    CheckStep("exactly the price is enough", JudgeRespec(facts), RespecStep::Walk);

    facts.money = 0;
    facts.costWaived = true;
    CheckStep("a realm that charges nothing needs no purse", JudgeRespec(facts),
              RespecStep::Walk);
}

void ACharacterAlreadyInItsTreeIsLeftAlone()
{
    // The Alliance head as measured: 51 points, all protection.
    RespecFacts facts;
    facts.specTab = 2;
    facts.level = 60;
    facts.pointsByTree[2] = 51;
    facts.money = 410920;
    facts.cost = 1 * GOLD;
    facts.available = true;
    facts.columnFree = true;
    CheckStep("51 points in protection under spec_tab 2", JudgeRespec(facts),
              RespecStep::InTree);

    facts.pointsByTree[2] = 0;
    CheckStep("no points at all is not a reason to pay for a reset", JudgeRespec(facts),
              RespecStep::InTree);
}

void NoTreeAndTooLowAreRefusedFirst()
{
    RespecFacts facts = FuryHeadAskedForProtection();
    facts.money = 5 * GOLD;
    facts.specTab = 255;
    CheckStep("255 names no tree", JudgeRespec(facts), RespecStep::NoTree);

    facts.specTab = 2;
    facts.level = RESPEC_MIN_LEVEL - 1;
    CheckStep("no trainer resets a level 9", JudgeRespec(facts), RespecStep::TooLow);
}

void AWalkWaitsForTheCharacterAndTheColumn()
{
    RespecFacts facts = FuryHeadAskedForProtection();
    facts.money = 5 * GOLD;

    facts.available = false;
    CheckStep("dead, fighting, flying or in an instance", JudgeRespec(facts),
              RespecStep::NotNow);

    facts.available = true;
    facts.columnFree = false;
    CheckStep("a standing errand in the column goes first", JudgeRespec(facts),
              RespecStep::ColumnBusy);

    facts.columnFree = true;
    facts.sinceLastMiss = RESPEC_RETRY_SECONDS - 1;
    CheckStep("a miss a moment ago is not retried yet", JudgeRespec(facts),
              RespecStep::Resting);

    facts.sinceLastMiss = RESPEC_RETRY_SECONDS;
    CheckStep("the retry opens when the clock does", JudgeRespec(facts), RespecStep::Walk);
}

void OnlyATrainerOfTheCharactersOwnClassServes()
{
    uint32_t const WARRIOR = 1;
    uint32_t const MAGE = 8;
    Check("a warrior trainer serves a warrior", ClassTrainerServes(true, WARRIOR, WARRIOR));
    Check("a mage trainer does not serve a warrior", !ClassTrainerServes(true, MAGE, WARRIOR));
    Check("a class trainer with no requirement is nobody's",
          !ClassTrainerServes(true, 0, WARRIOR));
    Check("a trade trainer is not a class trainer",
          !ClassTrainerServes(false, WARRIOR, WARRIOR));
}

void TheTreesAreCountedTheWayThePlayerbotCountsThem()
{
    uint32_t fury[3] = {0, 16, 0};
    Check("16 in fury is 16 outside protection", PointsOutsideTree(fury, 2) == 16);
    Check("and none outside fury", PointsOutsideTree(fury, 1) == 0);
    Check("every point is outside no tree", PointsOutsideTree(fury, 255) == 16);
    Check("fury dominates", DominantTree(fury) == 1);

    uint32_t none[3] = {0, 0, 0};
    Check("no points, no tree", DominantTree(none) == 255);

    uint32_t tie[3] = {5, 5, 0};
    Check("a tie goes to the lower tab, as AiFactory's does", DominantTree(tie) == 0);
}

void AResetIsReadOffTheCharacter()
{
    Check("16 points back and none left outside",
          RespecTook(16, 0, 0, 16));
    Check("nothing moved is a refusal", !RespecTook(16, 16, 0, 0));
    Check("free points without the old ones gone is not a reset",
          !RespecTook(16, 16, 0, 1));
    Check("nothing to reset was nothing reset", !RespecTook(0, 0, 0, 0));
}

void TheTankStrategiesFollowTheTalents()
{
    TankStrategyFacts facts;
    facts.rosterTreeTanks = true;
    facts.talentsInTree = true;
    CheckText("a protection warrior with neither gets both", TankStrategyChange(facts),
              "+tank,+tank assist");

    facts.hasTank = true;
    CheckText("only what is missing", TankStrategyChange(facts), "+tank assist");

    facts.hasTankAssist = true;
    CheckText("nothing when both are there", TankStrategyChange(facts), "");

    facts.hasTank = false;
    facts.hasTankAssist = false;
    facts.talentsInTree = false;
    CheckText("fury talents under a protection row get nothing yet",
              TankStrategyChange(facts), "");

    facts.talentsInTree = true;
    facts.rosterTreeTanks = false;
    CheckText("a roster tree that does not tank gets nothing", TankStrategyChange(facts), "");
}

GearVerdict Worn(float score, GearConfidence confidence = GearConfidence::Exact)
{
    GearVerdict verdict;
    verdict.wearable = true;
    verdict.judged = confidence == GearConfidence::Exact;
    verdict.confidence = confidence;
    verdict.score = score;
    verdict.why = "scored";
    return verdict;
}

void ATankWeighsAOneHanderAndAShieldAgainstItsTwoHander()
{
    Check("the pair is the tank's question when both hands are full",
          GearTankWeighsShieldPair(GearRole::Tank, true));
    Check("not when the main hand is a one-hander",
          !GearTankWeighsShieldPair(GearRole::Tank, false));
    Check("and never for a melee character",
          !GearTankWeighsShieldPair(GearRole::Melee, true));

    // The Horde head's own bags, at the tank's weights: a two-hand mace worth
    // about 44, a one-hand sword worth about 22 and a buckler worth about 280.
    GearVerdict const twoHander = Worn(44.f);
    GearVerdict const sword = Worn(22.f);
    GearVerdict const buckler = Worn(280.f);

    GearVerdict const pair = GearShieldPair(sword, buckler);
    Check("the pair is wearable", pair.wearable);
    Check("the pair is judged", pair.judged);
    Check("the pair scores both halves", pair.score > 301.f && pair.score < 303.f);
    Check("the sword alone never beats the two-hander",
          GearCompare(sword, GearWorn(twoHander)) == GearComparison::NotBetter);
    Check("the pair does",
          GearCompare(pair, GearWorn(twoHander)) == GearComparison::Better);

    GearVerdict unwearable;
    unwearable.wearable = false;
    unwearable.why = "no shield proficiency";
    GearVerdict const refused = GearShieldPair(sword, unwearable);
    Check("a shield the tank cannot hold refuses the pair", !refused.wearable);
    CheckText("and says why", refused.why, "no shield proficiency");

    GearVerdict const floored = GearShieldPair(sword, Worn(280.f, GearConfidence::Floor));
    Check("a floor on either half is a floor on the pair",
          floored.confidence == GearConfidence::Floor && !floored.judged);
}

void TheResetWalkPassesAnEmptyColumnWithALearnPending()
{
    // The Horde head as measured: skill 186 pending and an empty column. The
    // bridge holds its learn trips while a column is taken, so the reset walks
    // first and the learn after it.
    TravelClaimFacts empty(TravelOwner::Respec);
    empty.learnSkill = 186;
    empty.target = OverseerDecisions::RESPEC_AIM;
    Check("an empty column with a learn pending takes the reset walk",
          ReadTravelClaim(empty) == TravelClaim::Write);

    TravelClaimFacts trainer(TravelOwner::Respec);
    trainer.learnSkill = 186;
    trainer.column = "profession trainer";
    trainer.target = OverseerDecisions::RESPEC_AIM;
    Check("a trainer walk already in the column is not overwritten",
          ReadTravelClaim(trainer) == TravelClaim::RefusedProfession);
}

}  // namespace

int main()
{
    ACharacterShortOfThePriceDoesNotWalk();
    APaidUpCharacterInTheWrongTreeWalks();
    ACharacterAlreadyInItsTreeIsLeftAlone();
    NoTreeAndTooLowAreRefusedFirst();
    AWalkWaitsForTheCharacterAndTheColumn();
    OnlyATrainerOfTheCharactersOwnClassServes();
    TheTreesAreCountedTheWayThePlayerbotCountsThem();
    AResetIsReadOffTheCharacter();
    TheTankStrategiesFollowTheTalents();
    ATankWeighsAOneHanderAndAShieldAgainstItsTwoHander();
    TheResetWalkPassesAnEmptyColumnWithALearnPending();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a head in the wrong tree buys a reset, then tanks with a shield\n");
    return EXIT_SUCCESS;
}
