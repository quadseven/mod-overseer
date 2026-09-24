/*
 * Every walk a dungeon run makes passes the profession fence the same way, and
 * a character the staging hold is holding keeps its diverters down (#598).
 *
 * THE CLAIM. #589 let the staging, corridor and berth claims past a pending
 * `learn_skill` over an empty column. The BARRIER escort and the walk back in
 * called TravelAimBook::Claim with no owner at all and were fenced as ordinary
 * claims. Measured on the dev realm 2026-09-23 with skill 186 pending on the
 * leader at Ragefire: the staging errand was released on arrival, BARRIER's
 * re-claim of the identical point was refused, and the leader was 408 yards
 * away and 64 above it 90 seconds later. Every claim now names its owner.
 *
 * THE FOCUS. With no errand under him, SweepTravelFocus handed `grind` back to
 * the leader the barrier was waiting on. A staging hold now keeps the focus the
 * way a regroup hold already did (#404).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelFocusFacts;
using OverseerDecisions::TravelFocusOutlivesItsErrand;
using OverseerDecisions::TravelOwner;
using OverseerDecisions::TravelOwnerPassesAnEmptyLearnColumn;

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

// The measured column: nothing in it, skill 186 pending.
TravelClaim ClaimOverAPendingLearn(TravelOwner owner, char const* column)
{
    TravelClaimFacts facts(owner);
    facts.learnSkill = 186;
    facts.column = column;
    return ReadTravelClaim(facts);
}

void EveryRunWalkPassesAnEmptyLearnColumn()
{
    // The BARRIER escort, the crossing, the town trip, the reset exit and the
    // leader's staging legs are all the run's own.
    Check("a run's aim passes", TravelOwnerPassesAnEmptyLearnColumn(TravelOwner::Run),
          true);
    Check("the BARRIER re-claim of the staging point is written",
          ClaimOverAPendingLearn(TravelOwner::Run, "") == TravelClaim::Write, true);
    // The walk back in is the run's too: it walks a member into the instance
    // the run is clearing.
    Check("the walk back in passes",
          TravelOwnerPassesAnEmptyLearnColumn(TravelOwner::WalkBackIn), true);
    Check("the walk back in over an empty column is written",
          ClaimOverAPendingLearn(TravelOwner::WalkBackIn, "") == TravelClaim::Write,
          true);
    Check("a catch-up still passes",
          TravelOwnerPassesAnEmptyLearnColumn(TravelOwner::CatchUp), true);
}

void TheFenceStillStandsWhereItProtectsAWalk()
{
    Check("the home errand keeps the #435 fence",
          TravelOwnerPassesAnEmptyLearnColumn(TravelOwner::HomeErrand), false);
    Check("a home errand over a pending learn is refused",
          ClaimOverAPendingLearn(TravelOwner::HomeErrand, "") ==
              TravelClaim::RefusedProfession,
          true);
    // A live run outranks a trainer walk (#656): the trip waits for the run.
    Check("a run's aim over a trainer walk outranks it",
          ClaimOverAPendingLearn(TravelOwner::Run, "trainer") == TravelClaim::Outrank,
          true);
    Check("the walk back in over a trainer walk outranks it",
          ClaimOverAPendingLearn(TravelOwner::WalkBackIn, "trainer") ==
              TravelClaim::Outrank,
          true);
}

TravelFocusFacts Focus(bool aimed, bool regroup, bool staging)
{
    TravelFocusFacts facts;
    facts.stillAimed = aimed;
    facts.heldForRegroup = regroup;
    facts.heldAtStagingPoint = staging;
    return facts;
}

void AStagingHoldKeepsTheDivertersDown()
{
    Check("an errand still aimed keeps its focus",
          TravelFocusOutlivesItsErrand(Focus(true, false, false)), true);
    Check("a regroup hold keeps the focus with no errand",
          TravelFocusOutlivesItsErrand(Focus(false, true, false)), true);
    // The measured case: the leader's errand released on arrival, no claim
    // standing, and the barrier holding him on the staging point.
    Check("a staging hold keeps the focus with no errand",
          TravelFocusOutlivesItsErrand(Focus(false, false, true)), true);
    Check("no errand and no hold hands the focus back",
          TravelFocusOutlivesItsErrand(Focus(false, false, false)), false);
}

}  // namespace

int main()
{
    EveryRunWalkPassesAnEmptyLearnColumn();
    TheFenceStillStandsWhereItProtectsAWalk();
    AStagingHoldKeepsTheDivertersDown();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("every run walk names its owner, and a held leader stops choosing\n");
    return EXIT_SUCCESS;
}
