/*
 * A danger memory built from `overseer_death` rows (#151).
 *
 * `overseer_death` holds one row per death with the killer, the zone and the
 * character's level; this scores each zone and each creature entry from the
 * deaths in a window, with a score that decays as the party levels, so travel
 * routing can avoid lethal zones and creature pulls can skip entries that have
 * recently killed a party member. Zones 46 and 51 are produced by the rule from
 * the death data, not read from any config.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, like every
 * other test here, so the rule stays a pure decision.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <vector>

using OverseerDecisions::AvoidDangerousZone;
using OverseerDecisions::CreatureDanger;
using OverseerDecisions::CreaturePullIsSuppressed;
using OverseerDecisions::DeathRecord;
using OverseerDecisions::RememberSuppressedEntries;
using OverseerDecisions::ScoreCreatureDanger;
using OverseerDecisions::ScoreZoneDanger;
using OverseerDecisions::TravelAvoidance;
using OverseerDecisions::TravelLeg;
using OverseerDecisions::ZoneDanger;
using OverseerDecisions::ZoneIsTooDangerous;

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

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckUInt(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

void CheckDouble(char const* what, double got, double want, double tolerance)
{
    if (std::fabs(got - want) <= tolerance)
        return;
    std::printf("FAIL %s: got %f, wanted %f (tolerance %f)\n", what, got, want, tolerance);
    ++failures;
}

std::time_t const NOW = 1000000;

// `secondsAgo` seconds before NOW, for readability.
std::time_t At(std::time_t secondsAgo)
{
    return NOW - secondsAgo;
}

bool ZoneScoreIsAboveForTest(std::vector<ZoneDanger> const& danger, uint32_t zone,
                             unsigned threshold)
{
    for (ZoneDanger const& z : danger)
        if (z.zone == zone)
            return ZoneIsTooDangerous(z, threshold);
    return false;
}

DeathRecord D(uint32_t level, uint32_t zone, char const* killerType, uint32_t killerEntry,
              uint32_t killerLevel, std::time_t createdAt)
{
    DeathRecord d;
    d.level = level;
    d.zone = zone;
    d.killerType = killerType;
    d.killerEntry = killerEntry;
    d.killerLevel = killerLevel;
    d.createdAt = createdAt;
    return d;
}

// A creature `levelGap` or more above the party, in the window, is a lethal
// death; anything else is not.

// AC1: after two deaths to the same creature entry within an hour, no member
// pulls that entry voluntarily. One death is not enough.
void TwoDeathsToAnEntryWithinAnHourSuppressThePull()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 10, "creature", 12345, 25, At(600)),   // ten minutes ago
        D(17, 10, "creature", 12345, 25, At(1200)),  // twenty minutes ago
    };
    std::vector<CreatureDanger> const scored =
        ScoreCreatureDanger(deaths, NOW, 3600);  // one hour window

    CheckInt("the entry is scored with both deaths", scored.size(), 1);
    CheckUInt("two deaths to the entry", scored[0].deaths, 2);
    Check("the pull is suppressed at threshold two",
          CreaturePullIsSuppressed(scored[0], 2), true);
    Check("one death is not enough", CreaturePullIsSuppressed(CreatureDanger{12345, 1}, 2),
          false);
}

// AC1 continued: the window is one hour, so two deaths an hour apart do not
// suppress the pull.
void TwoDeathsOutsideTheWindowDoNotSuppressThePull()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 10, "creature", 12345, 25, At(4000)),  // over an hour ago
        D(17, 10, "creature", 12345, 25, At(100)),   // just now
    };
    std::vector<CreatureDanger> const scored =
        ScoreCreatureDanger(deaths, NOW, 3600);  // one hour window

    CheckInt("only the recent death is scored", scored.size(), 1);
    CheckUInt("one death in the window", scored[0].deaths, 1);
    Check("the pull is not suppressed outside the window",
          CreaturePullIsSuppressed(scored[0], 2), false);
}

// AC1 "for the rest of the session": once two deaths within the hour suppress
// an entry, it stays suppressed after the hour has rolled the deaths out of
// the scoring window.
void ASuppressedEntryStaysSuppressedForTheSession()
{
    std::set<uint32_t> session;
    RememberSuppressedEntries(session, {CreatureDanger{12345, 2}, CreatureDanger{777, 1}}, 2);
    Check("the twice-deadly entry is remembered", session.count(12345) == 1, true);
    Check("a once-deadly entry is not", session.count(777) == 0, true);

    // An hour later the window holds nothing for the entry.
    RememberSuppressedEntries(session, {}, 2);
    Check("the entry is still suppressed an hour later", session.count(12345) == 1, true);
}

// An environmental death and a player kill never count as a creature entry.
void NonCreatureDeathsAreNotScored()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 10, "environment", 0, 0, At(100)),
        D(17, 10, "player", 0, 0, At(100)),
        D(17, 10, "creature", 0, 0, At(100)),  // creature type but no entry
    };
    std::vector<CreatureDanger> const scored =
        ScoreCreatureDanger(deaths, NOW, 3600);

    Check("nothing is scored", scored.empty(), true);
}

// AC2: a zone with three or more deaths in 24 hours to creatures eight or more
// levels above the party is routed around.
void ThreeLethalDeathsInADayRouteTheZoneAround()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 25, At(1000)),   // party 17, creature 25, gap 8
        D(17, 46, "creature", 101, 25, At(2000)),
        D(17, 46, "creature", 102, 25, At(3000)),
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 17, 86400, 8);  // 24 hour window, gap 8

    CheckInt("zone 46 is scored", scored.size(), 1);
    CheckUInt("three lethal deaths", scored[0].lethalDeaths, 3);
    CheckDouble("score counts three", scored[0].score, 3.0, 0.001);
    Check("zone 46 is too dangerous at threshold three",
          ZoneIsTooDangerous(scored[0], 3), true);
}

// Two lethal deaths is below the threshold of three.
void TwoLethalDeathsAreNotAboveThreshold()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 25, At(1000)),
        D(17, 46, "creature", 101, 25, At(2000)),
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 17, 86400, 8);

    CheckUInt("two lethal deaths", scored[0].lethalDeaths, 2);
    Check("zone 46 is not too dangerous at threshold three",
          ZoneIsTooDangerous(scored[0], 3), false);
}

// A creature only five levels above the party is not lethal at gap eight.
void ACreatureFiveLevelsAboveIsNotLethal()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 22, At(1000)),  // gap 5, below 8
        D(17, 46, "creature", 101, 22, At(2000)),
        D(17, 46, "creature", 102, 22, At(3000)),
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 17, 86400, 8);

    CheckUInt("no lethal deaths at gap eight", scored[0].lethalDeaths, 0);
    Check("the zone is not too dangerous", ZoneIsTooDangerous(scored[0], 3), false);
}

// Deaths older than the window do not count.
void DeathsOlderThanTheWindowAreNotCounted()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 25, At(100000)),  // over a day ago
        D(17, 46, "creature", 101, 25, At(1000)),    // inside the day
        D(17, 46, "creature", 102, 25, At(2000)),
        D(17, 46, "creature", 103, 25, At(3000)),
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 17, 86400, 8);

    CheckUInt("only the three in-window deaths are lethal", scored[0].lethalDeaths, 3);
    Check("three in-window deaths still route the zone around",
          ZoneIsTooDangerous(scored[0], 3), true);
}

// AC3: zones 46 and 51 are produced by the rule from death data, not read from
// config. Both score above threshold from the same death stream.
void Zones46And51AreProducedFromTheDeathData()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 25, At(1000)),
        D(17, 46, "creature", 101, 25, At(2000)),
        D(17, 46, "creature", 102, 25, At(3000)),
        D(17, 51, "creature", 200, 25, At(1000)),
        D(17, 51, "creature", 201, 25, At(2000)),
        D(17, 51, "creature", 202, 25, At(3000)),
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 17, 86400, 8);

    CheckInt("both zones are produced by the rule", scored.size(), 2);
    bool found46 = false;
    bool found51 = false;
    for (ZoneDanger const& z : scored)
    {
        if (z.zone == 46)
        {
            found46 = true;
            CheckUInt("zone 46 has three lethal deaths", z.lethalDeaths, 3);
            Check("zone 46 is too dangerous", ZoneIsTooDangerous(z, 3), true);
        }
        if (z.zone == 51)
        {
            found51 = true;
            CheckUInt("zone 51 has three lethal deaths", z.lethalDeaths, 3);
            Check("zone 51 is too dangerous", ZoneIsTooDangerous(z, 3), true);
        }
    }
    Check("zone 46 appears", found46, true);
    Check("zone 51 appears", found51, true);
}

// The score decays as the party levels: a death to a level 17 character scored
// against a level 30 party contributes only a fraction.
void TheScoreDecaysAsThePartyLevels()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 38, At(1000)),  // party now 30, creature 38, gap 8
    };
    std::vector<ZoneDanger> const scored =
        ScoreZoneDanger(deaths, NOW, 30, 86400, 8);  // party has levelled to 30

    CheckUInt("still one lethal death", scored[0].lethalDeaths, 1);
    CheckDouble("the score is decayed from 1.0 to 1/14", scored[0].score, 1.0 / 14.0, 1e-6);
    Check("a decayed single death is below the threshold of three",
          ZoneIsTooDangerous(scored[0], 3), false);
}

// A zone with no deaths is not listed and is not dangerous.
void AQuietZoneIsNotListed()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 46, "creature", 100, 25, At(1000)),
    };
    std::vector<ZoneDanger> const scored = ScoreZoneDanger(deaths, NOW, 17, 86400, 8);

    CheckInt("only the death's zone is listed", scored.size(), 1);
    Check("zone 10 is not listed and not dangerous", ZoneScoreIsAboveForTest(scored, 10, 3),
          false);
}

// Travel: a dangerous aimed zone with a safe alternative is avoided and named.
void ADangerousAimedZoneIsAvoidedWhenAnAlternativeExists()
{
    std::vector<TravelLeg> const legs = {
        {46, true},   // the aimed leg, dangerous
        {10, false},  // a safe alternative to the same destination
    };
    std::vector<ZoneDanger> const danger = {
        {46, 3, 3, 3.0},
    };
    TravelAvoidance const verdict =
        AvoidDangerousZone(legs, danger, 3);

    Check("the aimed zone is avoided", verdict.avoid, true);
    CheckUInt("the avoided zone is 46", verdict.avoidedZone, 46);
    Check("a safe alternative exists", verdict.noAlternative, false);
}

// Travel: when every route enters a too-dangerous zone there is no alternative.
void NoSafeRouteMeansNoAlternative()
{
    std::vector<TravelLeg> const legs = {
        {46, true},  // the aimed leg, dangerous
        {51, false}, // the only alternative, also dangerous
    };
    std::vector<ZoneDanger> const danger = {
        {46, 3, 3, 3.0},
        {51, 3, 3, 3.0},
    };
    TravelAvoidance const verdict =
        AvoidDangerousZone(legs, danger, 3);

    Check("nothing is avoided when there is no safe route", verdict.avoid, false);
    CheckUInt("the aimed zone is named", verdict.avoidedZone, 46);
    Check("there is no alternative", verdict.noAlternative, true);
}

// Travel: a safe aimed zone is not avoided.
void ASafeAimedZoneIsNotAvoided()
{
    std::vector<TravelLeg> const legs = {
        {10, true},   // the aimed leg, safe
        {46, false},  // a dangerous alternative
    };
    std::vector<ZoneDanger> const danger = {
        {46, 3, 3, 3.0},
    };
    TravelAvoidance const verdict =
        AvoidDangerousZone(legs, danger, 3);

    Check("the safe aimed zone is not avoided", verdict.avoid, false);
    CheckUInt("no zone is avoided", verdict.avoidedZone, 0);
    Check("there is no no-alternative state", verdict.noAlternative, false);
}

}  // namespace

int main()
{
    TwoDeathsToAnEntryWithinAnHourSuppressThePull();
    TwoDeathsOutsideTheWindowDoNotSuppressThePull();
    ASuppressedEntryStaysSuppressedForTheSession();
    NonCreatureDeathsAreNotScored();
    ThreeLethalDeathsInADayRouteTheZoneAround();
    TwoLethalDeathsAreNotAboveThreshold();
    ACreatureFiveLevelsAboveIsNotLethal();
    DeathsOlderThanTheWindowAreNotCounted();
    Zones46And51AreProducedFromTheDeathData();
    TheScoreDecaysAsThePartyLevels();
    AQuietZoneIsNotListed();
    ADangerousAimedZoneIsAvoidedWhenAnAlternativeExists();
    NoSafeRouteMeansNoAlternative();
    ASafeAimedZoneIsNotAvoided();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_death_danger_memory: all passed\n");
    return 0;
}
