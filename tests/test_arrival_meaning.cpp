/*
 * What arriving at a creature MEANS (mod-overseer#402).
 *
 * Four handlers can run when a character arrives at a creature. Three of them
 * asked the aim STRING what to do and one asked the world, and only the one
 * that asked the world worked for an aim naming a creature by entry. The three
 * that asked the string misfired three different ways:
 *
 *   * a learn aim was consumed by any arrival at all, so walking a character to
 *     a flight master cancelled its profession;
 *   * `auctioneer` was missing from the counter vocabulary entirely, so the
 *     auction leg could never be held at a counter; and
 *   * an entry-id aim resolved to no counter role, so a repair vendor reached
 *     by entry was not held while the same vendor reached by keyword was.
 *
 * These tests are the three rules that fix that, plus the arithmetic of the
 * flag values the pure layer has to restate because it compiles without a core.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ArrivalAnswersLearnAim;
using OverseerDecisions::CounterArrival;
using OverseerDecisions::CounterArrivalStep;
using OverseerDecisions::CounterRole;
using OverseerDecisions::CounterRoleForAim;
using OverseerDecisions::CounterRoleForNpcFlags;

namespace
{

int failures = 0;

char const* RoleName(CounterRole role)
{
    switch (role)
    {
        case CounterRole::None:       return "None";
        case CounterRole::Vendor:     return "Vendor";
        case CounterRole::Banker:     return "Banker";
        case CounterRole::Repairer:   return "Repairer";
        case CounterRole::Auctioneer: return "Auctioneer";
    }
    return "unknown";
}

void CheckRole(char const* what, CounterRole got, CounterRole want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, RoleName(got), RoleName(want));
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

void CheckUInt(char const* what, std::uint32_t got, std::uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

// --------------------------------------------------- the restated flags --

void TheFlagValuesMatchTheCore()
{
    // UnitDefines.h:321-344 at the pinned core. Restated in the pure header
    // because this translation unit compiles without a core, and asserted here
    // so a mistyped constant fails a test rather than silently sending an
    // errand at the wrong sort of creature.
    CheckUInt("trainer",      OverseerDecisions::NPC_FLAG_TRAINER,      0x00000010);
    CheckUInt("vendor",       OverseerDecisions::NPC_FLAG_VENDOR,       0x00000080);
    CheckUInt("repair",       OverseerDecisions::NPC_FLAG_REPAIR,       0x00001000);
    CheckUInt("flightmaster", OverseerDecisions::NPC_FLAG_FLIGHTMASTER, 0x00002000);
    CheckUInt("banker",       OverseerDecisions::NPC_FLAG_BANKER,       0x00020000);
    CheckUInt("auctioneer",   OverseerDecisions::NPC_FLAG_AUCTIONEER,   0x00200000);

    // AND THE TWO SPAWNS THIS MODULE HAS ACTUALLY STOOD NEXT TO, as arithmetic
    // rather than as a comment. A repair vendor measured at 4224 and a flight
    // master measured at 8195. If either sum stops working, one of the values
    // above is wrong.
    CheckUInt("a repair vendor reads 4224",
              OverseerDecisions::NPC_FLAG_REPAIR | OverseerDecisions::NPC_FLAG_VENDOR,
              4224);
    CheckUInt("a flight master reads 8195 with gossip and questgiver",
              OverseerDecisions::NPC_FLAG_FLIGHTMASTER | 0x2u | 0x1u, 8195);
}

// ------------------------------------------------ the counter vocabulary --

void TheThreeShippedKeywordsAreUnchanged()
{
    CheckRole("vendor", CounterRoleForAim("vendor"), CounterRole::Vendor);
    CheckRole("banker", CounterRoleForAim("banker"), CounterRole::Banker);
    CheckRole("repair", CounterRoleForAim("repair"), CounterRole::Repairer);
}

void TheAuctioneerKeywordIsFinallyOneOfThem()
{
    // The defect: `auctioneer` is one of the thirteen keywords TravelRoles()
    // resolves, so a character could always be SENT to one, and this function
    // answered None, so CounterArrivalStep answered Done on its first line and
    // the errand released with no hold. The release is the signal the outside
    // pass reads as "arrived" before it writes a row.
    CheckRole("auctioneer", CounterRoleForAim("auctioneer"), CounterRole::Auctioneer);

    // And it has to reach the hold, which is the thing that was actually
    // missing. In reach at an auctioneer means stand and trade, exactly as it
    // does at the other three.
    CheckRole("...and it is a counter",
              CounterRoleForAim("auctioneer") == CounterRole::None ? CounterRole::None
                                                                   : CounterRole::Auctioneer,
              CounterRole::Auctioneer);
    Check("an auctioneer in reach is held",
          CounterArrivalStep(CounterRole::Auctioneer, true, true) ==
              CounterArrival::StandAndTrade,
          true);
    Check("an auctioneer nearby but out of reach closes the gap",
          CounterArrivalStep(CounterRole::Auctioneer, false, true) ==
              CounterArrival::CloseTheGap,
          true);
}

void AnAimThatIsNotAKeywordIsStillNotAKeyword()
{
    // The match stays exact and whole. An entry id, a coordinate and a portal
    // are not counter keywords and must not become them by accident.
    CheckRole("an entry id", CounterRoleForAim("16227"), CounterRole::None);
    CheckRole("a coordinate", CounterRoleForAim("at:1:-898.2,-3769.6,11.8"),
              CounterRole::None);
    CheckRole("a portal", CounterRoleForAim("trigger:2226"), CounterRole::None);
    CheckRole("a trainer is not a counter", CounterRoleForAim("trainer"),
              CounterRole::None);
    CheckRole("a flight master is not a counter", CounterRoleForAim("flight master"),
              CounterRole::None);
    CheckRole("an empty aim", CounterRoleForAim(""), CounterRole::None);
    // Case matters, as it always has. Worth pinning so a future edit that
    // loosens it has to change a test on purpose.
    CheckRole("case is not folded", CounterRoleForAim("Repair"), CounterRole::None);
}

// ------------------------------------- the same question, asked of the world --

void TheCreaturesOwnFlagsAnswerWhenTheAimCannot()
{
    CheckRole("a plain vendor", CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_VENDOR),
              CounterRole::Vendor);
    CheckRole("a banker", CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_BANKER),
              CounterRole::Banker);
    CheckRole("an auctioneer", CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_AUCTIONEER),
              CounterRole::Auctioneer);
    CheckRole("a repairer", CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_REPAIR),
              CounterRole::Repairer);
}

void NarrowBeatsBroadWhenACreatureIsSeveralThings()
{
    // THE CASE THAT DECIDES THE PRECEDENCE, and it is the real spawn: the
    // repair vendor this was measured against carries 4224, which is repair and
    // vendor together. A vendor-first test would mean no repairer is ever held
    // as a repairer, and the repair leg would keep failing for a new reason.
    CheckRole("a repair vendor is a repairer", CounterRoleForNpcFlags(4224),
              CounterRole::Repairer);

    // The rest of the documented order, each against the broad bit it usually
    // co-occurs with.
    CheckRole("a banker that also sells is a banker",
              CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_BANKER |
                                     OverseerDecisions::NPC_FLAG_VENDOR),
              CounterRole::Banker);
    CheckRole("an auctioneer that also sells is an auctioneer",
              CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_AUCTIONEER |
                                     OverseerDecisions::NPC_FLAG_VENDOR),
              CounterRole::Auctioneer);
    CheckRole("repair outranks banker",
              CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_REPAIR |
                                     OverseerDecisions::NPC_FLAG_BANKER),
              CounterRole::Repairer);
}

void ACreatureThatIsNoCounterGetsNoHold()
{
    // The case this was written for. A flight master reads 8195 and has no rows
    // waiting on a counter hold, so giving it one would pin the character for
    // the hold's whole ceiling for nothing.
    CheckRole("a flight master", CounterRoleForNpcFlags(8195), CounterRole::None);
    CheckRole("a trainer", CounterRoleForNpcFlags(OverseerDecisions::NPC_FLAG_TRAINER),
              CounterRole::None);
    CheckRole("nothing at all", CounterRoleForNpcFlags(0), CounterRole::None);
    // Gossip and questgiver alone are not a counter either.
    CheckRole("gossip and questgiver", CounterRoleForNpcFlags(0x3), CounterRole::None);

    Check("...and None still answers Done on its first line",
          CounterArrivalStep(CounterRole::None, false, false) == CounterArrival::Done,
          true);
}

// ------------------------------------------------------------ the learn aim --

void AFlightMasterDoesNotCancelAProfession()
{
    // THE DEFECT, as one line. The errand was a flight master named by entry,
    // so the aim names no trainer, and the creature does not train. Before
    // #402 this arrival called TrainOnArrival anyway, which warned about an
    // errand nobody issued and cleared the learn column.
    Check("a flight master arrival does not answer a learn aim",
          ArrivalAnswersLearnAim(false, false), false);
}

void AnArrivalThatIsAboutTrainingStillAnswersIt()
{
    // Both halves keep today's behaviour where today is right.
    //
    // The errand WAS a trainer errand, so whatever happened at the far end
    // answers the plan - including "this trainer cannot teach me", which is a
    // real answer and correctly clears it so a fresh errand can pick one that
    // can.
    Check("a trainer aim answers it", ArrivalAnswersLearnAim(true, true), true);
    Check("...even when the creature turns out not to train",
          ArrivalAnswersLearnAim(true, false), true);

    // Or the character is standing at a trainer whatever it was sent for, and
    // trying is free.
    Check("standing at a trainer answers it", ArrivalAnswersLearnAim(false, true), true);
}

}  // namespace

int main()
{
    TheFlagValuesMatchTheCore();

    TheThreeShippedKeywordsAreUnchanged();
    TheAuctioneerKeywordIsFinallyOneOfThem();
    AnAimThatIsNotAKeywordIsStillNotAKeyword();

    TheCreaturesOwnFlagsAnswerWhenTheAimCannot();
    NarrowBeatsBroadWhenACreatureIsSeveralThings();
    ACreatureThatIsNoCounterGetsNoHold();

    AFlightMasterDoesNotCancelAProfession();
    AnArrivalThatIsAboutTrainingStillAnswersIt();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: an arrival means what the creature is, not how the aim was spelled\n");
    return EXIT_SUCCESS;
}
