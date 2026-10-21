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
#include <string>
#include <vector>

using OverseerDecisions::AvoidDangerousRoute;
using OverseerDecisions::AvoidDangerousZone;
using OverseerDecisions::CompareDeathRate;
using OverseerDecisions::CreatureDanger;
using OverseerDecisions::CreaturePullIsSuppressed;
using OverseerDecisions::DangerListText;
using OverseerDecisions::DangerProbeJson;
using OverseerDecisions::DeathRateComparison;
using OverseerDecisions::DeathRecord;
using OverseerDecisions::FirstDangerousZone;
using OverseerDecisions::NotePullSuppressions;
using OverseerDecisions::PullFacts;
using OverseerDecisions::PullSuppressionBook;
using OverseerDecisions::RefuseVoluntaryPull;
using OverseerDecisions::ScoreCreatureDanger;
using OverseerDecisions::ScoreZoneDanger;
using OverseerDecisions::TravelAvoidance;
using OverseerDecisions::TravelLeg;
using OverseerDecisions::ZoneDanger;
using OverseerDecisions::ZoneAvoidanceLine;
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

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    [%s]\n  wanted [%s]\n", what, got.c_str(), want.c_str());
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
    CheckUInt("score counts three", scored[0].score, 3);
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

// The family page shows the danger list: every zone at or above the threshold
// by score, then every creature entry whose pulls are suppressed. A zone or an
// entry below its threshold is not on the list.
void TheDangerListNamesZonesAndCreaturesAboveThreshold()
{
    std::vector<ZoneDanger> const zones = {
        {51, 4, 4, 4.0},
        {46, 3, 3, 3.0},
        {10, 2, 1, 1.0},  // below the threshold of three
    };
    std::vector<CreatureDanger> const creatures = {
        {12345, 2},
        {777, 1},  // one death does not suppress the pull
    };

    CheckText("the danger list for the family page",
              DangerListText(zones, creatures, 3, 2),
              "zone 51: score 4.00 (4 of 4 deaths to creatures far above the party)\n"
              "zone 46: score 3.00 (3 of 3 deaths to creatures far above the party)\n"
              "creature 12345: 2 deaths, not pulled\n");
}

// Nothing above threshold renders as empty, so the page shows its own empty
// state instead of a stale or invented list.
void AQuietDangerListIsEmpty()
{
    std::vector<ZoneDanger> const zones = {{10, 1, 1, 1.0}};
    std::vector<CreatureDanger> const creatures = {{777, 1}};

    CheckText("nothing is dangerous", DangerListText(zones, creatures, 3, 2), "");
}

// AC4: deaths per hour, the week before the change against the time since,
// reported as numbers. The baseline is the seven days before the change; a
// death older than that, or with no timestamp, is in neither window.
void DeathsPerHourAreComparedAgainstAWeekBaseline()
{
    std::time_t const changeAt = NOW - 24 * 3600;  // the change went live a day ago
    std::vector<DeathRecord> deaths;
    for (int i = 0; i < 84; ++i)  // 84 deaths over the week before: 0.5 per hour
        deaths.push_back(D(17, 46, "creature", 100, 25, changeAt - 1 - i * 7200));
    for (int i = 0; i < 6; ++i)  // 6 deaths in the day since: 0.25 per hour
        deaths.push_back(D(17, 10, "environment", 0, 0, changeAt + i * 3600));
    deaths.push_back(D(17, 46, "creature", 100, 25, changeAt - 8 * 86400));  // too old
    deaths.push_back(D(17, 46, "creature", 100, 25, 0));                     // no timestamp

    DeathRateComparison const rate = CompareDeathRate(deaths, changeAt, NOW);

    CheckUInt("baseline deaths", rate.baseline.deaths, 84);
    CheckDouble("baseline hours", rate.baseline.hours, 168.0, 1e-9);
    CheckDouble("baseline per hour", rate.baseline.perHour, 0.5, 1e-9);
    CheckUInt("deaths since the change", rate.since.deaths, 6);
    CheckDouble("hours since the change", rate.since.hours, 24.0, 1e-9);
    CheckDouble("per hour since the change", rate.since.perHour, 0.25, 1e-9);
    CheckText("the rate is reported as numbers", rate.text,
              "deaths per hour: baseline 0.50 (84 in 168.0 h), "
              "since the change 0.25 (6 in 24.0 h)");
}

