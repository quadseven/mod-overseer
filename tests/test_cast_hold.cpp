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
 * call: what a hold changes given what it found, and what it therefore owes
 * back. The waiting half is not a decision this header owns - `conjure` waits
 * through ConjureNextStep and the other two verbs refuse rather than wait.
 *
 * The other half lives in tests/test_aimed_mover.cpp, where a cast hold now
 * answers `HeldOnPurpose` beside the revival hold, which is what stops the sweep
 * handing a mover back to a character mid-cast.
 *
 * AND THE THIRD HALF, WHICH IS #358 AND ARRIVED A YEAR OF ISSUES LATER. Both of
 * the decisions above are about what a character may be GIVEN, and neither of
 * them is about a walk it is already on. Unit::StopMoving does not touch the
 * MotionMaster, so a follower whose leader is walking is dragged after it at run
 * speed while carrying a hold that reports every one of its parts as applied -
 * measured at forty yards in nine seconds. The adapter answers that by taking
 * the active motion slot; what is pinned HERE is the one decision that answer
 * needs, which is when the sweep that keeps the hold may re-take that slot and,
 * far more importantly, when it may not.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CastHoldFacts;
using OverseerDecisions::CastHoldPlan;
using OverseerDecisions::HeldStillFacts;
using OverseerDecisions::PlanCastHold;
using OverseerDecisions::RetakeTheHold;

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
    Check("a follower's hold owes something back",
          plan.addStay || plan.dropFollow || plan.dropNewRpg, true);
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
    Check("the leader's hold owes something back",
          plan.addStay || plan.dropFollow || plan.dropNewRpg, true);
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
    Check("the release owes nothing",
          plan.addStay || plan.dropFollow || plan.dropNewRpg, false);
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
    Check("the hold owes stay back",
          plan.addStay || plan.dropFollow || plan.dropNewRpg, true);
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

// ------------------------------------------- keeping the hold, tick by tick --

// The slack the adapter actually passes, spelled here so a case that means "a
// yard and a half" cannot be read as "some distance".
constexpr float SLACK = 1.0f;

// A character standing where its hold meant, doing nothing. Every field is
// spelled at every call site below rather than mutated from a shared fixture,
// for the same reason Facts() above is.
HeldStillFacts Held(bool present, bool pastDeadline, bool walkingOnPurpose, bool inCombat,
                    float driftYards)
{
    HeldStillFacts facts;
    facts.present = present;
    facts.pastDeadline = pastDeadline;
    facts.walkingOnPurpose = walkingOnPurpose;
    facts.inCombat = inCombat;
    facts.driftYards = driftYards;
    return facts;
}

// THE CASE THE WHOLE ISSUE IS ABOUT. A held follower behind a leader that is
// still walking, dragged four yards since the last tick looked. Nothing else is
// true of it: it is present, inside its ceiling, nobody asked it to walk, and it
// is not fighting.
void ADraggedFollowerHasItsHoldRetaken()
{
    Check("a dragged follower is re-taken", RetakeTheHold(Held(true, false, false, false, 4.f), SLACK),
          true);
}

// AND THE PIN'S OWN FOOTPRINT IS NOT A DRAG. Taking a hold ends in a call that
// stops a spline, and stopping one recomputes the unit's position off it, so a
// character that has just been pinned can read a fraction of a yard from its
// anchor. A sweep that took that for drift would re-take the slot on every
// tick of every hold, which is a worse defect than the one being fixed and one
// that would only ever be visible as load.
void TheHoldsOwnFootprintIsNotDrift()
{
    Check("a fifth of a yard is not a drag",
          RetakeTheHold(Held(true, false, false, false, 0.2f), SLACK), false);
    Check("standing exactly still is not a drag",
          RetakeTheHold(Held(true, false, false, false, 0.f), SLACK), false);
}

// THE SUMMON'S APPROACH, AND IT IS THE REASON THIS DECISION HAS A `walkingOnPurpose`
// AT ALL (#357). A clicker being walked the last fifteen yards onto a meeting
// stone is a held character a long way from its anchor, and it is the one such
// character that must be left completely alone. Re-taking the slot here would
// cancel the walk on the tick after it started and move the summon's failure
// rather than fixing it.
void AWalkAVerbAskedForIsLeftAlone()
{
    Check("a deliberate walk is not interrupted",
          RetakeTheHold(Held(true, false, true, false, 15.f), SLACK), false);
    // ...and the same character once the walk's window is over is an ordinary
    // held character again. The suppression is a window and not a mode.
    Check("and the same character is held again afterwards",
          RetakeTheHold(Held(true, false, false, false, 15.f), SLACK), true);
}

// A FIGHT IS LEFT ALONE FOR THE REASON `flee` IS. A held character still fights
// and still flees, deliberately, and a pin that took the slot the combat engine
// is steering with would be this module holding a character still while
// something kills it - which is the one failure mode a hold must never have.
void AFightIsLeftAlone()
{
    Check("a fighting character is not pinned",
          RetakeTheHold(Held(true, false, false, true, 30.f), SLACK), false);
}

// AND THE TWO THAT ARE ABOUT THE HOLD RATHER THAN THE CHARACTER. A name that
// resolves to nothing has logged out or is mid-teleport, and a hold past its
// ceiling is one the release already owes: re-taking either would be this module
// acting on a character it no longer has, or holding one for longer than it said.
void AHoldThatIsOverOrAbsentIsNotRetaken()
{
    Check("an absent character is not pinned",
          RetakeTheHold(Held(false, false, false, false, 40.f), SLACK), false);
    Check("a hold past its ceiling is not pinned",
          RetakeTheHold(Held(true, true, false, false, 40.f), SLACK), false);
}

// EVERY REFUSAL BEATS THE DISTANCE, WHICH IS THE PROPERTY RATHER THAN THE FOUR
// CASES ABOVE. A character can be a hundred yards off its anchor and still be
// one this sweep must not touch, and a reader coming to this decision looking
// for "when does it act" should find that the distance is the last word and
// never the first.
void NoDistanceOverridesARefusal()
{
    for (int bits = 0; bits < 16; ++bits)
    {
        bool const present = (bits & 1) != 0;
        bool const pastDeadline = (bits & 2) != 0;
        bool const walking = (bits & 4) != 0;
        bool const inCombat = (bits & 8) != 0;
        bool const refused = !present || pastDeadline || walking || inCombat;
        Check("a hundred yards does not override a refusal",
              RetakeTheHold(Held(present, pastDeadline, walking, inCombat, 100.f), SLACK),
              !refused);
        // ...and with nothing refusing, the distance is the whole answer.
        Check("and inside the slack nothing is re-taken",
              RetakeTheHold(Held(present, pastDeadline, walking, inCombat, 0.5f), SLACK), false);
    }
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
    ADraggedFollowerHasItsHoldRetaken();
    TheHoldsOwnFootprintIsNotDrift();
    AWalkAVerbAskedForIsLeftAlone();
    AFightIsLeftAlone();
    AHoldThatIsOverOrAbsentIsNotRetaken();
    NoDistanceOverridesARefusal();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("one hold, it owes back exactly what it took, and it is kept\n");
    return EXIT_SUCCESS;
}
