/*
 * A worldserver roll leaves a run inside intact (wow-overseer#816, items 1 to 4,
 * 6 and 7).
 *
 * The live failure (dev realm, three days to 2026-10-10): 27 guild runs ended
 * "claim expired" 28 to 136 seconds after a worldserver start, and a family
 * campaign through a roll lost its run number. This file pins what the module
 * does for the pieces #896, #901 and #906 left open:
 *
 *   1. A waiting adoption touches its row, so the bridge's 300 s stale sweep
 *      never sees it.
 *   2. A stray's quarter hour starts when its tank is seen, so a late tank's
 *      finder group is still disbanded.
 *   3. CloseAbandonedRuns waits 10 minutes after the start, so a roll does not
 *      leave a phantom emptied run and renumber the real one.
 *   4. A family run that entered by the finder gets its dungeon back at startup.
 *   5. OnShutdown flushes the chat, event, death and level queues.
 *   6. An adopted run's loot tally rides its heartbeat and is read back.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

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

void AWaitingAdoptionTouchesItsRow()
{
    Check("a row written a moment ago is not touched again", !GuildRunAdoptionTouchDue(5));
    Check("a row untouched for a minute is touched", GuildRunAdoptionTouchDue(60));
    Check("the touch comes well inside the bridge's stale bound",
          GUILD_RUN_ADOPT_TOUCH_SECONDS * 2 < GUILD_RUN_ROW_STALE_SECONDS);
    Check("a whole adoption wait needs several touches",
          GUILD_RUN_ADOPT_SECONDS / GUILD_RUN_ADOPT_TOUCH_SECONDS >= 10);
}

void AStrayClockStartsWhenTheTankIsSeen()
{
    std::int64_t const read = 1000;
    Check("the clock is not running while the tank is away",
          GuildRunStrayExpiry(false, 0, read) == 0);
    Check("a stray with no clock never reads expired", !GuildRunStrayExpired(0, read + 100000));

    // The tank logs in 40 minutes after the row was read.
    std::int64_t const seen = read + 40 * 60;
    std::int64_t const expiry = GuildRunStrayExpiry(true, 0, seen);
    Check("the clock starts at the tank's first sight", expiry == seen + 15 * 60);
    Check("the late tank's stray is not expired when seen", !GuildRunStrayExpired(expiry, seen));
    Check("the clock does not restart on the next poll",
          GuildRunStrayExpiry(true, expiry, seen + 30) == expiry);
    Check("it expires a quarter hour after first sight",
          GuildRunStrayExpired(expiry, seen + 15 * 60));

    // What the late tank now gets: its finder group is taken out and disbanded.
    GuildRunStrayFacts facts;
    facts.tankInGroup = true;
    facts.finderGroup = true;
    facts.expired = GuildRunStrayExpired(expiry, seen);
    Check("a late tank in its finder group is taken out", GuildRunStrayNext(facts) ==
                                                           GuildRunStrayStep::TakeOut);
    facts.takenOut = true;
    Check("and its group is then disbanded", GuildRunStrayNext(facts) ==
                                                 GuildRunStrayStep::Disband);
}

void ARollDoesNotCloseRunsAsAbandoned()
{
    Check("at the start the close is held", RunCloseHeldAfterStartup(0));
    Check("two minutes in, the old cold bound would have closed it, and the close is held",
          RunCloseHeldAfterStartup(121));
    Check("nine minutes in it is still held", RunCloseHeldAfterStartup(9 * 60));
    Check("ten minutes in the close works again", !RunCloseHeldAfterStartup(10 * 60));
}

void AFamilyRunThatEnteredByTheFinderGetsItsDungeonBack()
{
    FamilyRunFinderRestoreFacts f;
    f.runActive = true;
    f.runMap = 36;
    f.dungeonId = 6;
    f.dungeonMap = 36;
    f.finderGroup = true;
    f.knownDungeon = 0;
    Check("an active run in a saved finder group with no dungeon gets it back",
          FamilyRunRestoresFinderDungeon(f));

    FamilyRunFinderRestoreFacts g = f;
    g.finderGroup = false;
    Check("a family that walked in (a plain party) is left alone",
          !FamilyRunRestoresFinderDungeon(g));
    g = f;
    g.knownDungeon = 6;
    Check("a dungeon the core kept is left alone", !FamilyRunRestoresFinderDungeon(g));
    g = f;
    g.runActive = false;
    Check("a run that is not active restores nothing", !FamilyRunRestoresFinderDungeon(g));
    g = f;
    g.dungeonMap = 389;
    Check("a finder entry on another map is refused", !FamilyRunRestoresFinderDungeon(g));
    g = f;
    g.dungeonId = 0;
    Check("no finder entry for the map restores nothing", !FamilyRunRestoresFinderDungeon(g));
}

void AnAdoptedRunKeepsItsLootTally()
{
    char const* const row =
        "{\"phase\":\"inside\",\"outcome\":\"\",\"why\":\"\",\"keyword\":\"deadmines\","
        "\"dungeon_id\":6,\"map\":36,\"seconds_queued\":4,\"seconds_inside\":7009,"
        "\"deaths\":0,\"bosses_done\":3,\"bosses_total\":7,\"dc_on\":true,"
        "\"ilvl_start\":753,\"ilvl_end\":0,\"loot_items\":14,\"loot_notable\":[1937,2815,5191],"
        "\"members\":[{\"name\":\"Bigzug\",\"seat\":\"tank\",\"level_start\":25,"
        "\"level_end\":25,\"deaths\":0}]}";
    GuildRunResume const r = ReadGuildRunResume(row);
    Check("the loot count is read back", r.lootItems == 14);
    Check("the notable items are read back",
          r.lootNotable.size() == 3 && r.lootNotable[0] == 1937 && r.lootNotable[2] == 5191);
    Check("the rest of the row still reads", r.inside && r.mapId == 36 && r.members.size() == 1);

    GuildRunResume const none = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":36,\"loot_items\":0,\"loot_notable\":[],\"members\":[]}");
    Check("an empty tally reads as nothing", none.lootItems == 0 && none.lootNotable.empty());
    GuildRunResume const holes = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":36,\"loot_items\":3,"
        "\"loot_notable\":[1937,,5191],\"members\":[]}");
    Check("a hole in the notable list reads as no list, never a shifted one",
          holes.lootNotable.empty());
    GuildRunResume const nested = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":36,\"loot_notable\":[1937,[5191]],\"members\":[]}");
    Check("a nested array reads as no list", nested.lootNotable.empty());
    GuildRunResume const old = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":36,\"deaths\":0,\"members\":[]}");
    Check("a row written before the tally rode the heartbeat reads as nothing",
          old.lootItems == 0 && old.lootNotable.empty());
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin + 1);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

bool Has(std::string const& in, char const* what)
{
    return in.find(what) != std::string::npos;
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }

    std::string const adoptions =
        Between(source, "void DriveGuildRunAdoptions(std::time_t now)", "void DriveGuildRunStrays");
    Check("the adoption drive is found", !adoptions.empty());
    Check("a waiting adoption asks the rule before it touches the row",
          Has(adoptions, "OverseerDecisions::GuildRunAdoptionTouchDue("));
    Check("the touch writes updated_at, guarded on the holder",
          Has(adoptions, "SET updated_at = NOW() WHERE id = {} ") &&
              Has(adoptions, "AND status = 'verifying' AND claimed_by = '{}'\",\n"
                             "                        adoption.id, g_runToken);"));
    Check("the touch says so", Has(adoptions, "touched so the bridge does not end it as stale"));
    Check("the adopted run carries its loot tally back in",
          Has(adoptions, "SeedGuildRunLoot(") && Has(adoptions, "adoption.resume.lootItems"));

    std::string const strays =
        Between(source, "void DriveGuildRunStrays(std::time_t now)", "HOW OFTEN THE LAST RUNG");
    Check("the stray drive is found", !strays.empty());
    Check("the stray clock starts on the tank's first sight",
          Has(strays, "OverseerDecisions::GuildRunStrayExpiry(") &&
              Has(strays, "OverseerDecisions::GuildRunStrayExpired("));
    Check("no stray is given a clock at the read",
          !Has(strays, "stray.until = now +") && !Has(adoptions, "stray.until = now +"));

    std::string const close =
        Between(source, "void CloseAbandonedRuns()", "QueryResult runs = CharacterDatabase.Query(");
    Check("the close is found", !close.empty());
    Check("the close waits after the start",
          Has(close, "OverseerDecisions::RunCloseHeldAfterStartup(") && Has(close, "g_startedAt"));

    std::string const family = Between(source, "static void RestoreFamilyRunFinderDungeons()",
                                       "void DriveGuildFinderRuns()");
    Check("the family restore is found", !family.empty());
    Check("it reads the active runs", Has(family, "FROM overseer_dungeon_run WHERE state = 'active'"));
    Check("it decides with the pure rule",
          Has(family, "OverseerDecisions::FamilyRunRestoresFinderDungeon(facts)"));
    Check("it hands the core's own finder state back", Has(family, "sLFGMgr->SetDungeon("));
    Check("the deploy proof is said", Has(family, "dungeon run {} - the finder group under"));
    Check("nobody is moved by it", !Has(family, "TeleportTo"));

    std::string const script = Between(source, "class OverseerWorldScript", "void OnUpdate(uint32 diff)");
    Check("the family restore runs at startup, after the guild one",
          script.find("RestoreGuildRunFinderDungeons();") != std::string::npos &&
              script.find("RestoreFamilyRunFinderDungeons();") >
                  script.find("RestoreGuildRunFinderDungeons();"));
    std::string const shutdown = Between(source, "void OnShutdown() override", "void OnUpdate(uint32 diff)");
    Check("a shutdown hook exists", !shutdown.empty());
    Check("it flushes the chat queue", Has(shutdown, "FlushChat();"));
    Check("it flushes the event queue", Has(shutdown, "FlushEvents();"));
    Check("it flushes the death queue", Has(shutdown, "FlushDeaths();"));
    Check("it flushes the level queue", Has(shutdown, "FlushLevels();"));

    Check("the heartbeat writes the loot tally without taking it",
          Has(source, "GuildRunLootTally const loot = PeekGuildRunLoot(run.guids);") &&
              Has(source, "GuildRunJson(run, \"inside\", &loot, 0, now)"));
}

}  // namespace

int main()
{
    AWaitingAdoptionTouchesItsRow();
    AStrayClockStartsWhenTheTankIsSeen();
    ARollDoesNotCloseRunsAsAbandoned();
    AFamilyRunThatEnteredByTheFinderGetsItsDungeonBack();
    AnAdoptedRunKeepsItsLootTally();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