// Asked at the moment of the change there is no time since it, so its rate is
// zero rather than a division by zero.
void NoTimeSinceTheChangeHasNoRate()
{
    std::vector<DeathRecord> const deaths = {D(17, 46, "creature", 100, 25, NOW)};
    DeathRateComparison const rate = CompareDeathRate(deaths, NOW, NOW);

    CheckUInt("the death at the change counts as since", rate.since.deaths, 1);
    CheckDouble("no hours since", rate.since.hours, 0.0, 1e-9);
    CheckDouble("no rate without hours", rate.since.perHour, 0.0, 1e-9);
}

// AC1, the session half: the creature memory is an hour, the refusal is the
// session. Two deaths within the hour put the entry in the book at the highest
// level those deaths were at; the book is what the pull guard asks.
void TwoDeathsPutTheEntryInTheSessionBook()
{
    std::vector<DeathRecord> const deaths = {
        D(17, 10, "creature", 12345, 25, At(600)),
        D(18, 10, "creature", 12345, 25, At(1200)),
        D(18, 10, "creature", 777, 25, At(1200)),  // one death: not booked
    };
    std::vector<CreatureDanger> const scored = ScoreCreatureDanger(deaths, NOW, 3600);
    CheckUInt("the entry carries the highest level its deaths were at", scored[1].level, 18);

    PullSuppressionBook book;
    std::vector<uint32_t> const added = NotePullSuppressions(book, scored, 2);
    CheckInt("one entry is newly suppressed", added.size(), 1);
    CheckUInt("and it is the twice-lethal entry", added.empty() ? 0 : added[0], 12345);
    CheckInt("the book holds only that entry", book.size(), 1);
    CheckUInt("at level 18", book[12345].level, 18);
    CheckUInt("with its two deaths", book[12345].deaths, 2);

    CheckInt("noting the same deaths again adds nothing",
             NotePullSuppressions(book, scored, 2).size(), 0);
}

// AC1: once in the book, an hour passing does not lift it. The deaths age out
// of the scoring window and the entry stays refused for the rest of the session.
void TheRefusalOutlivesTheHourWindow()
{
    PullSuppressionBook book;
    book[12345] = CreatureDanger{12345, 2, 17};
    std::vector<CreatureDanger> const later =
        ScoreCreatureDanger({D(17, 10, "creature", 12345, 25, At(7200))}, NOW, 3600);
    NotePullSuppressions(book, later, 2);
    CheckInt("the book still holds the entry", book.count(12345), 1);

    PullFacts facts;
    facts.entry = 12345;
    facts.memberLevel = 17;
    facts.targetInCombat = false;
    Check("a member at the same level does not start that fight",
          RefuseVoluntaryPull(book, facts, 3), true);
}

// Fighting back is unchanged: a creature already in combat is not a pull.
void FightingBackIsNotRefused()
{
    PullSuppressionBook book;
    book[12345] = CreatureDanger{12345, 2, 17};
    PullFacts facts;
    facts.entry = 12345;
    facts.memberLevel = 17;
    facts.targetInCombat = true;
    Check("a creature already fighting is fought back",
          RefuseVoluntaryPull(book, facts, 3), false);
}

// The refusal lifts when the member is N levels above the deaths, and an entry
// that is not in the book is never refused.
void TheRefusalLiftsNLevelsHigher()
{
    PullSuppressionBook book;
    book[12345] = CreatureDanger{12345, 2, 17};
    PullFacts facts;
    facts.entry = 12345;
    facts.targetInCombat = false;
    facts.memberLevel = 19;
    Check("two levels higher is still refused", RefuseVoluntaryPull(book, facts, 3), true);
    facts.memberLevel = 20;
    Check("three levels higher may pull again", RefuseVoluntaryPull(book, facts, 3), false);
    facts.memberLevel = 17;
    facts.entry = 999;
    Check("an entry with no deaths is pulled as before",
          RefuseVoluntaryPull(book, facts, 3), false);
}

// AC2: the first zone of a planned route at or above the threshold, in route
// order; 0 when the whole route is safe.
void TheFirstDangerousZoneOnARouteIsFound()
{
    std::vector<ZoneDanger> const danger = {{46, 3, 3, 3.0}, {51, 4, 4, 4.0}};
    CheckUInt("route order, not score order",
              FirstDangerousZone({12, 46, 51}, danger, 3), 46);
    CheckUInt("a safe route has none", FirstDangerousZone({12, 40}, danger, 3), 0);
}

