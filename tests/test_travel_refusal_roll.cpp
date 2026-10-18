/*
 * A worldserver roll keeps the travel errands the drive refused
 * (wow-overseer#1193, layer 5 of wow-overseer#789).
 *
 * The travel book refused an errand that was killing a character, and counted
 * lethal releases of one target in a row, in memory only. A roll forgot both,
 * so an aim still in overseer_roster.travel_npc, or one the bridge wrote again,
 * was walked from zero strikes. Now each refusal and strike is written to
 * overseer_travel_refusal and read back at start-up while it is still live.
 *
 * The decision half is exercised directly, and then fed to ErrandDeathBreaker
 * the way the first travel poll after a roll does. The wiring half reads
 * src/mod_overseer.cpp and the migration (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::ErrandCooloffSeconds;
using OverseerDecisions::ErrandDeathBreaker;
using OverseerDecisions::ErrandDeathLimits;
using OverseerDecisions::ErrandDeathRemedy;
using OverseerDecisions::ErrandDeathToll;
using OverseerDecisions::KeptTravelRefusal;
using OverseerDecisions::RestoredTravelRefusal;
using OverseerDecisions::RestoreTravelRefusal;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

KeptTravelRefusal Lethal(char const* target, uint32_t strikes, int64_t at)
{
    KeptTravelRefusal row;
    row.refusedTarget = target;
    row.reason = "it was killing this character";
    row.refusedAt = at;
    row.strikeTarget = target;
    row.strikes = strikes;
    row.struckAt = at;
    return row;
}

// What the first travel poll after the roll asks the breaker, with the book
// restored from `row`: no deaths yet (the errand's window starts at zero).
ErrandDeathRemedy FirstPollAfterRoll(KeptTravelRefusal const& row, char const* target,
                                     int64_t now, ErrandDeathLimits const& limits)
{
    RestoredTravelRefusal const kept = RestoreTravelRefusal(row, now, limits);
    ErrandDeathToll toll;
    toll.strikes = kept.strike && row.strikeTarget == target ? row.strikes : 0;
    toll.sinceRefused =
        kept.refusal && row.refusedTarget == target ? now - row.refusedAt : -1;
    return ErrandDeathBreaker(toll, limits).remedy;
}

}  // namespace

int main()
{
    ErrandDeathLimits const limits{};
    int64_t const now = 1'800'000'000;

    // THE ACCEPTANCE CASE: refused as lethal five minutes before the roll, still
    // refused after it rather than walked.
    {
        KeptTravelRefusal const row = Lethal("vendor", 1, now - 5 * 60);
        RestoredTravelRefusal const kept = RestoreTravelRefusal(row, now, limits);
        Check("a lethal refusal inside its cool-off is kept", kept.refusal);
        Check("its strike is kept", kept.strike);
        Check("the first poll after the roll refuses the re-issue",
              FirstPollAfterRoll(row, "vendor", now, limits) ==
                  ErrandDeathRemedy::RefuseReissue);
        Check("another target is not refused",
              FirstPollAfterRoll(row, "repair", now, limits) == ErrandDeathRemedy::Continue);
    }

    // Past the plain cool-off: the refusal goes, the strike stays (so the next
    // release doubles the cool-off, #776).
    {
        KeptTravelRefusal const row = Lethal("vendor", 1, now - limits.cooloffSeconds);
        RestoredTravelRefusal const kept = RestoreTravelRefusal(row, now, limits);
        Check("a refusal at its cool-off is not kept", !kept.refusal);
        Check("its strike outlives the cool-off", kept.strike);
        Check("an expired refusal is walked again",
              FirstPollAfterRoll(row, "vendor", now, limits) == ErrandDeathRemedy::Continue);
    }

    // A third strike is refused for four times the cool-off, and that is kept.
    {
        int64_t const age = 2 * limits.cooloffSeconds;
        Check("three strikes cool off for longer than two plain cool-offs",
              ErrandCooloffSeconds(3, limits) > age);
        KeptTravelRefusal const row = Lethal("vendor", 3, now - age);
        Check("a third strike's refusal is kept past the plain cool-off",
              RestoreTravelRefusal(row, now, limits).refusal);
        Check("and still refused after the roll",
              FirstPollAfterRoll(row, "vendor", now, limits) ==
                  ErrandDeathRemedy::RefuseReissue);
    }

    // A refusal for another reason (going nowhere, the economy budget) with a
    // strike on record for a DIFFERENT target gets the plain cool-off.
    {
        KeptTravelRefusal row = Lethal("repair", 3, now - 60);
        row.refusedTarget = "vendor";
        row.reason = "economy errands had taken more than their share of this character's time";
        row.refusedAt = now - 2 * limits.cooloffSeconds;
        RestoredTravelRefusal const kept = RestoreTravelRefusal(row, now, limits);
        Check("another target's strikes do not lengthen this refusal", !kept.refusal);
        Check("that strike is still kept", kept.strike);
        row.refusedAt = now - 60;
        Check("a fresh non-lethal refusal is kept", RestoreTravelRefusal(row, now, limits).refusal);
    }

    // Strikes age out at strikeMemorySeconds, as LethalStrikes does.
    {
        KeptTravelRefusal const row = Lethal("vendor", 4, now - limits.strikeMemorySeconds);
        RestoredTravelRefusal const kept = RestoreTravelRefusal(row, now, limits);
        Check("a strike at its memory limit is not kept", !kept.strike);
        Check("nor is its refusal", !kept.refusal);
    }

    // Empty and future rows keep nothing.
    {
        RestoredTravelRefusal const empty = RestoreTravelRefusal(KeptTravelRefusal{}, now, limits);
        Check("an empty row keeps nothing", !empty.refusal && !empty.strike);
        RestoredTravelRefusal const future =
            RestoreTravelRefusal(Lethal("vendor", 1, now + 60), now, limits);
        Check("a stamp from the future keeps nothing", !future.refusal && !future.strike);
    }

    // THE WIRING. Every Refuse and NoteLethal writes the row; start-up reads it.
    {
        std::string const source = Read("src/mod_overseer.cpp");
        Check("src/mod_overseer.cpp was read", !source.empty());

        std::string const refuse =
            Between(source, "    void Refuse(std::string const& name", "    // Record one more lethal");
        Check("Refuse writes the row", Has(refuse, "KeepRefusal(name);"));

        std::string const lethal =
            Between(source, "    uint32_t NoteLethal(", "    std::size_t RestoreRefusals(");
        Check("NoteLethal writes the row", Has(lethal, "KeepRefusal(name);"));

        std::string const keep = Between(source, "    void KeepRefusal(", "    bool _keepRefusals");
        Check("KeepRefusal writes the one row per character",
              Has(keep, "REPLACE INTO overseer_travel_refusal"));
        Check("KeepRefusal writes synchronously, so the later write wins",
              Has(keep, "DirectExecute"));
        Check("KeepRefusal does nothing without the table", Has(keep, "if (!_keepRefusals)"));

        std::string const restore =
            Between(source, "    std::size_t RestoreRefusals(", "private:");
        Check("RestoreRefusals reads the table", Has(restore, "FROM overseer_travel_refusal"));
        Check("RestoreRefusals keeps only what the decision keeps",
              Has(restore, "OverseerDecisions::RestoreTravelRefusal("));
        Check("a restored refusal is said again on its first re-issue",
              Has(restore, "refusal.said = 0;"));

        std::string const startup =
            Between(source, "    void OnStartup() override", "    void OnShutdown() override");
        Check("start-up restores the refusals", Has(startup, "RestoreTravelRefusals();"));
        Check("start-up probes for the table",
              Has(startup, "SchemaHasColumns(") && Has(startup, "\"overseer_travel_refusal\""));
    }

    {
        std::string const sql =
            Read("data/sql/characters/base/2026_10_18_00_overseer_travel_refusal.sql");
        Check("the migration was read", !sql.empty());
        Check("the migration creates the table idempotently",
              Has(sql, "CREATE TABLE IF NOT EXISTS `overseer_travel_refusal`"));
        for (char const* column : {"`name`", "`refused_target`", "`reason`", "`refused_at`",
                                   "`strike_target`", "`strikes`", "`struck_at`"})
            Check(column, Has(sql, column));
        Check("one row per character", Has(sql, "PRIMARY KEY (`name`)"));
    }

    if (failures)
    {
        std::printf("travel refusals across a roll: %d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("travel refusals across a roll: all checks passed");
    return EXIT_SUCCESS;
}
