/*
 * What stopped a cast this module drove at the core's own handler.
 *
 * mod-overseer#337. #336 gave three verbs one hold and the hold works: the
 * worldserver log has it going on and coming off around every attempt, and the
 * refusal rows carry `hold_applied: true`. The cast still does not go out.
 *
 * Measured on the dev realm 2026-09-08, eight consecutive hearths on the party
 * leader alternated in lockstep:
 *
 *   17:08:49  error      character is moving              <- hold placed
 *   17:09:14  unchanged  the cast went out and ... never left
 *   17:09:51  error      character is moving              <- hold placed
 *   17:10:16  unchanged  the cast went out and ... never left
 *
 * with the log putting the hold on at 17:08:51, the cast at 17:09:15 and the
 * release at 17:09:31. So the character was held for the whole cast, the
 * hearthstone cooldown still read clear at the verdict, and it stood 1,596
 * yards from a home it never reached.
 *
 * TWO THINGS WERE WRONG AND THIS FILE PINS BOTH.
 *
 * The row could not say why. `conjure` drives its cast through
 * PlayerbotAI::CastSpell and #330 taught it to name the wall it hit; `hearth`
 * and `summon` hand a packet to the core's own handler and recorded one bit,
 * whether a cast was running afterwards. When that bit was false the row had
 * nothing to say at all.
 *
 * And the row asserted a cast nobody saw. `the cast went out and the character
 * never left` was written into every `stayed` row whatever happened, including
 * these eight, where no cast went out. That sentence sent the reader looking
 * for a failed teleport when the failure was that nothing was ever cast.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CastWallBlocker;
using OverseerDecisions::CastWallGate;
using OverseerDecisions::HEARTH_STAYED_CAST_SEEN;
using OverseerDecisions::HEARTH_STAYED_NO_CAST;
using OverseerDecisions::HearthStayedDetail;
namespace CastWall = OverseerDecisions::CastWall;

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

void CheckWall(char const* what, CastWallGate const& gate, char const* want)
{
    char const* const got = CastWallBlocker(gate);
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got, want);
    ++failures;
}

// Every field spelled out at each call site rather than mutated from a shared
// fixture, so a case that reads as "sitting" cannot inherit a flag from the one
// above it.
CastWallGate Gate(bool grounded, bool standing, bool still, bool ready, bool free)
{
    CastWallGate gate;
    gate.grounded = grounded;
    gate.standing = standing;
    gate.still = still;
    gate.ready = ready;
    gate.free = free;
    return gate;
}

// A clear gate names nothing, and that is a real answer rather than a fallthrough:
// it is what the row says when the packet was parked on the spell queue for the
// global cooldown, which is indistinguishable from here and ends in a cast.
void AClearGateNamesNothing()
{
    CheckWall("nothing in the way", Gate(true, true, true, true, true), "");
    CheckWall("the defaults are a clear gate", CastWallGate(), "");
}

void EachWallIsNamedOnItsOwn()
{
    CheckWall("in flight", Gate(false, true, true, true, true), CastWall::InFlight);
    CheckWall("sitting", Gate(true, false, true, true, true), CastWall::NotStanding);
    CheckWall("moving", Gate(true, true, false, true, true), CastWall::Moving);
    CheckWall("on cooldown", Gate(true, true, true, false, true), CastWall::OnCooldown);
    CheckWall("already casting", Gate(true, true, true, true, false), CastWall::AlreadyCasting);
}

// THE ORDER IS THE CORE'S OWN, and it is checked by turning every wall on at
// once and then walking them off one at a time. A row must never name a wall the
// cast had not reached yet: saying `on cooldown` about a character that was
// flying would send an operator to wait out a cooldown that was never the
// problem.
void TheOrderIsTheOneTheCoreReachesThemIn()
{
    CheckWall("all five", Gate(false, false, false, false, false), CastWall::InFlight);
    CheckWall("four left", Gate(true, false, false, false, false), CastWall::NotStanding);
    CheckWall("three left", Gate(true, true, false, false, false), CastWall::Moving);
    CheckWall("two left", Gate(true, true, true, false, false), CastWall::OnCooldown);
    CheckWall("one left", Gate(true, true, true, true, false), CastWall::AlreadyCasting);
    CheckWall("none left", Gate(true, true, true, true, true), "");
}

// SITTING IS THE ONE THIS ISSUE WAS OPENED FOR, so it is pinned twice: it beats
// everything below it, and it is reachable with the character standing perfectly
// still, which is exactly the state the hold puts it in. A held character out of
// combat is one its own `food` strategy sits down to feed, so the hold's own
// success is what makes this wall likely.
void SittingIsReachableFromAWorkingHold()
{
    CheckWall("held still and sitting", Gate(true, false, true, true, true),
              CastWall::NotStanding);
    CheckWall("sitting outranks a cooldown", Gate(true, false, true, false, true),
              CastWall::NotStanding);
}

// The literals go straight into an UPDATE and into `detail`, so a quote in one
// would strand its row until the worldserver restarts, which is #318.
void NoLiteralCarriesAQuote()
{
    char const* const all[] = {
        CastWall::InFlight,      CastWall::NotStanding,     CastWall::Moving,
        CastWall::OnCooldown,    CastWall::AlreadyCasting,  CastWall::NoneNamed,
        HEARTH_STAYED_CAST_SEEN, HEARTH_STAYED_NO_CAST,
    };
    for (char const* literal : all)
    {
        Check("a literal is not empty", *literal != '\0', true);
        for (char const* c = literal; *c; ++c)
            Check("no literal carries a quote", *c == '\'' || *c == '"', false);
    }
}

// AND NONE OF THEM IS THE PRE-FLIGHT REFUSAL OF THE SAME NAME. `character is
// moving` is what a row says when it refused before sending anything; these are
// what a row says when it sent the packet and watched it come to nothing. A
// reader that cannot tell those apart cannot tell a verb that gave up from a
// verb that tried.
void AWallIsNotTheRefusalOfTheSameName()
{
    Check("moving differs from the refusal",
          std::string(CastWall::Moving) != "character is moving", true);
    Check("sitting differs from the refusal",
          std::string(CastWall::NotStanding) != "character is not standing", true);
    Check("in flight differs from the refusal",
          std::string(CastWall::InFlight) != "character is in flight", true);
}

// THE `stayed` VERDICT HAS TWO CAUSES AND ONLY ONE WAS EVER REPORTED.
void StayedSaysWhichOfItsTwoCausesItWas()
{
    Check("a cast that was seen", std::string(HearthStayedDetail(true)) == HEARTH_STAYED_CAST_SEEN,
          true);
    Check("a cast that never was", std::string(HearthStayedDetail(false)) == HEARTH_STAYED_NO_CAST,
          true);
    Check("and the two are different sentences",
          std::string(HEARTH_STAYED_CAST_SEEN) != HEARTH_STAYED_NO_CAST, true);
    // The old literal is kept exactly, because it is the one the retry table and
    // every reader outside this repository already key on for the case where it
    // was true all along.
    Check("the seen sentence is unchanged",
          std::string(HEARTH_STAYED_CAST_SEEN) == "the cast went out and the character never left",
          true);
}

}  // namespace

int main()
{
    AClearGateNamesNothing();
    EachWallIsNamedOnItsOwn();
    TheOrderIsTheOneTheCoreReachesThemIn();
    SittingIsReachableFromAWorkingHold();
    NoLiteralCarriesAQuote();
    AWallIsNotTheRefusalOfTheSameName();
    StayedSaysWhichOfItsTwoCausesItWas();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a cast that did not start says which wall it hit\n");
    return EXIT_SUCCESS;
}