// AC2: the plan crosses zone 46, the re-plan with zone 46 priced out does not,
// so the party walks the re-plan and the log names 46.
void ARouteThroughADangerousZoneIsWalkedAround()
{
    std::vector<ZoneDanger> const danger = {{46, 3, 3, 3.0}};
    TravelAvoidance const verdict = AvoidDangerousRoute({12, 46, 44}, {12, 40, 44}, danger, 3);
    Check("the re-plan is walked", verdict.avoid, true);
    CheckUInt("zone 46 is avoided", verdict.avoidedZone, 46);
    Check("an alternative existed", verdict.noAlternative, false);
    CheckText("the travel log names the zone",
              ZoneAvoidanceLine("Ugga", "at:0:1,2,3", verdict, danger),
              "overseer: 'Ugga' is routed around zone 46 on its way to 'at:0:1,2,3' - "
              "score 3.00, 3 of 3 deaths there in the window to creatures far above "
              "the party (#151)");
}

// AC2: when the re-plan still crosses the zone, or no re-plan exists, there is
// no other route and the log says so.
void NoOtherRouteIsSaid()
{
    std::vector<ZoneDanger> const danger = {{46, 3, 3, 3.0}};
    TravelAvoidance const through = AvoidDangerousRoute({12, 46}, {40, 46}, danger, 3);
    Check("the re-plan still crosses: nothing is avoided", through.avoid, false);
    Check("and there is no alternative", through.noAlternative, true);
    CheckUInt("the zone is named", through.avoidedZone, 46);
    TravelAvoidance const none = AvoidDangerousRoute({12, 46}, {}, danger, 3);
    Check("no re-plan at all is no alternative too", none.noAlternative, true);
    CheckText("the travel log says there is no other route",
              ZoneAvoidanceLine("Ugga", "Thelsamar", through, danger),
              "overseer: 'Ugga' has no route to 'Thelsamar' that avoids zone 46 - score "
              "3.00, 3 of 3 deaths there in the window to creatures far above the party - "
              "so it walks the planned route through it (#151)");
}

// A safe route asks nothing and says nothing.
void ASafeRouteSaysNothing()
{
    std::vector<ZoneDanger> const danger = {{46, 3, 3, 3.0}};
    TravelAvoidance const verdict = AvoidDangerousRoute({12, 40}, {}, danger, 3);
    Check("nothing is avoided", verdict.avoid, false);
    Check("nothing is missing", verdict.noAlternative, false);
    CheckText("and no line is written", ZoneAvoidanceLine("Ugga", "x", verdict, danger), "");
}

// AC5: the `danger` probe the family page reads carries the list text and the
// thresholds it was cut at, escaped as JSON.
void TheDangerProbeCarriesTheList()
{
    std::vector<ZoneDanger> const zones = {{46, 3, 3, 3.0}};
    std::vector<CreatureDanger> const creatures = {{12345, 2, 17}};
    CheckText("the probe json",
              DangerProbeJson(zones, creatures, 3, 2, 17),
              "{\"party_level\":17,\"zone_threshold\":3,\"creature_threshold\":2,"
              "\"zones\":[{\"zone\":46,\"score\":3.00,\"deaths\":3,\"lethal\":3}],"
              "\"creatures\":[{\"entry\":12345,\"deaths\":2}],"
              "\"text\":\"zone 46: score 3.00 (3 of 3 deaths to creatures far above the "
              "party)\\u000acreature 12345: 2 deaths, not pulled\\u000a\"}");
    CheckText("a quiet probe is empty lists and empty text",
              DangerProbeJson({}, {}, 3, 2, 17),
              "{\"party_level\":17,\"zone_threshold\":3,\"creature_threshold\":2,"
              "\"zones\":[],\"creatures\":[],\"text\":\"\"}");
}

}  // namespace

int main()
{
    TwoDeathsToAnEntryWithinAnHourSuppressThePull();
    TwoDeathsOutsideTheWindowDoNotSuppressThePull();
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
    TheDangerListNamesZonesAndCreaturesAboveThreshold();
    AQuietDangerListIsEmpty();
    DeathsPerHourAreComparedAgainstAWeekBaseline();
    NoTimeSinceTheChangeHasNoRate();
    TwoDeathsPutTheEntryInTheSessionBook();
    TheRefusalOutlivesTheHourWindow();
    FightingBackIsNotRefused();
    TheRefusalLiftsNLevelsHigher();
    TheFirstDangerousZoneOnARouteIsFound();
    ARouteThroughADangerousZoneIsWalkedAround();
    NoOtherRouteIsSaid();
    ASafeRouteSaysNothing();
    TheDangerProbeCarriesTheList();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_death_danger_memory: all passed\n");
    return 0;
}
