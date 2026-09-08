/*
 * One hold, for every verb that needs a character to stand still.
 *
 * mod-overseer#335. Three verbs cast something a movement interrupt cancels -
 * `conjure`, `hearth` and `summon` - and PlayerbotAI::CastSpell refuses a moving
 * bot before the core is ever asked. Only `conjure` ever asked a character to
 * stop, and measured on the dev realm 2026-09-08 even that hold did not survive
 * its own row:
 *
 *   hold_applied: true, hold_took_stay: true, hold_took_follow: false,
 *   stay_at_end: false, follow_at_end: true
 *
 * The hold added `stay` and found no `follow`; by the end of the row `stay` was
 * gone and `follow` was back, and nothing in the conjure executor touched
 * either. The writer is this module's own roster sweep, which re-adds `follow`
 * to any character with a master that does not carry it and exempts exactly one
 * thing - the post-revival hold - because that was the only hold this module had
 * when it was written.
 *
 * What is pinned here is the half of that which is a decision rather than a
 * call: what a hold changes given what it found, what it therefore owes back,
 * and what a character that was asked to stand still and did not is allowed to
 * be called.
 *
 * The other half lives in tests/test_aimed_mover.cpp, where a cast hold now
 * answers `HeldOnPurpose` beside the revival hold, which is what stops the sweep
 * handing a mover back to a character mid-cast.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CastHoldChangedAnything;
using OverseerDecisions::CastHoldFacts;
using OverseerDecisions::CastHoldPlan;
using OverseerDecisions::CastHoldProgress;
using OverseerDecisions::CastHoldStep;
using OverseerDecisions::CastHoldStepWord;
using OverseerDecisions::NextCastHoldStep;
using OverseerDecisions::PlanCastHold;

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

void CheckStep(char const* what, CastHoldProgress const& progress, CastHoldStep want)
{
    CastHoldStep const got = NextCastHoldStep(progress);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, CastHoldStepWord(got),
                CastHoldStepWord(want));
    ++failures;
}

// The three facts spelled out at every call site rather than mutated from a
// shared fixture, so a case that reads as "a follower" cannot inherit a flag set
// by the case above it.
CastHoldFacts Facts(bool hasStay, bool hasFollow, bool hasNewRpg)
{
    CastHoldFacts facts;
    facts.hasStay = hasStay;
    facts.hasFollow = hasFollow;
    facts.hasNewRpg = hasNewRpg;
    return facts;
}

CastHoldProgress Progress(bool moving, std::uint32_t settlePolls, std::uint32_t settleLimit,
                          bool outOfTime)
{
    CastHoldProgress progress;
    progress.moving = moving;
    progress.settlePolls = settlePolls;
    progress.settleLimit = settleLimit;
    progress.outOfTime = outOfTime;
    return progress;
}

// ---------------------------------------------------------------- the plan --

// THE CASE THIS WHOLE ISSUE IS ABOUT. The character refused 29 summons in three
// hours carries `follow` and no `stay`: it is a follower in a permanent party,
// dragged by the party rather than by any drive of its own. A hold that adds
// `stay` and leaves `follow` alone has stopped nothing.
void AFollowerLosesTheThingThatIsActuallyMovingIt()
{
    CastHoldPlan const plan = PlanCastHold(Facts(false, true, false));
    Check("a follower is given stay", plan.addStay, true);
    Check("a follower loses follow", plan.dropFollow, true);
    Check("a follower has no new rpg to lose", plan.dropNewRpg, false);
    Check("a follower's hold owes something back", CastHoldChangedAnything(plan), true);
}

// THE OTHER CHARACTER AT THE STONE, AND IT IS THE LEADER. Read off the live
// engine the same afternoon, the party leader carried `stay` AND `new rpg`.
// `stay` is a DEFAULT action at relevance 1.0 and `new rpg status update` is 11,
// so the leader walks around with the hold's own strategy already on it. Only
// dropping `new rpg` stops it, and adding `stay` would have been the entire
// hold under a plan that only ever added.
void ALeaderAlreadyCarriesStayAndWalksAnyway()
{
    CastHoldPlan const plan = PlanCastHold(Facts(true, false, true));
    Check("stay is not added twice", plan.addStay, false);
    Check("a leader has no follow to lose", plan.dropFollow, false);
    Check("the leader loses its self-drive", plan.dropNewRpg, true);
    Check("the leader's hold owes something back", CastHoldChangedAnything(plan), true);
}

// A hold that finds a character already standing with no mover on it has still
// taken hold - the caller still calls StopMoving - but it owes nothing back, and
// a release that restores what it never touched is the same lie in the other
// direction. This is the reading that stops a `conjure` row stripping a `+stay`
// an operator put on by hand, which is what happened during #329's diagnosis.
void AStandingCharacterIsHeldAndOwesNothing()
{
    CastHoldPlan const plan = PlanCastHold(Facts(true, false, false));
    Check("nothing is added", plan.addStay, false);
    Check("nothing is dropped", plan.dropFollow || plan.dropNewRpg, false);
    Check("the release owes nothing", CastHoldChangedAnything(plan), false);
}

// A character carrying both movers loses both. Not a case anybody expects - the
// anti-scatter rule is that exactly one character travels on its own and it is
// the leader - but it is reachable while a lease is in flight, and a plan that
// silently dropped one of them would leave a hold that half works.
void BothMoversComeOffTogether()
{
    CastHoldPlan const plan = PlanCastHold(Facts(false, true, true));
    Check("stay is added", plan.addStay, true);
    Check("follow comes off", plan.dropFollow, true);
    Check("new rpg comes off", plan.dropNewRpg, true);
}

// Nothing known at all is still a hold: `stay` goes on, and the caller still
// stops the character. The default reading of every input has to be the safe
// one, because these are read off a live engine that can answer late.
void NothingKnownStillHolds()
{
    CastHoldPlan const plan = PlanCastHold(CastHoldFacts());
    Check("stay goes on", plan.addStay, true);
    Check("nothing is taken off", plan.dropFollow || plan.dropNewRpg, false);
    Check("the hold owes stay back", CastHoldChangedAnything(plan), true);
}

// THE RELEASE IS THE PLAN READ BACKWARDS, and that is the property worth pinning
// rather than a second function to keep in step with the first. Every one of the
// eight strategy sets is walked, and for each the plan's three flags are exactly
// the changes that turn the set into a held one.
void TheReleaseUndoesTheHoldAndNothingElse()
{
    for (int bits = 0; bits < 8; ++bits)
    {
        bool const hasStay = (bits & 1) != 0;
        bool const hasFollow = (bits & 2) != 0;
        bool const hasNewRpg = (bits & 4) != 0;
        CastHoldPlan const plan = PlanCastHold(Facts(hasStay, hasFollow, hasNewRpg));

        // What the character carries once the hold has acted.
        bool const heldStay = hasStay || plan.addStay;
        bool const heldFollow = hasFollow && !plan.dropFollow;
        bool const heldNewRpg = hasNewRpg && !plan.dropNewRpg;
        Check("a held character carries stay", heldStay, true);
        Check("a held character carries no follow", heldFollow, false);
        Check("a held character carries no new rpg", heldNewRpg, false);

        // And what it carries once the release has undone exactly the plan.
        bool const backStay = heldStay && !plan.addStay;
        bool const backFollow = heldFollow || plan.dropFollow;
        bool const backNewRpg = heldNewRpg || plan.dropNewRpg;
        Check("stay comes back to where it was", backStay, hasStay);
        Check("follow comes back to where it was", backFollow, hasFollow);
        Check("new rpg comes back to where it was", backNewRpg, hasNewRpg);
    }
}

// ---------------------------------------------------------------- the wait --

// A hold is not instant. StopMoving stops the spline and the movement flags
// isMoving reads clear on a later tick; a live conjure row that started moving
// spent one settle poll before it could cast. A verb that asked and then refused
// in the same breath is the `hearth` and `summon` behaviour this issue is about.
void AStandingCharacterIsReadyEvenWithNoBudgetLeft()
{
    CheckStep("standing and idle", Progress(false, 0, 6, false), CastHoldStep::Ready);
    // BOTH BUDGETS SPENT AND IT IS STILL READY, which is the ordering that
    // matters: a character that came to a halt on the very poll its window ran
    // out has stood still, and a row calling that `never stood still` would be
    // false about the one fact it exists to report.
    CheckStep("standing on the last poll", Progress(false, 99, 6, true), CastHoldStep::Ready);
}

void AMovingCharacterWithBudgetWaits()
{
    CheckStep("first settle poll", Progress(true, 0, 6, false), CastHoldStep::Settle);
    CheckStep("one short of the limit", Progress(true, 5, 6, false), CastHoldStep::Settle);
}

void AMovingCharacterOutOfPollsIsNamed()
{
    CheckStep("at the settle limit", Progress(true, 6, 6, false),
              CastHoldStep::NeverStoodStill);
    CheckStep("past the settle limit", Progress(true, 40, 6, false),
              CastHoldStep::NeverStoodStill);
}

// The window is the backstop under the poll count, and it bounds a different
// failure: a settle limit ends a character being dragged on a spline the hold
// cannot stop, and the window ends a row whose polls stopped arriving. Either
// one alone leaves the other case holding a character still for ever.
void AMovingCharacterOutOfTimeIsNamedEvenWithPollsLeft()
{
    CheckStep("out of time, polls to spare", Progress(true, 0, 6, true),
              CastHoldStep::NeverStoodStill);
}

// A limit of zero is no poll limit, matching the ceiling conventions the rest of
// this header uses, and the window still ends it. A verb that wants to wait as
// long as its window allows must not have to invent a large number to say so.
void ASettleLimitOfZeroIsNoLimit()
{
    CheckStep("no poll limit, in time", Progress(true, 5000, 0, false), CastHoldStep::Settle);
    CheckStep("no poll limit, out of time", Progress(true, 0, 0, true),
              CastHoldStep::NeverStoodStill);
}

void EveryStepHasItsOwnName()
{
    std::string const ready = CastHoldStepWord(CastHoldStep::Ready);
    std::string const settle = CastHoldStepWord(CastHoldStep::Settle);
    std::string const never = CastHoldStepWord(CastHoldStep::NeverStoodStill);
    Check("ready is named", ready == "ready", true);
    Check("settle is named", settle == "settle", true);
    Check("never stood still is named", never == "never stood still", true);
    Check("the three names differ", ready != settle && settle != never && ready != never,
          true);
    // THE WORDS GO STRAIGHT INTO A ROW'S `detail`, which is written into an
    // UPDATE by hand. A quote in one strands the row until the worldserver
    // restarts, which is #318 and is worth one line here rather than a second
    // outage.
    for (char const* word : {"ready", "settle", "never stood still"})
        for (char const* c = word; *c; ++c)
            Check("no step name carries a quote", *c == '\'' || *c == '"', false);
}

}  // namespace

int main()
{
    AFollowerLosesTheThingThatIsActuallyMovingIt();
    ALeaderAlreadyCarriesStayAndWalksAnyway();
    AStandingCharacterIsHeldAndOwesNothing();
    BothMoversComeOffTogether();
    NothingKnownStillHolds();
    TheReleaseUndoesTheHoldAndNothingElse();
    AStandingCharacterIsReadyEvenWithNoBudgetLeft();
    AMovingCharacterWithBudgetWaits();
    AMovingCharacterOutOfPollsIsNamed();
    AMovingCharacterOutOfTimeIsNamedEvenWithPollsLeft();
    ASettleLimitOfZeroIsNoLimit();
    EveryStepHasItsOwnName();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("one hold, and it owes back exactly what it took\n");
    return EXIT_SUCCESS;
}
