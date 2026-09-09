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
 * AND THEN THE FIVE WALLS WERE NOT ENOUGH, WHICH IS WHAT 2026-09-09 MEASURED.
 * Six more hearths on the same leader on the build carrying that gate:
 *
 *   error      character is moving
 *   unchanged  the cast never started and the character never left
 *   unchanged  the cast never started and the character never left
 *   error      the character moved, and not to its home
 *   unchanged  the cast never started and the character never left
 *   unchanged  the cast never started and the character never left
 *
 * with the hold reporting `already standing` every time and the hearthstone
 * cooldown clear at every verdict, so all five walls read clear and the row
 * fell through to `the cast did not start and nothing in front of it was true`.
 *
 * THE DISCRIMINATOR WAS THE LEADER. The same verb worked first try on a
 * follower the same evening. The leader is the character the travel drive walks
 * across a continent, and a bot walking a long way is a MOUNTED bot; the core
 * refuses a mounted caster at Spell.cpp:6018 and reports it to a client a bot
 * does not have. So this file pins two more walls, the mount and the global
 * cooldown, and a third sentence for a cast the core parked on its own queue
 * rather than refused.
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
using OverseerDecisions::HEARTH_STAYED_QUEUED;
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
// above it. The arguments are in the same order CastWallBlocker asks them, so a
// row of booleans reads as the gate it describes.
CastWallGate Gate(bool grounded, bool unmounted, bool standing, bool still, bool ready,
                  bool globalReady, bool free)
{
    CastWallGate gate;
    gate.grounded = grounded;
    gate.unmounted = unmounted;
    gate.standing = standing;
    gate.still = still;
    gate.ready = ready;
    gate.globalReady = globalReady;
    gate.free = free;
    return gate;
}

// A clear gate names nothing, and that is a real answer rather than a fallthrough:
// it is what the row says when the packet was parked on the spell queue for the
// global cooldown, which is indistinguishable from here and ends in a cast.
void AClearGateNamesNothing()
{
    CheckWall("nothing in the way", Gate(true, true, true, true, true, true, true), "");
    CheckWall("the defaults are a clear gate", CastWallGate(), "");
}

void EachWallIsNamedOnItsOwn()
{
    CheckWall("in flight", Gate(false, true, true, true, true, true, true), CastWall::InFlight);
    CheckWall("mounted", Gate(true, false, true, true, true, true, true), CastWall::Mounted);
    CheckWall("sitting", Gate(true, true, false, true, true, true, true), CastWall::NotStanding);
    CheckWall("moving", Gate(true, true, true, false, true, true, true), CastWall::Moving);
    CheckWall("on cooldown", Gate(true, true, true, true, false, true, true),
              CastWall::OnCooldown);
    CheckWall("on the global cooldown", Gate(true, true, true, true, true, false, true),
              CastWall::OnGlobalCooldown);
    CheckWall("already casting", Gate(true, true, true, true, true, true, false),
              CastWall::AlreadyCasting);
}

// THE ORDER IS CHECKED BY TURNING EVERY WALL ON AT ONCE and then walking them
// off one at a time. A row must never name a wall an operator cannot act on
// while a nearer one stands: saying `on cooldown` about a character that was on
// a taxi would send somebody to wait out a cooldown that was never the problem.
void TheOrderIsTheOneAWallIsNamedIn()
{
    CheckWall("all seven", Gate(false, false, false, false, false, false, false),
              CastWall::InFlight);
    CheckWall("six left", Gate(true, false, false, false, false, false, false),
              CastWall::Mounted);
    CheckWall("five left", Gate(true, true, false, false, false, false, false),
              CastWall::NotStanding);
    CheckWall("four left", Gate(true, true, true, false, false, false, false), CastWall::Moving);
    CheckWall("three left", Gate(true, true, true, true, false, false, false),
              CastWall::OnCooldown);
    CheckWall("two left", Gate(true, true, true, true, true, false, false),
              CastWall::OnGlobalCooldown);
    CheckWall("one left", Gate(true, true, true, true, true, true, false),
              CastWall::AlreadyCasting);
    CheckWall("none left", Gate(true, true, true, true, true, true, true), "");
}

// SITTING IS THE ONE THE ISSUE WAS OPENED FOR, so it is pinned twice: it beats
// everything below it, and it is reachable with the character standing perfectly
// still, which is exactly the state the hold puts it in. A held character out of
// combat is one its own `food` strategy sits down to feed, so the hold's own
// success is what makes this wall likely.
void SittingIsReachableFromAWorkingHold()
{
    CheckWall("held still and sitting", Gate(true, true, false, true, true, true, true),
              CastWall::NotStanding);
    CheckWall("sitting outranks a cooldown", Gate(true, true, false, true, false, true, true),
              CastWall::NotStanding);
}

