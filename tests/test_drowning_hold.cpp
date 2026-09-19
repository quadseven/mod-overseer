/*
 * Water, on three axes: an aim that must not be written into a lake, a hold
 * that must not be taken in one, and a death row that has been carrying the
 * answer since it was added.
 *
 * The live failure this pins was measured on the dev realm and the rows are
 * still on disk. On 2026-09-19 the family leader carried a stuck `auctioneer`
 * errand and stood at (-6617.46, -1473.14, -278.806) in Un'Goro Crater. From
 * 14:33:34 to 14:44:01, eighteen times, five seconds apart, the errand drive
 * printed that it was being "held on the ground instead", released the errand,
 * and got it straight back. It was under water the whole time. It drowned at
 * 14:42:24 with a second character one yard away, and the loop was still
 * printing the same line after the death and after the revival.
 *
 *   id   who   pos                            killer  hp/max      fell  mask
 *   4456 Bork  (-6618.28, -1473.16, -278.116) self     929/2643    0.0    32
 *   4457 Grug  (-6617.46, -1473.14, -278.806) self    2324/4089    0.0    48
 *
 * 32 is FALL_GUARD_IN_WATER and 48 is that plus FALL_GUARD_FALLING. Both rows
 * carry `fall_guard_seconds` 0, so the guard looked in the second they died.
 * Six more characters died the same way at one spot on 2026-09-18 inside 95
 * seconds. Across the retained table since 2026-09-17 every non-zero
 * fall-guard mask is a self-attributed death: five carry 32, two carry 48 and
 * four carry 528.
 *
 * The follower still behind was aimed at the leader's own position, because
 * that is what a catch-up aim is, so it was routed 3,897 yards toward the same
 * lake.
 *
 * WHAT IS NOT HERE, SO NOBODY READS THIS FILE AS THE WHOLE GATE. The same edit
 * also stops that catch-up aim carrying the leader's unvalidated z (#188): the
 * aim's height is now the measured ground and a point whose ground cannot be
 * read at all is refused. That half is terrain code - it needs a Map and a
 * navmesh - and the only rule under it is ShoreStandable, which is tested
 * below. "The aim's z is the measured surface" has no arithmetic to pin, so
 * there is deliberately no test of it here rather than a test that asserts the
 * code calls the function it calls.
 *
 * THE TESTS THAT MATTER MOST ARE THE NEGATIVE ONES, and they are marked. A
 * rule that released every errand of every character in water would end every
 * water crossing this roster makes; a reader that called every self-attributed
 * death a drowning would be manufacturing the confusion it exists to end; and
 * a hold guard that stopped holding characters stuck on DRY land would undo
 * #498 and #500. Each of those is asserted against here, and each of them is a
 * thing this change could plausibly have done.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::AccountForFall;
using OverseerDecisions::DrowningHoldAction;
using OverseerDecisions::DrowningHoldDecision;
using OverseerDecisions::DrowningHoldFacts;
using OverseerDecisions::DrowningHoldLimits;
using OverseerDecisions::FallAccount;
using OverseerDecisions::ReadSelfDeath;
using OverseerDecisions::SelfDeathReading;
using OverseerDecisions::SelfDeathReadingFacts;
using OverseerDecisions::SelfDeathReadingLimits;
using OverseerDecisions::ShoreProbe;
using OverseerDecisions::ShoreProbeAt;
using OverseerDecisions::ShoreProbeCount;
using OverseerDecisions::ShoreSearchLimits;
using OverseerDecisions::ShoreStandable;
using OverseerDecisions::TravelStuckAction;
using OverseerDecisions::TravelStuckDecision;

namespace
{

int failures = 0;

// The live constants, so this file fails when they move rather than when
// somebody remembers to come back and edit it.
DrowningHoldLimits const LIVE_HOLD{60};
ShoreSearchLimits const LIVE_SHORE{20.f, 20.f, 4u, 8u};
SelfDeathReadingLimits const LIVE_READING{2};

// A human's collision height at the pinned revision. The rule takes it as an
// argument rather than assuming it, because the core compares against the
// character's own.
constexpr float HUMAN_COLLISION_HEIGHT = 2.03f;

char const* Name(DrowningHoldAction action)
{
    switch (action)
    {
        case DrowningHoldAction::HoldAsBefore:   return "hold as before";
        case DrowningHoldAction::ReleaseInWater: return "release in water";
    }
    return "unknown";
}

char const* Name(SelfDeathReading reading)
{
    switch (reading)
    {
        case SelfDeathReading::Unsampled: return "unsampled";
        case SelfDeathReading::NoOpinion: return "no opinion";
        case SelfDeathReading::Drowning:  return "drowning";
        case SelfDeathReading::Fall:      return "fall";
    }
    return "unknown";
}

void CheckHold(char const* what, DrowningHoldAction got, DrowningHoldAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckReading(char const* what, SelfDeathReading got, SelfDeathReading want)
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

void CheckUnsigned(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

DrowningHoldFacts Standing(bool inWater, long long stillSeconds)
{
    DrowningHoldFacts facts;
    facts.inWater = inWater;
    facts.stillInWaterSeconds = stillSeconds;
    return facts;
}

// ------------------------------------------------------------- the hold ----

// THE INCIDENT. 191 seconds in one spot under water, on an errand this module
// was holding. The core gives three minutes of breath and then kills; the
// release fires at sixty, with two full minutes to spare.
void TheLeaderThatDrownedWhileBeingHeldOnTheGround()
{
    CheckHold("standing still in water for 191s",
              DrowningHoldDecision(Standing(true, 191), LIVE_HOLD),
              DrowningHoldAction::ReleaseInWater);
    CheckHold("the poll it crosses the limit",
              DrowningHoldDecision(Standing(true, 60), LIVE_HOLD),
              DrowningHoldAction::ReleaseInWater);
    CheckHold("one second short of it",
              DrowningHoldDecision(Standing(true, 59), LIVE_HOLD),
              DrowningHoldAction::HoldAsBefore);
}

// THE NEGATIVE THAT MATTERS MOST, AND THE WHOLE REASON THE CLOCK EXISTS. A
// character SWIMMING is in water and is getting somewhere. Swim speed is 4.72
// yards a second and the clock restarts every five yards, so a real crossing
// never accumulates more than about a second of this. If "in water" alone
// released an errand, every one of these would release and every water
// crossing this roster makes would end on its first stroke.
void ASwimIsNotADrowning()
{
    for (long long seconds = -1; seconds <= 2; ++seconds)
        CheckHold("in water, clock barely started",
                  DrowningHoldDecision(Standing(true, seconds), LIVE_HOLD),
                  DrowningHoldAction::HoldAsBefore);
}

// AND THE OTHER NEGATIVE, WHICH IS #498 AND #500 STAYING FIXED. A character
// genuinely stuck on DRY land gets the behaviour it had before this change,
// for every clock value there is - including nonsense ones, because a clock
// is only meaningful while `inWater` is true and a rule that read it anyway
// would release characters standing in a field.
void DryLandIsUntouchedWhateverElseIsTrue()
{
    long long const clocks[] = {-1, 0, 1, 59, 60, 61, 191, 100000};
    for (long long seconds : clocks)
        CheckHold("not in water",
                  DrowningHoldDecision(Standing(false, seconds), LIVE_HOLD),
                  DrowningHoldAction::HoldAsBefore);
}

// AND THE GUARD IT SITS IN FRONT OF IS BYTE FOR BYTE WHAT IT WAS. The stuck
// counter rule is the one that decides for every character on dry land, and
// the point of adding a separate decision rather than a fourth argument to
// that one was that this property could be asserted rather than promised.
// TravelStuckDecision's own file tests it in full; this is the seam.
void TheStuckCounterRuleIsUnchanged()
{
    for (uint32_t attempts = 0; attempts < 12; ++attempts)
    {
        TravelStuckAction const want = attempts >= 5 ? TravelStuckAction::Release
                                                     : TravelStuckAction::Continue;
        if (TravelStuckDecision(attempts, 5, true) != want)
        {
            std::printf("FAIL the stuck counter rule moved at attempts=%u\n", attempts);
            ++failures;
        }
        if (TravelStuckDecision(attempts, 5, false) != TravelStuckAction::Continue)
        {
            std::printf("FAIL an unwritten counter decided something at attempts=%u\n",
                        attempts);
            ++failures;
        }
    }
}

// ZERO DISABLES, the same convention every other bound in the header keeps. A
// caller that has not configured this gets the old behaviour rather than a
// release on the first poll, which is what a limit of zero would mean read as
// a threshold.
void ALimitOfZeroAsksNothing()
{
    DrowningHoldLimits const off{0};
    for (long long seconds = 0; seconds < 400; seconds += 37)
        CheckHold("limit disabled",
                  DrowningHoldDecision(Standing(true, seconds), off),
                  DrowningHoldAction::HoldAsBefore);
}

// NOTHING IS EVER RELEASED OUT OF WATER, asserted as a property over the whole
// input space rather than read out of the cases above. This is the one line
// that would catch a future edit moving the water test below something else.
void NoCharacterOnDryGroundIsEverReleased()
{
    for (int limit = 0; limit < 120; limit += 13)
        for (long long seconds = -2; seconds < 300; seconds += 17)
        {
            DrowningHoldLimits const limits{limit};
            if (DrowningHoldDecision(Standing(false, seconds), limits) !=
                DrowningHoldAction::HoldAsBefore)
            {
                std::printf("FAIL released a character on dry land "
                            "(limit=%d still=%lld)\n", limit, seconds);
                ++failures;
            }
        }
}

// ------------------------------------------------------------ the shore ----

// THE CORE'S OWN COMPARISON. Map::GetLiquidData calls a point UNDER_WATER when
// `waterLevel - z > collisionHeight`, so the boundary here is the character's
// own height and not a number anybody chose. Asserted on both sides of it.
void StandableIsTheCoresOwnThreshold()
{
    CheckBool("dry ground", ShoreStandable(0.f, HUMAN_COLLISION_HEIGHT), true);
    CheckBool("ankle deep", ShoreStandable(0.5f, HUMAN_COLLISION_HEIGHT), true);
    CheckBool("exactly at the collision height",
              ShoreStandable(HUMAN_COLLISION_HEIGHT, HUMAN_COLLISION_HEIGHT), true);
    CheckBool("one inch over it",
              ShoreStandable(HUMAN_COLLISION_HEIGHT + 0.01f, HUMAN_COLLISION_HEIGHT),
              false);
    CheckBool("a lake", ShoreStandable(15.f, HUMAN_COLLISION_HEIGHT), false);
}

// A NEGATIVE DEPTH IS DRY GROUND ABOVE A WATER LINE, not a reading to reject.
// GetWaterOrGroundLevel returns max(level, ground) where the ground stands
// above the water, so the subtraction the caller does can legitimately come
// back at or below zero and it means the driest thing there is.
void GroundAboveAWaterLineIsTheDriestAnswerThereIs()
{
    CheckBool("a bank above the water line",
              ShoreStandable(-4.f, HUMAN_COLLISION_HEIGHT), true);
}

// ZERO DISABLES here too, and it is reached by a caller that could not read a
// collision height rather than by one that wants everything drown-proofed.
void AWadeDepthOfZeroAsksNothing()
{
    CheckBool("not asking, over a lake", ShoreStandable(40.f, 0.f), true);
    CheckBool("not asking, negative", ShoreStandable(40.f, -1.f), true);
}

// THE SEARCH IS BOUNDED AND THE BOUND IS ONE NUMBER. Every candidate is a
// terrain query on the world thread, so the count is the thing a reviewer
// checks first.
void TheSearchIsBounded()
{
    CheckUnsigned("four rings of eight", ShoreProbeCount(LIVE_SHORE), 32u);
    CheckUnsigned("no rings is no search",
                  ShoreProbeCount(ShoreSearchLimits{20.f, 20.f, 0u, 8u}), 0u);
    CheckUnsigned("no bearings is no search",
                  ShoreProbeCount(ShoreSearchLimits{20.f, 20.f, 4u, 0u}), 0u);

    ShoreProbe probe;
    CheckBool("one past the end", ShoreProbeAt(32u, LIVE_SHORE, probe), false);
    CheckBool("far past the end", ShoreProbeAt(9999u, LIVE_SHORE, probe), false);
    CheckBool("a search with no bearings yields nothing",
              ShoreProbeAt(0u, ShoreSearchLimits{20.f, 20.f, 4u, 0u}, probe), false);
}

// NEAREST RING FIRST, AND EACH RING SWEPT WHOLE. The caller takes the FIRST
// candidate that measures standable, so this ordering is the entire "nearest
// shore" promise. A search that wandered outward and back would hand the party
// a meeting point further away than one it had already walked past.
void TheSearchGoesOutwardsAndNeverBack()
{
    float previousRadius = -1.f;
    unsigned seen = 0;
    for (unsigned i = 0; i < 64; ++i)
    {
        ShoreProbe probe;
        if (!ShoreProbeAt(i, LIVE_SHORE, probe))
            break;
        ++seen;
        if (probe.radiusYards < previousRadius)
        {
            std::printf("FAIL candidate %u stepped back in: %.1f after %.1f\n", i,
                        probe.radiusYards, previousRadius);
            ++failures;
        }
        previousRadius = probe.radiusYards;
        if (probe.turns < 0.f || probe.turns >= 1.f)
        {
            std::printf("FAIL candidate %u has a bearing outside one turn: %.3f\n", i,
                        probe.turns);
            ++failures;
        }
    }
    CheckUnsigned("every candidate was yielded", seen, 32u);
}

// THE RINGS ARE WHERE THE CONSTANTS SAY. Twenty yards out, twenty apart, four
// of them, so the furthest a shore is ever taken from is eighty. Spelled out
// rather than inferred, because the refusal line quotes this arithmetic back
// to the operator and the two must not drift.
void TheRingsAreWhereTheConstantsSay()
{
    float const wanted[] = {20.f, 40.f, 60.f, 80.f};
    for (unsigned ring = 0; ring < 4; ++ring)
    {
        ShoreProbe probe;
        if (!ShoreProbeAt(ring * LIVE_SHORE.bearings, LIVE_SHORE, probe))
        {
            std::printf("FAIL ring %u was not yielded at all\n", ring);
            ++failures;
            continue;
        }
        if (probe.ring != ring || probe.radiusYards != wanted[ring])
        {
            std::printf("FAIL ring %u came back as ring %u at %.1f yards, wanted %.1f\n",
                        ring, probe.ring, probe.radiusYards, wanted[ring]);
            ++failures;
        }
        // The first candidate of every ring is the straight-ahead bearing, so
        // a caller that only ever gets one answer gets a consistent one.
        if (probe.turns != 0.f)
        {
            std::printf("FAIL ring %u did not start at bearing zero: %.3f\n", ring,
                        probe.turns);
            ++failures;
        }
    }
}

// EVERY BEARING IS DISTINCT AND THEY SPAN THE TURN. Eight bearings that
// happened to collide would be a search of four directions dressed up as one
// of eight, and the cost would be paid either way.
void TheBearingsSpanTheTurnWithoutRepeating()
{
    for (unsigned i = 0; i < LIVE_SHORE.bearings; ++i)
    {
        ShoreProbe a;
        if (!ShoreProbeAt(i, LIVE_SHORE, a))
        {
            std::printf("FAIL bearing %u missing\n", i);
            ++failures;
            continue;
        }
        for (unsigned j = i + 1; j < LIVE_SHORE.bearings; ++j)
        {
            ShoreProbe b;
            if (ShoreProbeAt(j, LIVE_SHORE, b) && a.turns == b.turns)
            {
                std::printf("FAIL bearings %u and %u are the same direction (%.3f)\n",
                            i, j, a.turns);
                ++failures;
            }
        }
    }
}

// ------------------------------------------------------- the death row -----

// The account is computed exactly as FlushDeaths computes it, from the row's
// own numbers, so this fixture exercises the same arithmetic the log line does
// rather than a hand-picked verdict.
SelfDeathReadingFacts Row(int32_t mask, int32_t guardSeconds, char const* killer,
                          float yardsFallen, float z)
{
    SelfDeathReadingFacts facts;
    facts.fallGuardMask = mask;
    facts.fallGuardSeconds = guardSeconds;
    facts.selfAttributed = std::string(killer) == "self";
    facts.fall = AccountForFall(yardsFallen, 0.f, 1.f, z,
                                OverseerDecisions::VOID_PLANE_Z);
    return facts;
}

// THE TWO ROWS #503 IS ABOUT. Masks 32 and 48, `fall_guard_seconds` 0,
// `yards_fallen` 0, both self-attributed, one yard apart in the same second.
void TheTwoRowsFromUnGoroCrater()
{
    CheckReading("4456 Bork, mask 32",
                 ReadSelfDeath(Row(32, 0, "self", 0.f, -278.116f), LIVE_READING),
                 SelfDeathReading::Drowning);
    CheckReading("4457 Grug, mask 48 (in-water plus falling)",
                 ReadSelfDeath(Row(48, 0, "self", 0.f, -278.806f), LIVE_READING),
                 SelfDeathReading::Drowning);
}

// THE SIX-ROW CLUSTER OF 2026-09-18, AND IT IS NOT SIX DROWNINGS. Four of the
// six carry a water bit and read as drowning; two carry mask 0, which means
// the guard looked and nothing declined it, and those two say NOTHING. A
// reader that answered "drowning" for all six because the four beside them did
// would be inventing evidence, which is the exact failure #281's comment about
// nothing reading the mask back was protecting against.
void TheSixRowClusterIsFourDrowningsAndTwoSilences()
{
    struct Row6 { int id; int32_t mask; int32_t seconds; SelfDeathReading want; };
    Row6 const rows[] = {
        {4425, 0,  0, SelfDeathReading::NoOpinion},
        {4426, 48, 0, SelfDeathReading::Drowning},
        {4427, 32, 1, SelfDeathReading::Drowning},
        {4428, 32, 1, SelfDeathReading::Drowning},
        {4429, 32, 1, SelfDeathReading::Drowning},
        {4430, 0,  1, SelfDeathReading::NoOpinion},
        {4431, 0,  0, SelfDeathReading::NoOpinion},
    };
    for (Row6 const& row : rows)
    {
        char what[64];
        std::snprintf(what, sizeof(what), "row %d, mask %d", row.id, row.mask);
        CheckReading(what,
                     ReadSelfDeath(Row(row.mask, row.seconds, "self", 0.f, -194.f),
                                   LIVE_READING),
                     row.want);
    }
}

// THE FALLS, WHICH MUST NOT READ AS DROWNINGS. Mask 528 is DESCENDING plus
// FALLING and carries no water bit at all, and these four rows recorded drops
// of 80.4, 45.9, 257.6 and 129.4 yards. Three of them clear the core's own
// lethal distance (69.03 yards at the pinned arithmetic) and read as falls;
// the fourth does not clear it, and the honest answer for that one is nothing
// rather than a guess in either direction.
void TheMask528RowsAreFallsAndOneOfThemIsNotEvenThat()
{
    CheckReading("4343 Bork, 80.4 yards",
                 ReadSelfDeath(Row(528, 1, "self", 80.4f, -78.f), LIVE_READING),
                 SelfDeathReading::Fall);
    CheckReading("4452 Grog, 257.6 yards",
                 ReadSelfDeath(Row(528, 1, "self", 257.6f, 587.f), LIVE_READING),
                 SelfDeathReading::Fall);
    CheckReading("4453 Grog, 129.4 yards",
                 ReadSelfDeath(Row(528, 0, "self", 129.4f, 428.f), LIVE_READING),
                 SelfDeathReading::Fall);
    CheckReading("4344 Grug, 45.9 yards, which could not have killed it",
                 ReadSelfDeath(Row(528, 0, "self", 45.9f, -78.f), LIVE_READING),
                 SelfDeathReading::NoOpinion);
}

// A FALL WINS OVER A WATER BIT WHEN THE ARITHMETIC SAYS IT COULD HAVE KILLED.
// A character can be in water at the bottom of the drop that killed it, and
// the drop is then the answer. This is the ordering inside the rule, asserted
// rather than left to a reading of it.
void ADropThatCouldHaveKilledOutranksTheWaterBit()
{
    CheckReading("in water at the bottom of a lethal drop",
                 ReadSelfDeath(Row(32, 0, "self", 140.f, -20.f), LIVE_READING),
                 SelfDeathReading::Fall);
    CheckReading("below the void plane with a water bit set",
                 ReadSelfDeath(Row(32, 0, "self", 30.f, -900.f), LIVE_READING),
                 SelfDeathReading::Fall);
}

// SOMETHING ELSE KILLED IT, AND THE MASK SAYS NOTHING ABOUT THAT. Three
// creature kills in the retained table carry FALL_GUARD_IN_WATER for no better
// reason than that the fight was in a river. Reading those as drownings would
// be the most expensive kind of wrong: a reader chasing water when a mob is
// what needs looking at.
void ACreatureKillInARiverIsACreatureKill()
{
    CheckReading("killed by a creature while standing in water",
                 ReadSelfDeath(Row(32, 0, "creature", 0.f, -100.f), LIVE_READING),
                 SelfDeathReading::NoOpinion);
    CheckReading("killed by a creature, mask 48",
                 ReadSelfDeath(Row(48, 0, "creature", 0.f, -100.f), LIVE_READING),
                 SelfDeathReading::NoOpinion);
}

// AN ABSENCE IS NOT A READING, which is the rule #281 wrote the -1 sentinel
// for and which this reader has to keep. "The guard never looked" and "the
// guard looked and found nothing" are different findings with different fixes.
void NeverSampledIsItsOwnAnswer()
{
    CheckReading("no mask on the row",
                 ReadSelfDeath(Row(-1, -1, "self", 0.f, -278.f), LIVE_READING),
                 SelfDeathReading::Unsampled);
    CheckReading("a mask with no age",
                 ReadSelfDeath(Row(32, -1, "self", 0.f, -278.f), LIVE_READING),
                 SelfDeathReading::Unsampled);
    CheckReading("the guard ran and declined nothing",
                 ReadSelfDeath(Row(0, 0, "self", 0.f, -278.f), LIVE_READING),
                 SelfDeathReading::NoOpinion);
}

// A STALE READING IS NOT EVIDENCE ABOUT THIS DEATH. The drive polls once a
// second; a mask several seconds old describes a character the drive was not
// reaching, which the column's own comment already calls a different fault.
// Every one of the eleven rows this issue is about carries an age of 0 or 1,
// so this bound excludes none of them - which is the point, and is why it is
// checked at the boundary rather than assumed.
void AMaskTooOldToBeAboutThisDeathSaysNothing()
{
    CheckReading("one second old", ReadSelfDeath(Row(32, 1, "self", 0.f, -278.f),
                                                 LIVE_READING),
                 SelfDeathReading::Drowning);
    CheckReading("two seconds old", ReadSelfDeath(Row(32, 2, "self", 0.f, -278.f),
                                                  LIVE_READING),
                 SelfDeathReading::Drowning);
    CheckReading("three seconds old", ReadSelfDeath(Row(32, 3, "self", 0.f, -278.f),
                                                    LIVE_READING),
                 SelfDeathReading::NoOpinion);
    CheckReading("a minute old", ReadSelfDeath(Row(32, 60, "self", 0.f, -278.f),
                                               LIVE_READING),
                 SelfDeathReading::NoOpinion);
}

// AND NOTHING WITHOUT THE WATER BIT IS EVER CALLED A DROWNING, asserted over
// every mask the guard can produce rather than over the ones that happen to be
// in the table. This is the property that stops a future bit being folded in
// by accident.
void OnlyTheWaterBitEverReadsAsDrowning()
{
    for (int32_t mask = 0; mask <= 0x3FF; ++mask)
    {
        SelfDeathReading const reading =
            ReadSelfDeath(Row(mask, 0, "self", 0.f, -278.f), LIVE_READING);
        bool const hasWater = (mask & OverseerDecisions::FALL_GUARD_IN_WATER) != 0;
        if ((reading == SelfDeathReading::Drowning) != hasWater)
        {
            std::printf("FAIL mask %d read as %s with water bit %s\n", mask,
                        Name(reading), hasWater ? "set" : "clear");
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    TheLeaderThatDrownedWhileBeingHeldOnTheGround();
    ASwimIsNotADrowning();
    DryLandIsUntouchedWhateverElseIsTrue();
    TheStuckCounterRuleIsUnchanged();
    ALimitOfZeroAsksNothing();
    NoCharacterOnDryGroundIsEverReleased();

    StandableIsTheCoresOwnThreshold();
    GroundAboveAWaterLineIsTheDriestAnswerThereIs();
    AWadeDepthOfZeroAsksNothing();
    TheSearchIsBounded();
    TheSearchGoesOutwardsAndNeverBack();
    TheRingsAreWhereTheConstantsSay();
    TheBearingsSpanTheTurnWithoutRepeating();

    TheTwoRowsFromUnGoroCrater();
    TheSixRowClusterIsFourDrowningsAndTwoSilences();
    TheMask528RowsAreFallsAndOneOfThemIsNotEvenThat();
    ADropThatCouldHaveKilledOutranksTheWaterBit();
    ACreatureKillInARiverIsACreatureKill();
    NeverSampledIsItsOwnAnswer();
    AMaskTooOldToBeAboutThisDeathSaysNothing();
    OnlyTheWaterBitEverReadsAsDrowning();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a character is not held on the ground when the ground is water\n");
    return EXIT_SUCCESS;
}
