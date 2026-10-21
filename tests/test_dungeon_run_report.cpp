/*
 * The post-run dungeon report: built from the summary view's rows so that
 * Discord and the family page read the same report (mod-overseer#152).
 *
 * Every decision here is about strings: which events belong to which run, how
 * they are ordered, and how they are rendered. None of it touches a world, a
 * bot, or a database, so it can be exercised directly with no core headers.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

using OverseerDecisions::BuildDungeonRunReport;
using OverseerDecisions::DungeonRunEventRow;
using OverseerDecisions::DungeonRunFacts;
using OverseerDecisions::DungeonRunReport;
using OverseerDecisions::DungeonRunReportLine;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

void CheckText(std::string const& got, std::string const& want, char const* what)
{
    if (got != want)
    {
        std::printf("FAIL %s:\n  got  [%s]\n  want [%s]\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

void CheckU(char const* what, std::uint64_t got, std::uint64_t want)
{
    if (got != want)
    {
        std::printf("FAIL %s: got %llu, want %llu\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    }
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

DungeonRunEventRow Row(std::string const& character, std::string const& kind,
                       std::string const& subject, std::string const& detail,
                       std::string const& firstSeen)
{
    DungeonRunEventRow row;
    row.character_name = character;
    row.kind = kind;
    row.subject_name = subject;
    row.detail = detail;
    row.first_seen = firstSeen;
    row.occurrences = 1;
    return row;
}

DungeonRunFacts Facts()
{
    DungeonRunFacts facts;
    facts.run_id = 42;
    facts.leader_name = "Grug";
    facts.map_id = 36;
    facts.state = "ended";
    facts.outcome = "complete";
    facts.started_at = "2026-10-19 10:00:00";
    facts.ended_at = "2026-10-19 10:45:00";
    return facts;
}

// ---------------------------------------------------------- basic rendering --

void EmptyRunStillReports()
{
    DungeonRunReport const report = BuildDungeonRunReport(Facts(), {});
    CheckText(report.text,
              "Dungeon run 42: led by Grug, map 36, outcome complete\n"
              "  started 2026-10-19 10:00:00, ended 2026-10-19 10:45:00\n",
              "an empty run still renders its facts");
    Check(report.run.run_id == 42 &&
                                            report.run.leader_name == "Grug" &&
                                            report.run.map_id == 36 &&
                                            report.run.outcome == "complete",
          "the report keeps the facts");
    Check(report.lines.empty(), "no lines when there are no events");
}

void OneEventOneLine()
{
    std::vector<DungeonRunEventRow> events;
    events.push_back(Row("Grug", "level_up", "17", "", "2026-10-19 10:30:00"));

    DungeonRunReport const report = BuildDungeonRunReport(Facts(), events);
    CheckU("one event becomes one line", report.lines.size(), 1);
    CheckText(report.lines[0].character_name, "Grug", "line carries the character");
    CheckText(report.lines[0].kind, "level_up", "line carries the kind");
    CheckText(report.lines[0].subject_name, "17", "line carries the subject");
    Check(Has(report.text, "level_up 17"), "text mentions the level");
}

void EventsGroupedByCharacter()
{
    std::vector<DungeonRunEventRow> events;
    events.push_back(Row("O'Brien", "death", "", "", "2026-10-19 10:10:00"));
    events.push_back(Row("Grug", "loot_won", "Heart of the Old Ones", "loot x1 from Charlga", "2026-10-19 10:20:00"));
    events.push_back(Row("O'Brien", "quest_reward", "The Old Guard", "", "2026-10-19 10:40:00"));

    DungeonRunReport const report = BuildDungeonRunReport(Facts(), events);
    CheckU("three events, three lines", report.lines.size(), 3);
    // Grouped by character: Grug first (alphabetical), then O'Brien.
    CheckText(report.lines[0].character_name, "Grug", "leader's character first");
    CheckText(report.lines[1].character_name, "O'Brien", "then the next character");
    CheckText(report.lines[2].character_name, "O'Brien", "same character's events together");
    Check(Has(report.text, "Grug:\n") && Has(report.text, "O'Brien:\n"),
          "text has two character blocks");
}

void EventsOrderedWithinCharacter()
{
    std::vector<DungeonRunEventRow> events;
    // Out of order on purpose; the report must sort by first_seen.
    events.push_back(Row("Grug", "boss_kill", "Charlga Razorflank", "encounter Charlga Razorflank", "2026-10-19 10:40:00"));
    events.push_back(Row("Grug", "level_up", "17", "", "2026-10-19 10:30:00"));
    events.push_back(Row("Grug", "death", "", "", "2026-10-19 10:20:00"));

    DungeonRunReport const report = BuildDungeonRunReport(Facts(), events);
    CheckU("three lines in time order", report.lines.size(), 3);
    CheckText(report.lines[0].kind, "death", "earliest event first");
    CheckText(report.lines[1].kind, "level_up", "then the next");
    CheckText(report.lines[2].kind, "boss_kill", "the latest last");
    Check(report.text.find("death") < report.text.find("level_up") &&
              report.text.find("level_up") < report.text.find("boss_kill"),
          "text lists death before level before boss");
}

void OccurrencesShownWhenMoreThanOne()
{
    std::vector<DungeonRunEventRow> events;
    DungeonRunEventRow row = Row("Grug", "death", "", "", "2026-10-19 10:20:00");
    row.occurrences = 3;
    events.push_back(row);

    DungeonRunReport const report = BuildDungeonRunReport(Facts(), events);
    CheckU("occurrences carried on the line", report.lines[0].occurrences, 3);
    Check(Has(report.text, "(3x)"), "text shows the count");
}

void NoOccurrencesWhenOne()
{
    std::vector<DungeonRunEventRow> events;
    events.push_back(Row("Grug", "death", "", "", "2026-10-19 10:20:00"));

    DungeonRunReport const report = BuildDungeonRunReport(Facts(), events);
    Check(!Has(report.text, "(1x)"), "text does not show (1x)");
}

}  // namespace

int main()
{
    EmptyRunStillReports();
    OneEventOneLine();
    EventsGroupedByCharacter();
    EventsOrderedWithinCharacter();
    OccurrencesShownWhenMoreThanOne();
    NoOccurrencesWhenOne();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("dungeon run report: all checks passed\n");
    return EXIT_SUCCESS;
}
