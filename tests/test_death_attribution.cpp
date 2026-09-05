/*
 * What was moving this character when it died, decided without a world.
 *
 * The live gap this pins is an absence rather than a wrong answer. Over one
 * measured day 55 of 113 roster deaths carried no travel target and 21 carried
 * no quest aim, and 223 under-world deaths since 2026-08-30 all landed between
 * z -642.2 and -500.1 - a kill plane, not ground - with nothing on any row to
 * say what had put a character over it. Two investigations stopped there.
 *
 * So these cases are about what a column is allowed to CLAIM. "Unknown" and
 * "nothing was moving it" and "something was moving it and it was not us" are
 * three different findings, and folding any two of them together is what made
 * the table unable to answer the question it was built for.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using OverseerDecisions::DeathAttribution;
using OverseerDecisions::DeathDriver;
using OverseerDecisions::DeathDriverName;
using OverseerDecisions::MoveGenerator;
using OverseerDecisions::MoveGeneratorName;
using OverseerDecisions::NameTheDriver;
using OverseerDecisions::YardsFallen;

namespace
{

int failures = 0;

void CheckDriver(char const* what, DeathDriver got, DeathDriver want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, DeathDriverName(got),
                DeathDriverName(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    float const d = got - want;
    if ((d < 0.f ? -d : d) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

// The window the adapter passes: one snapshot cadence either side of a
// remedy, so a recovery owns a death that happened while it was still the
// last thing to have moved the character.
long const LIVE_RECOVERY_WINDOW = 15;

DeathAttribution Sampled(MoveGenerator movement, bool travel = false,
                         bool quest = false, long recoverySeconds = -1)
{
    DeathAttribution a;
    a.sampled = true;
    a.movement = movement;
    a.hasTravelTarget = travel;
    a.hasQuestAim = quest;
    a.recoverySeconds = recoverySeconds;
    a.recoveryWindow = LIVE_RECOVERY_WINDOW;
    return a;
}

// NOTHING SAMPLED IS NOT "NOTHING WAS MOVING IT". The whole reason this
// column exists is that a plausible-looking value was worse than a blank.
void AnUnsampledDeathSaysSoAndClaimsNothingElse()
{
    DeathAttribution none;
    CheckDriver("no snapshot at all", NameTheDriver(none), DeathDriver::Unknown);

    // Even with every other field filled in, an unsampled row may not claim
    // a driver: those fields came from the same sample that was never taken.
    DeathAttribution stale = Sampled(MoveGenerator::Follow, true, true);
    stale.sampled = false;
    CheckDriver("fields set but the sample flag is false", NameTheDriver(stale),
                DeathDriver::Unknown);

    DeathAttribution unsampledGenerator = Sampled(MoveGenerator::Unsampled);
    CheckDriver("sampled, but the generator was not readable",
                NameTheDriver(unsampledGenerator), DeathDriver::Unknown);
}

// THE ANSWER THIS MODULE IS IN A POSITION TO BE CERTAIN ABOUT, so it outranks
// every other one. A character that dies while the last thing to have moved it
// was a terrain recovery is a death this module has to own, whatever the core
// had hold of it by the time it landed.
void ARecoveryOwnsADeathItCausedEvenWhenSomethingElseHadHold()
{
    CheckDriver("a recovery four seconds before the death",
                NameTheDriver(Sampled(MoveGenerator::Follow, true, true, 4)),
                DeathDriver::Recovery);
    CheckDriver("and even mid-fall, which is what a failed lift looks like",
                NameTheDriver(Sampled(MoveGenerator::Thrown, false, false, 1)),
                DeathDriver::Recovery);
    CheckDriver("at the edge of the window it still counts",
                NameTheDriver(Sampled(MoveGenerator::Idle, false, false,
                                      LIVE_RECOVERY_WINDOW)),
                DeathDriver::Recovery);
    CheckDriver("one second past it, the generator answers instead",
                NameTheDriver(Sampled(MoveGenerator::Follow, false, false,
                                      LIVE_RECOVERY_WINDOW + 1)),
                DeathDriver::Following);

    // A recovery that never happened is not a recovery that happened at t=0.
    CheckDriver("a character this module has never moved",
                NameTheDriver(Sampled(MoveGenerator::Follow, false, false, -1)),
                DeathDriver::Following);

    // And a caller that wants recoveries never blamed has to write a zero.
    DeathAttribution disabled = Sampled(MoveGenerator::Follow, false, false, 0);
    disabled.recoveryWindow = 0;
    CheckDriver("a zero window disables the attribution outright",
                NameTheDriver(disabled), DeathDriver::Following);
}

// Each generator is a positive statement about what had hold of the character,
// and being thrown beats having wanted to be somewhere.
void EachGeneratorIsItsOwnAnswer()
{
    CheckDriver("EFFECT: a spline it did not choose",
                NameTheDriver(Sampled(MoveGenerator::Thrown, true, true)),
                DeathDriver::Thrown);
    CheckDriver("CHASE is combat", NameTheDriver(Sampled(MoveGenerator::Chase)),
                DeathDriver::Fighting);
    CheckDriver("so is fleeing", NameTheDriver(Sampled(MoveGenerator::Flee)),
                DeathDriver::Fighting);
    CheckDriver("FOLLOW with no aim of its own",
                NameTheDriver(Sampled(MoveGenerator::Follow)),
                DeathDriver::Following);
    CheckDriver("IDLE means nothing was moving it",
                NameTheDriver(Sampled(MoveGenerator::Idle)), DeathDriver::Idle);
    CheckDriver("and an aim it was not executing does not overrule that",
                NameTheDriver(Sampled(MoveGenerator::Idle, true, true)),
                DeathDriver::Idle);
}

// THE VALUE THE WHOLE COLUMN EXISTS FOR. A point move with an aim this module
// set is an errand; the same move with no aim at all is something else moving
// the character, which is the finding #231 needed and could not make.
void APointMoveWithNoAimIsTheFindingNobodyCouldMakeBefore()
{
    CheckDriver("a point move toward a travel target",
                NameTheDriver(Sampled(MoveGenerator::Point, true, false)),
                DeathDriver::Errand);
    CheckDriver("a point move with a quest aim",
                NameTheDriver(Sampled(MoveGenerator::Point, false, true)),
                DeathDriver::Errand);
    CheckDriver("a point move with NO aim of ours at all",
                NameTheDriver(Sampled(MoveGenerator::Point, false, false)),
                DeathDriver::Unattributed);
    CheckDriver("and the same for any other generator we did not ask for",
                NameTheDriver(Sampled(MoveGenerator::Other, false, false)),
                DeathDriver::Unattributed);
    Check("'unattributed' is not spelled the same as 'unknown'",
          std::strcmp(DeathDriverName(DeathDriver::Unattributed),
                      DeathDriverName(DeathDriver::Unknown)) != 0,
          true);
}

// A column somebody groups by has to have stable, distinct values, and the
// generator's own name must survive into the row so a reader can disagree with
// the interpretation above it.
void EveryValueHasItsOwnName()
{
    DeathDriver const drivers[] = {
        DeathDriver::Unknown,  DeathDriver::Recovery, DeathDriver::Errand,
        DeathDriver::Following, DeathDriver::Fighting, DeathDriver::Thrown,
        DeathDriver::Idle,     DeathDriver::Unattributed};
    for (DeathDriver a : drivers)
    {
        Check("a driver name is never empty", DeathDriverName(a)[0] != '\0', true);
        for (DeathDriver b : drivers)
        {
            if (a == b)
                continue;
            Check("two drivers never share a name",
                  std::strcmp(DeathDriverName(a), DeathDriverName(b)) != 0, true);
        }
    }

    // The unsampled generator is the one deliberate blank: an empty string in
    // the row says "not read", which no real generator may be confused with.
    Check("an unsampled generator writes a blank",
          MoveGeneratorName(MoveGenerator::Unsampled)[0] == '\0', true);
    MoveGenerator const generators[] = {
        MoveGenerator::Idle,  MoveGenerator::Follow, MoveGenerator::Point,
        MoveGenerator::Chase, MoveGenerator::Flee,   MoveGenerator::Thrown,
        MoveGenerator::Other};
    for (MoveGenerator g : generators)
        Check("every real generator has a name",
              MoveGeneratorName(g)[0] != '\0', true);
}

// THE KILL PLANE, measured: 223 under-world deaths since 2026-08-30, every one
// between z -642.2 and -500.1. A drop of that size is only visible after the
// fact if the height it fell FROM was being held somewhere, which is what the
// last sample is for.
void TheDropOntoTheKillPlaneIsMeasurableFromTheLastSample()
{
    // 'Grug' died at z -505.3 on map 1. Five seconds earlier the snapshot had
    // him on the ground in Stonetalon at around z 92.
    CheckNear("the drop onto the kill plane", YardsFallen(true, 92.f, -505.3f),
              597.3f);

    // Never sampled is NOT a fall of zero. This is the distinction the report
    // has to keep: "we do not know" and "it did not fall" are different rows.
    CheckNear("unsampled reads negative, not zero", YardsFallen(false, 92.f, -505.3f),
              -1.f);
    Check("and that is distinguishable from a character that did not fall",
          YardsFallen(false, 92.f, -505.3f) < 0.f &&
              YardsFallen(true, 92.f, 92.f) == 0.f,
          true);

    // A character killed where it stood did not fall.
    CheckNear("killed on the spot", YardsFallen(true, 97.4f, 97.4f), 0.f);
    // And one that ended HIGHER than it was last seen did not fall either,
    // rather than falling a negative distance.
    CheckNear("it went up, so it did not fall",
              YardsFallen(true, 60.f, 95.f), 0.f);
}

}  // namespace

int main()
{
    AnUnsampledDeathSaysSoAndClaimsNothingElse();
    ARecoveryOwnsADeathItCausedEvenWhenSomethingElseHadHold();
    EachGeneratorIsItsOwnAnswer();
    APointMoveWithNoAimIsTheFindingNobodyCouldMakeBefore();
    EveryValueHasItsOwnName();
    TheDropOntoTheKillPlaneIsMeasurableFromTheLastSample();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a death says what was moving the character, or says it does not know\n");
    return EXIT_SUCCESS;
}
