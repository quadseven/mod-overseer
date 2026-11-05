/*
 * Which liquid a self-attributed death happened in (#1158).
 *
 * THE FAULT. #503 taught the death summary to read the fall guard's mask, and
 * FALL_GUARD_IN_WATER is Unit::IsInWater. That is true in ANY liquid: the
 * core's swimming status is set for water, ocean, magma and slime alike. So a
 * character that burned to death in lava read "it was in water and did not
 * fall, which reads as drowning". The live check of #503 found such lines for
 * zone 46 (Burning Steppes) and zone 25 (Blackrock Mountain), where the liquid
 * at those heights is lava, and with `seconds_since_full_health` under 15 s,
 * far too fast for breath to run out.
 *
 * THE FIXTURES.
 *
 *   zone 46 row  map 0, Burning Steppes, z 135.3 (the zone's mean death height
 *                in the #503 live check), killer self, mask 32 (in-water),
 *                fall_guard_seconds 0, yards_fallen 0. The liquid there reads
 *                MAP_LIQUID_TYPE_MAGMA. It must not read as drowning.
 *   water        the Un'Goro rows #503 pinned (4456, mask 32, z -278.116). They
 *                must still read as drowning.
 *   slime        the same row in slime. It must not read as drowning either.
 *
 * And the two absences that must never be mistaken for water: a liquid that
 * was never read (the row predates the column, or no adapter filled it) and a
 * read that found no liquid at all while the guard said in-water.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using OverseerDecisions::AccountForFall;
using OverseerDecisions::ClassifyDeathLiquid;
using OverseerDecisions::DeathLiquid;
using OverseerDecisions::DeathLiquidName;
using OverseerDecisions::ReadSelfDeath;
using OverseerDecisions::SelfDeathReading;
using OverseerDecisions::SelfDeathReadingFacts;
using OverseerDecisions::SelfDeathReadingLimits;
using OverseerDecisions::SelfDeathReadingName;

namespace
{

int failures = 0;

SelfDeathReadingLimits const LIVE_READING{2};

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// A self-attributed death in the guard's in-water state with no drop, as the
// zone 46 and Un'Goro rows were, in whichever liquid the core reported.
SelfDeathReadingFacts InLiquid(float z, DeathLiquid liquid)
{
    SelfDeathReadingFacts facts;
    facts.fallGuardMask = OverseerDecisions::FALL_GUARD_IN_WATER;
    facts.fallGuardSeconds = 0;
    facts.selfAttributed = true;
    facts.fall = AccountForFall(0.f, 0.f, 1.f, z, OverseerDecisions::VOID_PLANE_Z);
    facts.liquid = liquid;
    return facts;
}

// The core's own type bits, one at a time and together, read through the same
// classifier the adapter calls on LiquidData::Flags.
void TheCoresTypeBitsNameTheLiquid()
{
    Check("no bits is no liquid",
          ClassifyDeathLiquid(0) == DeathLiquid::None);
    Check("water bit is water",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_WATER) == DeathLiquid::Water);
    Check("ocean bit is water",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_OCEAN) == DeathLiquid::Water);
    Check("dark water bit is water",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_DARK_WATER) == DeathLiquid::Water);
    Check("magma bit is magma",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_MAGMA) == DeathLiquid::Magma);
    Check("slime bit is slime",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_SLIME) == DeathLiquid::Slime);
    // A mask carrying magma beside another bit is magma: lava is what burns,
    // and calling it water is the mistake this exists to end.
    Check("magma with water is magma",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_MAGMA |
                              OverseerDecisions::DEATH_LIQUID_WATER) == DeathLiquid::Magma);
    Check("slime with water is slime",
          ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_SLIME |
                              OverseerDecisions::DEATH_LIQUID_OCEAN) == DeathLiquid::Slime);
    // A bit the core does not define is not a liquid this module can name.
    Check("an unknown bit alone is no liquid", ClassifyDeathLiquid(0x40) == DeathLiquid::None);
}

// The column's values, exactly as the migration's comment lists them. Unread
// has no value: it is written as NULL.
void TheColumnValuesAreTheMigrationsWords()
{
    Check("water", std::strcmp(DeathLiquidName(DeathLiquid::Water), "water") == 0);
    Check("magma", std::strcmp(DeathLiquidName(DeathLiquid::Magma), "magma") == 0);
    Check("slime", std::strcmp(DeathLiquidName(DeathLiquid::Slime), "slime") == 0);
    Check("none", std::strcmp(DeathLiquidName(DeathLiquid::None), "none") == 0);
    Check("unread is NULL", DeathLiquidName(DeathLiquid::Unread) == nullptr);
}

// THE ZONE 46 ROW. The fault this ticket is about.
void TheBurningSteppesRowIsMagmaNotDrowning()
{
    SelfDeathReading const reading = ReadSelfDeath(
        InLiquid(135.3f, ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_MAGMA)),
        LIVE_READING);
    Check("zone 46 row is not drowning", reading != SelfDeathReading::Drowning);
    Check("zone 46 row reads as magma", reading == SelfDeathReading::Magma);
    Check("the magma clause never says drowning",
          std::strstr(SelfDeathReadingName(reading), "drowning") == nullptr);
    Check("the magma clause names magma",
          std::strstr(SelfDeathReadingName(reading), "magma") != nullptr);
}

// THE UN'GORO ROW, which is real water and must still say so.
void TheUnGoroRowIsStillDrowning()
{
    SelfDeathReading const reading = ReadSelfDeath(
        InLiquid(-278.116f, ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_WATER)),
        LIVE_READING);
    Check("Un'Goro row is drowning", reading == SelfDeathReading::Drowning);
    Check("the water clause says drowning",
          std::strstr(SelfDeathReadingName(reading), "drowning") != nullptr);
}

void SlimeIsNotDrowning()
{
    SelfDeathReading const reading = ReadSelfDeath(
        InLiquid(-278.116f, ClassifyDeathLiquid(OverseerDecisions::DEATH_LIQUID_SLIME)),
        LIVE_READING);
    Check("slime row reads as slime", reading == SelfDeathReading::Slime);
    Check("the slime clause never says drowning",
          std::strstr(SelfDeathReadingName(reading), "drowning") == nullptr);
}

// THE TWO ABSENCES. Neither is water, so neither reads as drowning.
void NeitherAbsenceIsWater()
{
    SelfDeathReading const unread =
        ReadSelfDeath(InLiquid(-278.116f, DeathLiquid::Unread), LIVE_READING);
    Check("an unread liquid is not drowning", unread != SelfDeathReading::Drowning);
    Check("an unread liquid says the liquid was not read",
          unread == SelfDeathReading::LiquidUnread);
    Check("the unread clause never says drowning",
          std::strstr(SelfDeathReadingName(unread), "drowning") == nullptr);

    // The guard said in-water and the core's liquid read at the death point
    // found none: the two instruments disagree, so the reader says nothing.
    Check("no liquid at the point is no opinion",
          ReadSelfDeath(InLiquid(-278.116f, DeathLiquid::None), LIVE_READING) ==
              SelfDeathReading::NoOpinion);
}

// The liquid never overrides the order the reader already had: a lethal drop
// is a fall, a creature kill is a creature kill, and a missing mask is
// unsampled, whatever the liquid was.
void TheLiquidDoesNotReorderTheReader()
{
    SelfDeathReadingFacts drop = InLiquid(-20.f, DeathLiquid::Magma);
    drop.fall = AccountForFall(140.f, 0.f, 1.f, -20.f, OverseerDecisions::VOID_PLANE_Z);
    Check("a lethal drop into magma is a fall",
          ReadSelfDeath(drop, LIVE_READING) == SelfDeathReading::Fall);

    SelfDeathReadingFacts creature = InLiquid(135.3f, DeathLiquid::Magma);
    creature.selfAttributed = false;
    Check("a creature kill in magma is no opinion",
          ReadSelfDeath(creature, LIVE_READING) == SelfDeathReading::NoOpinion);

    SelfDeathReadingFacts dry = InLiquid(135.3f, DeathLiquid::Magma);
    dry.fallGuardMask = 0;
    Check("magma under the point without the in-water bit is no opinion",
          ReadSelfDeath(dry, LIVE_READING) == SelfDeathReading::NoOpinion);

    SelfDeathReadingFacts const unset{};
    Check("default facts are unsampled",
          ReadSelfDeath(unset, LIVE_READING) == SelfDeathReading::Unsampled);
    Check("default liquid is unread", unset.liquid == DeathLiquid::Unread);
}

}  // namespace

int main()
{
    TheCoresTypeBitsNameTheLiquid();
    TheColumnValuesAreTheMigrationsWords();
    TheBurningSteppesRowIsMagmaNotDrowning();
    TheUnGoroRowIsStillDrowning();
    SlimeIsNotDrowning();
    NeitherAbsenceIsWater();
    TheLiquidDoesNotReorderTheReader();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a death in magma or slime is not reported as drowning\n");
    return EXIT_SUCCESS;
}
