/*
 * A travel destination this character can USE but cannot SURVIVE (#267).
 *
 * #234 named two things. #250 fixed the first, that an errand was aimed at a
 * counter the character could not interact with, and said plainly under its own
 * Out of scope that it was not fixing the second: nothing refuses a destination
 * on grounds of danger. #234 was closed anyway, and the second half came back
 * the same evening one level band higher.
 *
 * THE FIXTURES ARE REAL ROWS, measured on the dev realm on 2026-09-06 while the
 * family was dying at them, not numbers invented to make the predicate fire.
 * Distances are from the party's own position at (790.062, -2541.63) on map 1
 * to the vendor spawns in acore_world.creature, and the guard counts are
 * hostile spawns above the leader's level within 60 yards of each vendor, the
 * same radius and the same test GraveyardRefusal already applies to a
 * graveyard.
 *
 * Compiled without AzerothCore, like every other test here, so the rule stays a
 * pure decision.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ChooseTravelTarget;
using OverseerDecisions::TravelTargetCandidate;
using OverseerDecisions::TravelTargetChoice;
using OverseerDecisions::TravelTargetExplanation;
using OverseerDecisions::TravelTargetVerdict;

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

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    '%s'\n  wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// THE INCIDENT, as the vendor sweep saw it.
//
//   14964  Hecht Copperpinch  faction 35    501.9y  usable, and lethal
//   14754  Kelm Hargunth      faction 1515  507.0y  hostile, refused by #250
//    3682  Vrang Wildgore     faction 29    726.6y  hostile, refused by #250
//    3495  Gagsprocket        faction 69   2012.7y  usable and safe
//    3491  Ironzar            faction 69   2056.5y  usable and safe
//
// Hostile spawns above the level 31 leader within 60 yards of 14964, read out
// of acore_world.creature the same afternoon: eight Horde Elite at level 65
// (21 to 53 yards), Kelm Hargunth at 55 (9 yards), Captain Shatterskull at 55
// (15 yards). Ten of them, worst level 65.
std::vector<TravelTargetCandidate> TheMapAsItKilledThem()
{
    return {
        {14964, 501.9f,  true, 10, 65},
        {14754, 507.0f,  false, 0, 0},
        {3682,  726.6f,  false, 0, 0},
        {3495,  2012.7f, true,  0, 0},
        {3491,  2056.5f, true,  0, 0},
    };
}

// The vendor the module actually chose is the one that killed them, and it
// passed every gate the module had. This is the whole issue in one assertion.
void TheNearestUSABLECounterIsTheOneThatKillsThem()
{
    TravelTargetChoice const choice = ChooseTravelTarget(TheMapAsItKilledThem());

    Check("a target is still chosen", choice.verdict == TravelTargetVerdict::Chosen, true);
    CheckInt("the safe town at 2013 yards wins, not the lethal one at 502",
             choice.index, 3);
    CheckInt("the two hostile-faction counters are still refused as before",
             long(choice.refused), 2);
    CheckInt("and the usable-but-guarded one is refused on its own terms",
             long(choice.guarded), 1);
    CheckInt("the nearest guarded one is remembered", choice.nearestGuarded, 0);
    CheckInt("five spawns were considered", long(choice.considered), 5);

    CheckText("and the log names both reasons it walked past three shops",
              TravelTargetExplanation(choice, TheMapAsItKilledThem()),
              "chose entry 3495 at 2013 yards over 2 nearer one(s) this character may "
              "not interact with - the nearest of those is entry 14754 at 507 yards; "
              "and over 1 nearer one(s) standing in hostile ground - the nearest of "
              "those is entry 14964 at 502 yards, guarded by 10 hostile spawn(s) up to "
              "level 65");
}

// Four hundred yards farther is not a tie-break here any more than 1,470 yards
// was in #250. Ten level 65 elites is not a cost to be weighed against a walk,
// it is the reason there is no walk.
void DistanceNeverBuysItsWayPastAGuard()
{
    std::vector<TravelTargetCandidate> const closeAndLethal = {
        {14964, 5.0f,    true, 10, 65},
        {3495,  2012.7f, true, 0,  0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(closeAndLethal);
    CheckInt("the safe one wins from five yards away", choice.index, 1);
    Check("and it is a clean choice, not a compromise",
          choice.verdict == TravelTargetVerdict::Chosen, true);
}

// Part D of #234, applied to the danger half: an errand with nowhere safe to go
// refuses rather than walking, and the refusal names the guard so an operator
// can act on it. This is the state the family was actually in before the safe
// town came into range.
void NowhereSafeIsARefusalThatNamesTheGuard()
{
    std::vector<TravelTargetCandidate> const theCampOnly = {
        {14964, 501.9f, true, 10, 65},
        {14754, 507.0f, false, 0, 0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(theCampOnly);

    Check("no target is chosen",
          choice.verdict == TravelTargetVerdict::EveryOneIsGuarded, true);
    CheckInt("and nothing is named as the answer", choice.index, -1);
    CheckText("the refusal names the guard, its count and its worst level",
              TravelTargetExplanation(choice, theCampOnly),
              "1 of them on this map are ones this character may use and every one "
              "stands in hostile ground - the nearest is entry 14964 at 502 yards, "
              "guarded by 10 hostile spawn(s) up to level 65");
}

// Three different facts, three different verdicts, three different fixes. This
// is the assertion that stops the new one being folded back into the old one:
// "there are none", "none will serve you" and "every one is lethal" send an
// operator to three different places.
void TheThreeRefusalsDoNotReadAlike()
{
    TravelTargetChoice const none = ChooseTravelTarget({});
    Check("an empty map is nothing of that kind",
          none.verdict == TravelTargetVerdict::NothingOfThatKind, true);

    std::vector<TravelTargetCandidate> const allHostile = {
        {14754, 507.0f, false, 0, 0},
        {3682,  726.6f, false, 0, 0},
    };
    Check("a map of hostile counters is still NoneWillDealWithUs",
          ChooseTravelTarget(allHostile).verdict ==
              TravelTargetVerdict::NoneWillDealWithUs,
          true);

    std::vector<TravelTargetCandidate> const allGuarded = {
        {14964, 501.9f, true, 10, 65},
    };
    Check("a map of guarded counters is EveryOneIsGuarded",
          ChooseTravelTarget(allGuarded).verdict ==
              TravelTargetVerdict::EveryOneIsGuarded,
          true);

    CheckText("and the hostile-counter wording is byte for byte what #250 wrote",
              TravelTargetExplanation(ChooseTravelTarget(allHostile), allHostile),
              "2 of them are on this map and this character may interact with none of "
              "them - the nearest is entry 14754 at 507 yards");
}

// When both refusals happened and nothing survived either, the guard is the one
// worth saying. "Nobody will serve you" names no danger and no fix; "there is a
// shop you may use and it will kill you" names both.
void AGuardOutRanksAnUnfriendlyCounterInTheVERDICT()
{
    std::vector<TravelTargetCandidate> const both = {
        {14754, 507.0f, false, 0, 0},
        {14964, 501.9f, true, 10, 65},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(both);
    Check("the guard names the verdict",
          choice.verdict == TravelTargetVerdict::EveryOneIsGuarded, true);
    CheckInt("both refusals are still counted separately", long(choice.refused), 1);
    CheckInt("and so is the guarded one", long(choice.guarded), 1);
}

// The order of the two gates is part of the meaning: interaction is a property
// of the counter and costs nothing to ask, the guard test costs a sweep over
// every spawn in the world. A counter that fails both is REFUSED, never
// GUARDED, so the expensive question is never asked about a spawn already out.
void AnUnfriendlyCounterIsRefusedBeforeItsGuardsAreEverCounted()
{
    std::vector<TravelTargetCandidate> const unfriendlyAndGuarded = {
        {14754, 507.0f, false, 10, 65},
        {3495,  2012.7f, true, 0, 0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(unfriendlyAndGuarded);
    CheckInt("it counts as refused", long(choice.refused), 1);
    CheckInt("and not as guarded", long(choice.guarded), 0);
    CheckInt("the nearest guarded is therefore nothing", choice.nearestGuarded, -1);
    CheckInt("the safe one is still chosen", choice.index, 1);
}

// The reason the caller is allowed to measure lazily. Everything farther than
// the chosen candidate could not have won, so leaving it unmeasured (which
// reads as unguarded) cannot change the answer, and measuring it would buy a
// full spawn sweep for nothing.
void AnUnmeasuredFartherCandidateCannotChangeTheAnswer()
{
    std::vector<TravelTargetCandidate> const measuredThenNot = {
        {14964, 501.9f,  true, 10, 65},  // measured: guarded
        {3495,  2012.7f, true, 0,  0},   // measured: clean, and it wins
        {3491,  2056.5f, true, 0,  0},   // never measured, farther, reads clean
        {6791,  2155.9f, true, 0,  0},   // never measured, farther, reads clean
    };
    TravelTargetChoice const choice = ChooseTravelTarget(measuredThenNot);
    CheckInt("the first clean one still wins", choice.index, 1);
    CheckInt("and the unmeasured ones are not counted as guarded",
             long(choice.guarded), 1);
}

// High level neighbours are not the test. Hostility is. The safe town has
// creatures up to level 80 standing around its vendors and every one of them is
// a neutral goblin faction, so the guard count there is zero and the errand is
// ordinary. A rule that counted LEVEL rather than HOSTILE LEVEL would refuse
// the only town this family can shop in, which is a worse bug than the one
// being fixed.
void NeutralGiantsAreNotGuards()
{
    std::vector<TravelTargetCandidate> const theSafeTown = {
        {3495, 2012.7f, true, 0, 0},
        {3491, 2056.5f, true, 0, 0},
        {3492, 2106.6f, true, 0, 0},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(theSafeTown);
    CheckInt("the nearest is chosen", choice.index, 0);
    CheckInt("nothing was refused", long(choice.refused), 0);
    CheckInt("nothing was guarded", long(choice.guarded), 0);
    CheckText("and an ordinary errand says nothing extra",
              TravelTargetExplanation(choice, theSafeTown), "");
}

// A guarded spawn FARTHER than the chosen one cost nobody a walk, so it is not
// mentioned, exactly as an unusable one farther away already was not. A log
// line that appears is always a line about this errand.
void AGuardBeyondTheChosenOneIsNotANearMiss()
{
    std::vector<TravelTargetCandidate> const behindUs = {
        {3495,  40.0f,  true, 0,  0},
        {14964, 900.0f, true, 10, 65},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(behindUs);
    CheckInt("the near clean one is chosen", choice.index, 0);
    CheckInt("the farther guarded one is still counted", long(choice.guarded), 1);
    CheckText("but nothing is said about it",
              TravelTargetExplanation(choice, behindUs), "");
}

// Two guarded vendors the same distance away is the ordinary town square seen
// from outside, so the remembered one must not depend on the order a spawn
// sweep produced.
void AGuardedTieGoesToTheLowerIndex()
{
    std::vector<TravelTargetCandidate> const together = {
        {14964, 502.0f, true, 10, 65},
        {14754, 502.0f, true, 10, 65},
    };
    CheckInt("the first of two equals is the one remembered",
             ChooseTravelTarget(together).nearestGuarded, 0);
}

}  // namespace

int main()
{
    TheNearestUSABLECounterIsTheOneThatKillsThem();
    DistanceNeverBuysItsWayPastAGuard();
    NowhereSafeIsARefusalThatNamesTheGuard();
    TheThreeRefusalsDoNotReadAlike();
    AGuardOutRanksAnUnfriendlyCounterInTheVERDICT();
    AnUnfriendlyCounterIsRefusedBeforeItsGuardsAreEverCounted();
    AnUnmeasuredFartherCandidateCannotChangeTheAnswer();
    NeutralGiantsAreNotGuards();
    AGuardBeyondTheChosenOneIsNotANearMiss();
    AGuardedTieGoesToTheLowerIndex();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a travel errand refuses a destination it cannot survive\n");
    return 0;
}