// AND THE MOUNT IS THE ONE THE ISSUE WAS REOPENED FOR, so it is pinned the same
// way. It is reachable with every one of the original five clear, which is the
// whole finding: six rows on the party leader reported a character standing
// still, off cooldown, not casting and not in flight, and no cast went out.
// `grounded` was reading one branch of Spell.cpp:6018 and this is the other.
void AMountIsReachableWithEveryOtherWallClear()
{
    CheckWall("standing still on a mount", Gate(true, false, true, true, true, true, true),
              CastWall::Mounted);
    Check("and it is not the sentence a taxi gets",
          std::string(CastWall::Mounted) != CastWall::InFlight, true);
    // The taxi still wins, because a character in flight is one whose teleport
    // could not land either; naming the mount there would answer the smaller
    // half of a bigger problem.
    CheckWall("a taxi outranks a mount", Gate(false, false, true, true, true, true, true),
              CastWall::InFlight);
}

// THE GLOBAL COOLDOWN IS SEPARATE FROM THE SPELL'S OWN, and the two sentences
// have to differ, because the actions they imply differ: a hearthstone cooldown
// is an hour and a global one is a second and a half. `conjure` has told them
// apart since #330 and this gate could not.
void TheTwoCooldownsAreDifferentSentences()
{
    Check("the two cooldowns read differently",
          std::string(CastWall::OnCooldown) != CastWall::OnGlobalCooldown, true);
    CheckWall("the spell's own comes first",
              Gate(true, true, true, true, false, false, true), CastWall::OnCooldown);
    CheckWall("and the global one is named on its own",
              Gate(true, true, true, true, true, false, true), CastWall::OnGlobalCooldown);
}

// The literals go straight into an UPDATE and into `detail`, so a quote in one
// would strand its row until the worldserver restarts, which is #318.
void NoLiteralCarriesAQuote()
{
    char const* const all[] = {
        CastWall::InFlight,         CastWall::Mounted,          CastWall::NotStanding,
        CastWall::Moving,           CastWall::OnCooldown,       CastWall::OnGlobalCooldown,
        CastWall::AlreadyCasting,   CastWall::Queued,           CastWall::NoneNamed,
        HEARTH_STAYED_CAST_SEEN,    HEARTH_STAYED_NO_CAST,      HEARTH_STAYED_QUEUED,
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
    Check("mounted differs from the refusal",
          std::string(CastWall::Mounted) != "character is on a mount", true);
}

// THE `stayed` VERDICT HAS THREE CAUSES AND ONLY ONE WAS EVER REPORTED.
void StayedSaysWhichOfItsThreeCausesItWas()
{
    Check("a cast that was seen",
          std::string(HearthStayedDetail(true, false)) == HEARTH_STAYED_CAST_SEEN, true);
    Check("a cast that never was",
          std::string(HearthStayedDetail(false, false)) == HEARTH_STAYED_NO_CAST, true);
    Check("a cast the core parked",
          std::string(HearthStayedDetail(false, true)) == HEARTH_STAYED_QUEUED, true);
    // A CAST THAT WAS SEEN BEATS A QUEUE ENTRY, and the case is real rather than
    // defensive: the queue holds one request per category, so a row can find an
    // unrelated entry there while its own cast is visibly running. Seen is the
    // one reading here that nothing has to infer, so it wins.
    Check("seeing one beats finding one parked",
          std::string(HearthStayedDetail(true, true)) == HEARTH_STAYED_CAST_SEEN, true);
    Check("and the three are different sentences",
          std::string(HEARTH_STAYED_CAST_SEEN) != HEARTH_STAYED_NO_CAST &&
              std::string(HEARTH_STAYED_NO_CAST) != HEARTH_STAYED_QUEUED &&
              std::string(HEARTH_STAYED_CAST_SEEN) != HEARTH_STAYED_QUEUED,
          true);
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
    TheOrderIsTheOneAWallIsNamedIn();
    SittingIsReachableFromAWorkingHold();
    AMountIsReachableWithEveryOtherWallClear();
    TheTwoCooldownsAreDifferentSentences();
    NoLiteralCarriesAQuote();
    AWallIsNotTheRefusalOfTheSameName();
    StayedSaysWhichOfItsThreeCausesItWas();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a cast that did not start says which wall it hit\n");
    return EXIT_SUCCESS;
}
