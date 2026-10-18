/*
 * The operator's guard (#163): an aim a person writes to get characters out
 * of ground that is killing them, and which holds until a person clears it.
 *
 * The live failure this pins. The documented emergency procedure for a death
 * spike wrote a homebind `at:` aim into every roster row. Fourteen minutes
 * later four of the five aims were gone and all five characters were back on
 * the ground they had been pulled out of, with seven deaths in the five
 * minutes around it. Nothing told the operator's aim apart from an errand the
 * module or the bridge wrote for its own reasons, so every rule that may end
 * or replace an errand applied to it:
 *
 *     a live dungeon run          wrote over it            (Outrank)
 *     a training stop             wrote over it            (Preempt)
 *     the death breaker           called it off as lethal  (Release)
 *     the quest drive             re-picked the same objective once it went
 *
 * So the guard has a spelling of its own, `guard:<map>:<x>,<y>,<z>`, and every
 * one of those rules is asked about it here.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ErrandDeathBreaker;
using OverseerDecisions::ErrandDeathLimits;
using OverseerDecisions::ErrandDeathRemedy;
using OverseerDecisions::ErrandDeathToll;
using OverseerDecisions::IsForeignTravelAim;
using OverseerDecisions::IsTravelGuardAim;
using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelGuardPlace;
using OverseerDecisions::TravelOwner;
using OverseerDecisions::TravelReleaseGate;
using OverseerDecisions::TravelReleaseGateFacts;

namespace
{

int failures = 0;

// The measured guard: the homebind the operator wrote, in whole yards.
char const* const GUARD = "guard:0:-8950,-132,84";

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

char const* ClaimName(TravelClaim claim)
{
    switch (claim)
    {
        case TravelClaim::Write:             return "write";
        case TravelClaim::Outrank:           return "write (a live run outranks it)";
        case TravelClaim::RefusedProfession: return "refused (profession)";
        case TravelClaim::RefusedForeign:    return "refused (foreign)";
        case TravelClaim::Preempt:           return "write (a training stop takes it)";
    }
    return "unknown";
}

char const* RemedyName(ErrandDeathRemedy remedy)
{
    switch (remedy)
    {
        case ErrandDeathRemedy::Continue:        return "Continue";
        case ErrandDeathRemedy::Release:         return "Release";
        case ErrandDeathRemedy::RefuseReissue:   return "RefuseReissue";
        case ErrandDeathRemedy::DeclineRunOwned: return "DeclineRunOwned";
        case ErrandDeathRemedy::EndCatchUp:      return "EndCatchUp";
    }
    return "unknown";
}

// THE SPELLING. A guard is recognised by its prefix and nothing else, and the
// place it names is handed to the travel drive as the `at:` aim it already
// knows how to walk.
void AGuardIsItsOwnSpelling()
{
    CheckBool("the measured guard is a guard", IsTravelGuardAim(GUARD), true);
    CheckBool("an `at:` aim is not a guard", IsTravelGuardAim("at:0:-8950,-132,84"), false);
    CheckBool("an empty column is not a guard", IsTravelGuardAim(""), false);
    CheckBool("a keyword is not a guard", IsTravelGuardAim("vendor"), false);
    CheckBool("a prefix in the middle is not a guard",
              IsTravelGuardAim("at:guard:0:1,2,3"), false);

    CheckText("a guard walks to the place it names", TravelGuardPlace(GUARD),
              "at:0:-8950,-132,84");
    CheckText("a guard with a fractional place keeps it",
              TravelGuardPlace("guard:1:-1175.1,-2532.8,123.9"), "at:1:-1175.1,-2532.8,123.9");
    CheckText("anything else names no guard place", TravelGuardPlace("at:0:1,2,3"), "");
    CheckText("a bare prefix names no place", TravelGuardPlace("guard:"), "");

    // It has to fit the column, or the operator cannot write it at all.
    CheckBool("the measured guard fits VARCHAR(32)", std::string(GUARD).size() <= 32, true);
}

// NO RELEASE BLANKS IT. The release gate skips the column write for a foreign
// aim the book never claimed; a guard has to be one.
void NoReleaseBlanksAGuard()
{
    CheckBool("a guard is foreign to the travel book", IsForeignTravelAim(GUARD), true);

    TravelReleaseGateFacts gate;
    gate.claimed = false;
    gate.changedSinceClaim = false;
    gate.learnSkill = 0;
    gate.standing = GUARD;
    CheckBool("an arrival or backstop release leaves a guard in the column",
              TravelReleaseGate(gate).empty(), false);
}

// NO CLAIM WRITES OVER IT, WHOEVER IS CLAIMING. The two that used to win are
// the ones that matter: a live run outranked a positional aim (#656) and a
// training stop preempted one (2026-09-29).
void NoClaimWritesOverAGuard()
{
    TravelOwner const owners[] = {
        TravelOwner::HomeErrand, TravelOwner::CatchUp,      TravelOwner::Run,
        TravelOwner::WalkBackIn, TravelOwner::Exit,         TravelOwner::Respec,
        TravelOwner::TrainingStop, TravelOwner::TownTrip,
    };
    for (TravelOwner owner : owners)
    {
        for (uint32_t learn : {0u, 186u})
        {
            TravelClaimFacts facts(owner);
            facts.learnSkill = learn;
            facts.column = GUARD;
            facts.columnIsOurs = false;
            facts.target = "at:36:-16.4,-383.1,61.8";
            TravelClaim const got = ReadTravelClaim(facts);
            if (got != TravelClaim::RefusedForeign)
            {
                std::printf("FAIL owner %d (learn %u) over a guard: got %s, wanted %s\n",
                            static_cast<int>(owner), learn, ClaimName(got),
                            ClaimName(TravelClaim::RefusedForeign));
                ++failures;
            }
        }
    }

    // AND THE ORDINARY POSITIONAL AIM KEEPS THE ANSWERS IT HAD, or this has
    // traded one lever for another.
    TravelClaimFacts run(TravelOwner::Run);
    run.column = "at:0:-8950,-132,84";
    run.target = "at:36:-16.4,-383.1,61.8";
    if (ReadTravelClaim(run) != TravelClaim::Outrank)
    {
        std::printf("FAIL a live run over an ordinary `at:` aim: got %s, wanted %s\n",
                    ClaimName(ReadTravelClaim(run)), ClaimName(TravelClaim::Outrank));
        ++failures;
    }
}

// THE DEATH BREAKER NEVER CALLS IT OFF. Three deaths in the window on an
// ordinary errand is a release; on a guard it is the reason the guard exists.
void TheDeathBreakerLeavesAGuard()
{
    ErrandDeathLimits const limits;

    ErrandDeathToll toll;
    toll.deaths = limits.deaths + 2;
    toll.partyDeaths = 7;
    toll.guard = true;
    ErrandDeathRemedy got = ErrandDeathBreaker(toll, limits).remedy;
    if (got != ErrandDeathRemedy::Continue)
    {
        std::printf("FAIL deaths over the line on a guard: got %s, wanted Continue\n",
                    RemedyName(got));
        ++failures;
    }

    // Not refused either, whatever an earlier refusal of the same place says.
    toll.sinceRefused = 10;
    toll.strikes = 1;
    got = ErrandDeathBreaker(toll, limits).remedy;
    if (got != ErrandDeathRemedy::Continue)
    {
        std::printf("FAIL a guard over an earlier refusal: got %s, wanted Continue\n",
                    RemedyName(got));
        ++failures;
    }

    // THE ORDINARY ERRAND IS STILL RELEASED, or the breaker has been switched
    // off rather than taught about the guard.
    toll.guard = false;
    toll.sinceRefused = -1;
    toll.strikes = 0;
    got = ErrandDeathBreaker(toll, limits).remedy;
    if (got != ErrandDeathRemedy::Release)
    {
        std::printf("FAIL deaths over the line on an ordinary errand: got %s, wanted Release\n",
                    RemedyName(got));
        ++failures;
    }
}

}  // namespace

int main()
{
    AGuardIsItsOwnSpelling();
    NoReleaseBlanksAGuard();
    NoClaimWritesOverAGuard();
    TheDeathBreakerLeavesAGuard();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
