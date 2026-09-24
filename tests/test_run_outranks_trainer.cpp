/*
 * A live dungeon run's walks outrank a pending trainer trip and a positional
 * walk (#656).
 *
 * MEASURED ON THE DEV REALM 2026-09-24. At 03:51 a far catch-up aimed Oz, Uzza
 * and Zork at the spot their leader hearthed to, 'at:1:-610.983,-4320.09,
 * 39.7389'. The worldserver restarted, and TravelAimBook forgot that it had
 * written the aim. At 05:36 the Ragefire run's BARRIER claim for each member
 * was refused for a pending learn (skills 197, 182, 393) over that aim. The
 * release could not clear it either, and staging timed out with the three
 * members 2,400 yards from the door.
 *
 * The way a human group plays: while the run is live, its walks come first
 * and the trainer trip waits until after it. `learn_skill` is left pending.
 * A counter errand still stands, and every other owner keeps its fences.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelOwner;
using OverseerDecisions::TravelOwnerIsALiveRun;

namespace
{

int failures = 0;

char const* const STAGING = "at:1:1807.39,-4407.8,-18.4334";
char const* const STALE_CATCH_UP = "at:1:-610.983,-4320.09,39.7389";

char const* Name(TravelClaim claim)
{
    switch (claim)
    {
        case TravelClaim::Write:             return "write";
        case TravelClaim::Outrank:           return "write (a live run outranks it)";
        case TravelClaim::RefusedProfession: return "refused (profession)";
        case TravelClaim::RefusedForeign:    return "refused (foreign)";
    }
    return "unknown";
}

void CheckClaim(char const* what, TravelClaim got, TravelClaim want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

TravelClaim Claim(TravelOwner owner, uint32_t learnSkill, char const* column)
{
    TravelClaimFacts facts(owner);
    facts.learnSkill = learnSkill;
    facts.column = column;
    facts.columnIsOurs = false;
    facts.target = STAGING;
    return ReadTravelClaim(facts);
}

void TheMeasuredBarrierClaimIsWritten()
{
    // Oz, Uzza and Zork as the 05:50 roster read them.
    CheckClaim("Oz: BARRIER over the forgotten catch-up with skill 197 pending",
               Claim(TravelOwner::Run, 197, STALE_CATCH_UP), TravelClaim::Outrank);
    CheckClaim("Uzza: BARRIER over the forgotten catch-up with skill 182 pending",
               Claim(TravelOwner::Run, 182, STALE_CATCH_UP), TravelClaim::Outrank);
    CheckClaim("Zork: BARRIER over the forgotten catch-up with skill 393 pending",
               Claim(TravelOwner::Run, 393, STALE_CATCH_UP), TravelClaim::Outrank);
    // The foreign fence alone would have refused it too.
    CheckClaim("a run over a positional aim with no learn pending",
               Claim(TravelOwner::Run, 0, STALE_CATCH_UP), TravelClaim::Outrank);
    CheckClaim("the walk back in over a positional aim with a learn pending",
               Claim(TravelOwner::WalkBackIn, 197, STALE_CATCH_UP),
               TravelClaim::Outrank);
}

void TheTrainerTripWaitsForTheRun()
{
    CheckClaim("a run over a trainer walk in flight",
               Claim(TravelOwner::Run, 186, "profession trainer"),
               TravelClaim::Outrank);
    CheckClaim("the walk back in over a trainer walk in flight",
               Claim(TravelOwner::WalkBackIn, 186, "profession trainer"),
               TravelClaim::Outrank);
    // Over an empty column nothing is taken over, which is #589's plain write.
    CheckClaim("a run over an empty column with a learn pending",
               Claim(TravelOwner::Run, 186, ""), TravelClaim::Write);
}

void ACounterErrandStillStands()
{
    CheckClaim("a run over a vendor errand is refused as foreign",
               Claim(TravelOwner::Run, 0, "vendor"), TravelClaim::RefusedForeign);
    CheckClaim("a run over a banker errand with a learn pending is refused",
               Claim(TravelOwner::Run, 197, "banker"), TravelClaim::RefusedProfession);
    CheckClaim("the walk back in over a repair errand is refused as foreign",
               Claim(TravelOwner::WalkBackIn, 0, "repair"), TravelClaim::RefusedForeign);
}

void EveryOtherOwnerKeepsItsFences()
{
    CheckBool("a run is a live run", TravelOwnerIsALiveRun(TravelOwner::Run), true);
    CheckBool("the walk back in is a live run",
              TravelOwnerIsALiveRun(TravelOwner::WalkBackIn), true);
    CheckBool("a catch-up is not", TravelOwnerIsALiveRun(TravelOwner::CatchUp), false);
    CheckBool("a home errand is not", TravelOwnerIsALiveRun(TravelOwner::HomeErrand),
              false);
    CheckBool("a talent reset is not", TravelOwnerIsALiveRun(TravelOwner::Respec), false);

    CheckClaim("a catch-up over a positional aim with a learn pending",
               Claim(TravelOwner::CatchUp, 197, STALE_CATCH_UP),
               TravelClaim::RefusedProfession);
    CheckClaim("a catch-up over a positional aim",
               Claim(TravelOwner::CatchUp, 0, STALE_CATCH_UP),
               TravelClaim::RefusedForeign);
    CheckClaim("a home errand over a positional aim with a learn pending",
               Claim(TravelOwner::HomeErrand, 197, STALE_CATCH_UP),
               TravelClaim::RefusedProfession);
    CheckClaim("a talent reset over a trainer walk",
               Claim(TravelOwner::Respec, 186, "profession trainer"),
               TravelClaim::RefusedProfession);
}

void TheBooksOwnAimIsStillAPlainWrite()
{
    TravelClaimFacts facts(TravelOwner::Run);
    facts.learnSkill = 197;
    facts.column = STAGING;
    facts.columnIsOurs = true;
    facts.target = "at:1:1810.0,-4410.0,-18.4";
    CheckClaim("a run's next leg over its own aim is not a takeover",
               ReadTravelClaim(facts), TravelClaim::Write);
}

}  // namespace

int main()
{
    TheMeasuredBarrierClaimIsWritten();
    TheTrainerTripWaitsForTheRun();
    ACounterErrandStillStands();
    EveryOtherOwnerKeepsItsFences();
    TheBooksOwnAimIsStillAPlainWrite();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a live run's walks outrank a trainer trip and a positional walk\n");
    return EXIT_SUCCESS;
}
