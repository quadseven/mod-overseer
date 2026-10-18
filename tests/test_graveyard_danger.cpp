/*
 * A graveyard is judged by the same danger test as repeat deaths at a corpse
 * (mod-overseer#933).
 *
 * The graveyard safety test counted hostile spawns ABOVE the member's level.
 * On wow-dev 2026-10-12, 15 Horde guild members at levels 26 and 27 died 355
 * times in 24 hours to Hillsbrad Footmen (entry 2268, level 26) within about
 * 25 yards of the Hillsbrad Fields graveyard. For a level 26 ghost no footman
 * is above it, so the graveyard passed and the repeat-death rung sent the ghost
 * to the healer there, while the repeat-death line beside it already counted
 * "3 live hostile(s) there at or above its level or of its killer's kind".
 * Both now ask OverseerDecisions::JudgePlaceDanger.
 *
 * Compiled against src/overseer_decisions.cpp; the last block reads
 * src/mod_overseer.cpp to hold both callers to the one function.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::JudgePlaceDanger;
using OverseerDecisions::PlaceDanger;
using OverseerDecisions::PlaceDangerFacts;
using OverseerDecisions::PlaceDangerReason;

namespace
{

int failures = 0;

char const* Word(PlaceDangerReason r)
{
    switch (r)
    {
        case PlaceDangerReason::Safe: return "safe";
        case PlaceDangerReason::Hostiles: return "hostiles";
        case PlaceDangerReason::RepeatDeaths: return "repeat deaths";
    }
    return "?";
}

void ExpectReason(char const* what, PlaceDanger const& got, PlaceDangerReason want)
{
    if (got.reason == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Word(got.reason), Word(want));
    ++failures;
}

void ExpectNumber(char const* what, unsigned long got, unsigned long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lu, wanted %lu\n", what, got, want);
    ++failures;
}

void ExpectTrue(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr uint32_t HILLSBRAD_FOOTMAN = 2268;
constexpr uint32_t HILLSBRAD_FARMER = 2264;
constexpr uint32_t MOUNTAIN_LION = 2406;

// Text from `from` to the first `end` after it, or empty.
std::string Body(std::string const& text, std::string const& from, std::string const& end)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return {};
    std::size_t const stop = text.find(end, start);
    if (stop == std::string::npos)
        return {};
    return text.substr(start, stop - start);
}

}  // namespace

int main()
{
    // A level 26 ghost at the Hillsbrad Fields graveyard: three level 26
    // footmen, the kind that killed it, and three of its deaths there inside
    // the window. The old test (above its level) passed this graveyard.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 26;
        facts.killerEntry = HILLSBRAD_FOOTMAN;
        facts.hostiles = {{26, HILLSBRAD_FOOTMAN}, {26, HILLSBRAD_FOOTMAN},
                          {26, HILLSBRAD_FOOTMAN}, {22, HILLSBRAD_FARMER}};
        facts.deathsNear = 3;
        facts.repeatDeaths = 2;
        PlaceDanger const d = JudgePlaceDanger(facts);
        ExpectReason("a level 26 ghost refuses the graveyard among level 26 footmen", d,
                     PlaceDangerReason::Hostiles);
        ExpectNumber("the three footmen count, the farmer below it does not", d.threats, 3);
        ExpectNumber("the strongest is level 26", d.strongest, 26);
        ExpectTrue("the strongest points at a footman",
                   facts.hostiles[d.strongestAt].entry == HILLSBRAD_FOOTMAN);
    }

    // Same-level hostiles alone, with no killer known and no deaths: refused,
    // where the old above-its-level test passed it.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 26;
        facts.hostiles = {{26, HILLSBRAD_FOOTMAN}};
        ExpectReason("a same-level hostile alone refuses the graveyard",
                     JudgePlaceDanger(facts), PlaceDangerReason::Hostiles);
    }

    // Only lower-level hostiles and no repeat deaths: accepted.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 26;
        facts.killerEntry = HILLSBRAD_FOOTMAN;
        facts.hostiles = {{22, HILLSBRAD_FARMER}, {24, MOUNTAIN_LION}};
        facts.deathsNear = 0;
        PlaceDanger const d = JudgePlaceDanger(facts);
        ExpectReason("lower-level hostiles of another kind and no deaths pass", d,
                     PlaceDangerReason::Safe);
        ExpectNumber("nothing counts", d.threats, 0);
    }

    // At a resurrection destination, a nearby camp a few levels below the
    // ghost is still dangerous: it can attack before the ghost gets away.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 30;
        facts.hostiles = {{26, HILLSBRAD_FOOTMAN}, {26, HILLSBRAD_FOOTMAN},
                          {26, HILLSBRAD_FOOTMAN}};
        facts.threatLevelGap = 5;
        PlaceDanger const d = JudgePlaceDanger(facts);
        ExpectReason("three level 26 hostiles refuse the healer for a level 30 ghost", d,
                     PlaceDangerReason::Hostiles);
        ExpectNumber("all three nearby camp hostiles count", d.threats, 3);
    }

    // The killer's kind counts at any level: a level 27 member killed by a
    // level 26 footman, and even a level 10 one.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 27;
        facts.killerEntry = HILLSBRAD_FOOTMAN;
        facts.hostiles = {{26, HILLSBRAD_FOOTMAN}};
        ExpectReason("a level 27 member refuses the graveyard among the footmen that killed it",
                     JudgePlaceDanger(facts), PlaceDangerReason::Hostiles);
        facts.hostiles = {{10, HILLSBRAD_FOOTMAN}};
        ExpectReason("the killer's kind counts far below the member's level",
                     JudgePlaceDanger(facts), PlaceDangerReason::Hostiles);
        facts.killerEntry = 0;
        ExpectReason("with no killer known, a level 10 footman does not count",
                     JudgePlaceDanger(facts), PlaceDangerReason::Safe);
    }

    // Repeat deaths beside it refuse a graveyard with no hostiles counted.
    {
        PlaceDangerFacts facts;
        facts.memberLevel = 26;
        facts.hostiles = {{20, MOUNTAIN_LION}};
        facts.repeatDeaths = 2;
        facts.deathsNear = 1;
        ExpectReason("one death beside it is not a loop", JudgePlaceDanger(facts),
                     PlaceDangerReason::Safe);
        facts.deathsNear = 2;
        ExpectReason("two deaths beside it inside the window refuse it", JudgePlaceDanger(facts),
                     PlaceDangerReason::RepeatDeaths);
        facts.repeatDeaths = 0;
        facts.deathsNear = 9;
        ExpectReason("a zero repeat count turns the death rule off", JudgePlaceDanger(facts),
                     PlaceDangerReason::Safe);
    }

    // BOTH CALLERS ASK THE ONE FUNCTION. The graveyard choice (GraveyardRefusal)
    // and the repeat-death line (DriveGhostRecovery) each call
    // JudgePlaceDanger, and neither counts hostiles its own way any more.
    {
        std::ifstream source("src/mod_overseer.cpp");
        if (!source)
        {
            std::printf("FAIL cannot read src/mod_overseer.cpp\n");
            return 1;
        }
        std::string const text((std::istreambuf_iterator<char>(source)),
                               std::istreambuf_iterator<char>());
        std::string const refusal = Body(text, "static std::string GraveyardRefusal(", "\n}\n");
        ExpectTrue("GraveyardRefusal is found", !refusal.empty());
        ExpectTrue("GraveyardRefusal calls JudgePlaceDanger",
                   refusal.find("OverseerDecisions::JudgePlaceDanger(") != std::string::npos);
        ExpectTrue("GraveyardRefusal no longer counts spawns above the level itself",
                   refusal.find("HostileSpawnsNear(") == std::string::npos);
        ExpectTrue("GraveyardRefusal counts hostiles a few levels below the ghost",
                   refusal.find("facts.threatLevelGap = 5;") != std::string::npos);

        std::size_t const drive = text.find("bool DriveGhostRecovery(");
        std::size_t const line = text.find("live hostile(s) there at or above its level", drive);
        std::size_t const judge = text.find("OverseerDecisions::JudgePlaceDanger(", drive);
        ExpectTrue("the repeat-death line calls JudgePlaceDanger before it speaks",
                   drive != std::string::npos && judge != std::string::npos &&
                       line != std::string::npos && judge < line);
        ExpectTrue("the live threat count is the shared verdict's",
                   text.find("facts.liveThreatsNearCorpse = corpseDanger.threats;", drive) <
                       line);
        std::string const check = Body(text, "struct GhostThreatCheck", "\n    };\n");
        ExpectTrue("the live grid check no longer applies its own level rule",
                   !check.empty() && check.find("CorpseThreat(") == std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("graveyard danger: all checks passed\n");
    return 0;
}
