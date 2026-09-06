/*
 * Why the fall baseline guard declined, recorded as a number.
 *
 * WHAT THIS IS FOR. #266 deployed an invariant: a character that is not
 * falling is standing somewhere, and a character that is standing somewhere
 * owes nothing for having got there, so the drive hands the core a baseline at
 * that character's own feet once a second. Phantom fall deaths continued, and
 * the reason was not the rule.
 *
 * FallBaselineStep declines only on `!mayInspect || falling`, and its call site
 * is above the recovery's own stand-down and outside the episode cooldown. At a
 * one second poll, a guard that ran leaves m_lastFallZ within one second of
 * movement of the character's feet. The deaths of 2026-09-06 need it 69 or more
 * yards away, and two of those rows measured a descent of exactly zero. Both
 * cannot hold, so the guard was not called: correct, and not running, which in
 * the data looks exactly like called and wrong.
 *
 * This file pins the thing that tells the two apart. It changes no decision.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::FallGuardStandDownMask;
using OverseerDecisions::FallGuardStandDownNames;
using OverseerDecisions::TerrainRecoveryMayInspect;

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

void CheckMask(char const* what, uint16_t got, uint16_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got 0x%02X, wanted 0x%02X\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// An ordinary living character on the ground stands nothing down, and that
// zero is a reading rather than an absence. The caller carries "never looked"
// as a negative, exactly like recovery_rung and yards_fallen.
void AnOrdinaryCharacterReadsAsRan()
{
    CheckMask("nothing declines the guard",
              FallGuardStandDownMask(true, false, false, false, false, false, false, false),
              OverseerDecisions::FALL_GUARD_RAN);
    CheckText("and says so in a word", FallGuardStandDownNames(0), "ran");
    Check("which is the same answer TerrainRecoveryMayInspect gives",
          TerrainRecoveryMayInspect(true, false, false, false, false, false, false, false),
          true);
}

// One bit each, in the argument order TerrainRecoveryMayInspect takes, so the
// two cannot drift apart without this failing.
void EachStandDownHasItsOwnBit()
{
    CheckMask("dead", FallGuardStandDownMask(false, false, false, false, false, false, false, false),
              OverseerDecisions::FALL_GUARD_DEAD);
    CheckMask("teleporting", FallGuardStandDownMask(true, true, false, false, false, false, false, false),
              OverseerDecisions::FALL_GUARD_TELEPORTING);
    CheckMask("in flight", FallGuardStandDownMask(true, false, true, false, false, false, false, false),
              OverseerDecisions::FALL_GUARD_IN_FLIGHT);
    CheckMask("flying", FallGuardStandDownMask(true, false, false, true, false, false, false, false),
              OverseerDecisions::FALL_GUARD_FLYING);
    CheckMask("falling", FallGuardStandDownMask(true, false, false, false, true, false, false, false),
              OverseerDecisions::FALL_GUARD_FALLING);
    CheckMask("in water", FallGuardStandDownMask(true, false, false, false, false, true, false, false),
              OverseerDecisions::FALL_GUARD_IN_WATER);
    CheckMask("on a transport", FallGuardStandDownMask(true, false, false, false, false, false, true, false),
              OverseerDecisions::FALL_GUARD_TRANSPORT);
    CheckMask("in a vehicle", FallGuardStandDownMask(true, false, false, false, false, false, false, true),
              OverseerDecisions::FALL_GUARD_VEHICLE);

    // Every bit distinct, or two reasons would be indistinguishable on the row.
    uint16_t seen = 0;
    uint16_t const bits[] = {
        OverseerDecisions::FALL_GUARD_DEAD, OverseerDecisions::FALL_GUARD_TELEPORTING,
        OverseerDecisions::FALL_GUARD_IN_FLIGHT, OverseerDecisions::FALL_GUARD_FLYING,
        OverseerDecisions::FALL_GUARD_FALLING, OverseerDecisions::FALL_GUARD_IN_WATER,
        OverseerDecisions::FALL_GUARD_TRANSPORT, OverseerDecisions::FALL_GUARD_VEHICLE};
    for (uint16_t b : bits)
    {
        Check("each reason has a bit of its own", (seen & b) == 0, true);
        seen |= b;
    }
    CheckMask("and eight of them fit in one byte", seen, 0x00FF);
}

// THE POINT OF A MASK. The state under suspicion is a character whose flags say
// flying and falling at once, and a first-match answer would report whichever
// the code happened to test first and hide the other.
void TwoReasonsAtOnceAreBothReported()
{
    uint16_t const both =
        FallGuardStandDownMask(true, false, false, true, true, false, false, false);
    Check("flying is reported", (both & OverseerDecisions::FALL_GUARD_FLYING) != 0, true);
    Check("and so is falling", (both & OverseerDecisions::FALL_GUARD_FALLING) != 0, true);
    CheckMask("as one value that is neither of them alone", both,
              uint16_t(OverseerDecisions::FALL_GUARD_FLYING |
                       OverseerDecisions::FALL_GUARD_FALLING));
    Check("which is not what either alone reads as",
          both != OverseerDecisions::FALL_GUARD_FLYING &&
              both != OverseerDecisions::FALL_GUARD_FALLING, true);
    CheckText("and reads in bit order", FallGuardStandDownNames(both), "flying|falling");
}

// The teleport-surviving trio, which is the shape that fits a character dying
// and reviving every minute without ever recovering: TeleportTo reduces the
// flags to a mask that drops FALLING but keeps DISABLE_GRAVITY, CAN_FLY and
// HOVER, so a stand-down caused by IsFlying outlives every death.
void TheStateUnderSuspicionIsDistinguishable()
{
    uint16_t const flyingOnly =
        FallGuardStandDownMask(true, false, false, true, false, false, false, false);
    uint16_t const fallingOnly =
        FallGuardStandDownMask(true, false, false, false, true, false, false, false);
    uint16_t const deadAndFlying =
        FallGuardStandDownMask(false, false, false, true, false, false, false, false);

    Check("a stuck flight flag is not a fall", flyingOnly != fallingOnly, true);
    Check("and a corpse that is also flagged flying says both",
          deadAndFlying == uint16_t(OverseerDecisions::FALL_GUARD_DEAD |
                                    OverseerDecisions::FALL_GUARD_FLYING), true);
    CheckText("named", FallGuardStandDownNames(deadAndFlying), "dead|flying");
}

// EVERY STAND-DOWN AGREES WITH THE RULE IT DESCRIBES. A non-zero mask must mean
// TerrainRecoveryMayInspect said no, and a zero mask must mean it said yes,
// over all 256 combinations rather than the handful spelled out above.
void TheMaskAndTheRuleNeverDisagree()
{
    for (int i = 0; i < 256; ++i)
    {
        bool const alive = (i & 1) == 0;   // bit 0 set means DEAD
        bool const teleporting = (i & 2) != 0;
        bool const inFlight = (i & 4) != 0;
        bool const flying = (i & 8) != 0;
        bool const falling = (i & 16) != 0;
        bool const inWater = (i & 32) != 0;
        bool const transport = (i & 64) != 0;
        bool const vehicle = (i & 128) != 0;

        uint16_t const mask = FallGuardStandDownMask(alive, teleporting, inFlight, flying,
                                                     falling, inWater, transport, vehicle);
        bool const mayInspect = TerrainRecoveryMayInspect(alive, teleporting, inFlight, flying,
                                                          falling, inWater, transport, vehicle);
        if (mayInspect != (mask == OverseerDecisions::FALL_GUARD_RAN))
        {
            std::printf("FAIL combination %d: mayInspect=%s but mask=0x%02X\n", i,
                        mayInspect ? "true" : "false", mask);
            ++failures;
            return;
        }
        // The mask is exactly the inputs, so it round trips: i is the same
        // eight bits in the same order.
        if (mask != uint16_t(i))
        {
            std::printf("FAIL combination %d: mask 0x%02X is not the inputs\n", i, mask);
            ++failures;
            return;
        }
    }
}

// A reader at three in the morning gets words, and the column keeps the number.
void TheNamesAreReadableAndTotal()
{
    CheckText("nothing", FallGuardStandDownNames(0), "ran");
    CheckText("one", FallGuardStandDownNames(OverseerDecisions::FALL_GUARD_IN_WATER),
              "in-water");
    CheckText("all eight",
              FallGuardStandDownNames(0x00FF),
              "dead|teleporting|in-flight|flying|falling|in-water|transport|vehicle");
    CheckText("a bit this build does not know is not silently 'ran'",
              FallGuardStandDownNames(uint16_t(1u << 12)), "unknown");
}

}  // namespace

int main()
{
    AnOrdinaryCharacterReadsAsRan();
    EachStandDownHasItsOwnBit();
    TwoReasonsAtOnceAreBothReported();
    TheStateUnderSuspicionIsDistinguishable();
    TheMaskAndTheRuleNeverDisagree();
    TheNamesAreReadableAndTotal();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a guard that did not run says which input declined it\n");
    return EXIT_SUCCESS;
}
