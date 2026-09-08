/*
 * Who gets `new rpg` handed back when an aimed character is not carrying it.
 *
 * The live failure this pins is a repair that covered everybody except the one
 * character it mattered most for. #295 taught DriveTravel to take the strategy
 * back on every poll of an errand, and gated that on "may this character steer
 * itself", which is an escort or a follower cut off from its leader. A LEADER is
 * neither, so on the dev realm 2026-09-08 the party leader got a refusal written
 * for followers instead:
 *
 *   00:20:24 INFO 'Og' was sent to 'at:1:-705,-2045,66.45' but does not carry
 *                 `new rpg` - nothing walks it anywhere. Followers travel by
 *                 following the leader; aim the leader instead
 *
 * That character's roster row carries `lead` = 1, so the remedy the line offers
 * is the action that had already been taken, and the drive moved nobody.
 *
 * The other half is who took the strategy, and the answer is this module. Three
 * seconds before that line the post-revival hold took it off on purpose and
 * twenty-four seconds after it the same hold gave it back:
 *
 *   00:20:21 INFO 'Og' is held where it revives for 20s (`+stay`, `-new rpg`)
 *   00:20:45 INFO 'Og' is released from its post-revival hold (`new rpg`
 *                 restored)
 *
 * So a held character is neither granted nor refused, and the cases below say
 * so in both directions: the hold outranks every role, and lifting the hold
 * turns the same character straight back into a grant.
 *
 * Nothing here makes a follower steer itself. The anti-scatter rule is the
 * reason `new rpg` is on the leader alone, and the follower cases are written
 * so a reader cannot mistake this for a relaxation of it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::AimedMover;
using OverseerDecisions::AimedMoverFacts;
using OverseerDecisions::AimedMoverGrants;
using OverseerDecisions::AimedMoverName;
using OverseerDecisions::ReadAimedMover;

namespace
{

int failures = 0;

void CheckVerdict(char const* what, AimedMoverFacts const& facts, AimedMover want)
{
    AimedMover const got = ReadAimedMover(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, AimedMoverName(got),
                AimedMoverName(want));
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

// The five facts, spelled out at each call site rather than mutated from a
// shared fixture, so a case that reads as "a leader" cannot silently inherit a
// flag set by the case above it.
AimedMoverFacts Facts(bool carries, bool held, bool leads, bool steers, bool cutOff)
{
    AimedMoverFacts facts;
    facts.carriesStrategy = carries;
    facts.heldAfterRevival = held;
    facts.leadsItsParty = leads;
    facts.steersItself = steers;
    facts.cutOffFromLeader = cutOff;
    return facts;
}

// THE MEASURED CASE, AND THE WHOLE POINT OF THE CHANGE. A leader on an `at:` aim
// with the strategy gone is granted it back, not lectured about following.
void TheLeaderIsGrantedRatherThanRefused()
{
    CheckVerdict("a leader with no strategy",
                 Facts(false, false, true, false, false),
                 AimedMover::GrantToLeader);
    Check("and that verdict grants",
          AimedMoverGrants(ReadAimedMover(Facts(false, false, true, false, false))),
          true);
}

// A CHARACTER IN NO PARTY IS A LEADER FOR THIS PURPOSE, which is upstream's own
// test and not one invented here: AiFactory adds `new rpg` behind
// `!GetGroup() || GetGroup()->GetLeaderGUID() == GetGUID()`. The caller collapses
// both halves into `leadsItsParty`, and a family reduced to one character by
// logouts must not become unwalkable.
void ACharacterInNoPartyIsGrantedToo()
{
    CheckVerdict("alone, so it leads by default",
                 Facts(false, false, true, false, false),
                 AimedMover::GrantToLeader);
}

// THE HOLD OUTRANKS THE ROLE, and this is the case the log line got wrong. The
// module took the strategy off itself and intends to give it back itself, so a
// grant here would override a hold that is still standing: `stay` and `new rpg`
// are not siblings in the engine, so adding one does not lift the other.
void AHeldLeaderIsNeitherGrantedNorRefused()
{
    CheckVerdict("a held leader", Facts(false, true, true, false, false),
                 AimedMover::HeldOnPurpose);
    Check("and a hold does not grant",
          AimedMoverGrants(ReadAimedMover(Facts(false, true, true, false, false))),
          false);
}

// THE HOLD OUTRANKS EVERY ROLE, not just the leader's. An escorted follower
// parked after a revival is in exactly the same position, and the old grant site
// re-armed it every poll while the hold was still in force.
void AHeldSteererAndAHeldFollowerWaitTheSameWay()
{
    CheckVerdict("a held escort", Facts(false, true, false, true, false),
                 AimedMover::HeldOnPurpose);
    CheckVerdict("a held follower in formation",
                 Facts(false, true, false, false, false), AimedMover::HeldOnPurpose);
    CheckVerdict("a held cut-off follower",
                 Facts(false, true, false, false, true), AimedMover::HeldOnPurpose);
}

// THE SECOND HOLD, AND IT IS THE ONE THAT WAS MISSING (#335). A casting verb
// holds a character still for the length of a conjure, a hearth or a summon
// channel, and until this fact existed the grant site could not see one: it
// asked only about the post-revival hold, because that was the only hold this
// module had when this decision was written. Measured on the dev realm
// 2026-09-08, that is how 29 summons in a row were refused `summoner is moving`
// while the sweep handed `follow` back to the summoner on every poll.
void ACastHoldWaitsExactlyLikeARevivalHold()
{
    AimedMoverFacts leader = Facts(false, false, true, false, false);
    leader.heldStill = true;
    CheckVerdict("a leader mid-cast", leader, AimedMover::HeldOnPurpose);
    Check("and a cast hold does not grant", AimedMoverGrants(ReadAimedMover(leader)), false);

    AimedMoverFacts follower = Facts(false, false, false, false, false);
    follower.heldStill = true;
    CheckVerdict("a follower mid-cast", follower, AimedMover::HeldOnPurpose);

    AimedMoverFacts cutOff = Facts(false, false, false, false, true);
    cutOff.heldStill = true;
    CheckVerdict("a cut-off follower mid-cast", cutOff, AimedMover::HeldOnPurpose);
}

// EITHER HOLD ALONE IS ENOUGH, AND BOTH AT ONCE IS ONE ANSWER. A character that
// hearths the moment it is released from a revival hold carries both, and the
// verdict must not depend on which was asked first.
void EitherHoldIsAHoldAndBothIsStillOne()
{
    AimedMoverFacts both = Facts(false, true, true, false, false);
    both.heldStill = true;
    CheckVerdict("held twice", both, AimedMover::HeldOnPurpose);

    AimedMoverFacts neither = Facts(false, false, true, false, false);
    neither.heldStill = false;
    CheckVerdict("held by neither", neither, AimedMover::GrantToLeader);
}

// AND A CHARACTER THAT ALREADY CARRIES THE STRATEGY IS STILL NOT A DECISION,
// even mid-cast. `carriesStrategy` is asked before either hold on purpose: the
// caller is asking whether to HAND ONE BACK, and there is nothing to hand back
// to a character that has it.
void CarryingItStillOutranksACastHold()
{
    AimedMoverFacts walking = Facts(true, false, true, false, false);
    walking.heldStill = true;
    CheckVerdict("walking and held", walking, AimedMover::Walks);
}

// AND LIFTING THE HOLD IS ALL IT TAKES. The pair matters more than either case
// alone: it is what makes "wait" a pause rather than a refusal, so the leader
// that was held becomes a grant on the very next poll with nothing else changed.
void TheHoldIsAPauseAndNotARefusal()
{
    CheckVerdict("held", Facts(false, true, true, false, false),
                 AimedMover::HeldOnPurpose);
    CheckVerdict("the same leader once the hold lifts",
                 Facts(false, false, true, false, false),
                 AimedMover::GrantToLeader);
}

// #295'S CASE, UNCHANGED. An escort and a cut-off follower on an errand it can
// run alone still get the strategy back on every poll.
void TheSteererIsStillGranted()
{
    CheckVerdict("an escorted follower", Facts(false, false, false, true, false),
                 AimedMover::GrantToSteerer);
    CheckVerdict("a cut-off follower steering its own errand",
                 Facts(false, false, false, true, true),
                 AimedMover::GrantToSteerer);
    Check("an escort grants",
          AimedMoverGrants(ReadAimedMover(Facts(false, false, false, true, false))),
          true);
}

// THE ANTI-SCATTER RULE, WHICH MUST NOT MOVE. A follower in formation that is
// not escorted and not steering an errand of its own is still refused. Five
// characters each carrying `new rpg` free-roam, and that is the 937-yard scatter
// taking the strategy off the followers cured.
void AFollowerInFormationIsStillRefused()
{
    CheckVerdict("a plain follower", Facts(false, false, false, false, false),
                 AimedMover::RefuseInFormation);
    Check("and a refusal never grants",
          AimedMoverGrants(ReadAimedMover(Facts(false, false, false, false, false))),
          false);
}

// #289'S CASE, UNCHANGED AND STILL DISTINCT. A follower on another map is
// refused too, but it gets its own verdict, because "aim the leader instead" is
// the one remedy that cannot work for it.
void ACutOffFollowerKeepsItsOwnRefusal()
{
    CheckVerdict("a cut-off follower on a point aim",
                 Facts(false, false, false, false, true), AimedMover::RefuseCutOff);
    Check("and it does not grant",
          AimedMoverGrants(ReadAimedMover(Facts(false, false, false, false, true))),
          false);
}

// A CHARACTER THAT ALREADY CARRIES IT IS NOT A CASE AT ALL, and it answers
// rather than crashing, because the caller reads this every poll of every
// errand and a walking character is the common one.
void AWalkingCharacterIsNotADecision()
{
    CheckVerdict("a leader that already carries it",
                 Facts(true, false, true, false, false), AimedMover::Walks);
    CheckVerdict("a follower that already carries it",
                 Facts(true, false, false, false, false), AimedMover::Walks);
    CheckVerdict("carrying it beats even a hold",
                 Facts(true, true, false, false, false), AimedMover::Walks);
    Check("walking is not a grant",
          AimedMoverGrants(ReadAimedMover(Facts(true, false, true, false, false))),
          false);
}

// THE LEADER BEATS THE STEERER WHEN BOTH ARE TRUE. A leader escorted by its own
// dungeon run is both at once, and the reason it may carry the strategy does not
// end when the escort does, so the stronger claim is the one reported.
void ALeaderThatIsAlsoEscortedReadsAsTheLeader()
{
    CheckVerdict("a leader under an escort", Facts(false, false, true, true, false),
                 AimedMover::GrantToLeader);
    Check("either way it is granted",
          AimedMoverGrants(ReadAimedMover(Facts(false, false, true, true, false))),
          true);
}

// THE DEFAULTS ARE THE SAFE READING. A caller that forgets to fill a field must
// not accidentally hand `new rpg` to a follower, so an all-default fact set is
// the answer that moves nobody.
void NothingKnownMovesNobody()
{
    AimedMoverFacts const nothing;
    CheckVerdict("all defaults", nothing, AimedMover::RefuseInFormation);
    Check("all defaults do not grant", AimedMoverGrants(ReadAimedMover(nothing)),
          false);
}

// Every verdict has a name, and no two share one, so a log line built from this
// cannot report one answer as another.
void EveryVerdictHasItsOwnName()
{
    std::vector<AimedMover> const all = {
        AimedMover::Walks,          AimedMover::HeldOnPurpose,
        AimedMover::GrantToLeader,  AimedMover::GrantToSteerer,
        AimedMover::RefuseInFormation, AimedMover::RefuseCutOff,
    };
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        char const* const name = AimedMoverName(all[i]);
        Check("a verdict has a name", name != nullptr && name[0] != '\0', true);
        for (std::size_t j = i + 1; j < all.size(); ++j)
            Check("two verdicts share a name",
                  std::string(name) == AimedMoverName(all[j]), false);
    }
}

// Exactly two of the six hand the strategy over, asked across the whole set so a
// verdict added later cannot quietly become a grant.
void OnlyTheTwoGrantsGrant()
{
    std::vector<AimedMover> const all = {
        AimedMover::Walks,          AimedMover::HeldOnPurpose,
        AimedMover::GrantToLeader,  AimedMover::GrantToSteerer,
        AimedMover::RefuseInFormation, AimedMover::RefuseCutOff,
    };
    int grants = 0;
    for (AimedMover verdict : all)
        if (AimedMoverGrants(verdict))
            ++grants;
    Check("exactly two verdicts grant", grants == 2, true);
}

}  // namespace

int main()
{
    TheLeaderIsGrantedRatherThanRefused();
    ACharacterInNoPartyIsGrantedToo();
    AHeldLeaderIsNeitherGrantedNorRefused();
    AHeldSteererAndAHeldFollowerWaitTheSameWay();
    ACastHoldWaitsExactlyLikeARevivalHold();
    EitherHoldIsAHoldAndBothIsStillOne();
    CarryingItStillOutranksACastHold();
    TheHoldIsAPauseAndNotARefusal();
    TheSteererIsStillGranted();
    AFollowerInFormationIsStillRefused();
    ACutOffFollowerKeepsItsOwnRefusal();
    AWalkingCharacterIsNotADecision();
    ALeaderThatIsAlsoEscortedReadsAsTheLeader();
    NothingKnownMovesNobody();
    EveryVerdictHasItsOwnName();
    OnlyTheTwoGrantsGrant();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("an aimed leader keeps the strategy that walks it\n");
    return EXIT_SUCCESS;
}
