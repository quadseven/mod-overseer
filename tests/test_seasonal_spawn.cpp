/*
 * A seasonal spawn out of season is not a destination (mod-overseer#686).
 *
 * The core keeps an event's creature spawns in GameEventMgr::
 * GameEventCreatureGuids, list `eventCount + event - 1` for a signed event, and
 * puts them in the world only while the event runs (or, for a negative event,
 * only while it does not). The numbers are the dev realm's: its game_event
 * table ends at event 190, so the core sizes the event map at 191 (the highest
 * id plus one); Orgrimmar's "Warrior Trainer" 26332 is
 * event 31 (Arena Tournament) and the vendor 14480 is event 10 (Children's
 * Week).
 *
 * The last part reads src/mod_overseer.cpp (run from the repo root) to pin the
 * places that must ask.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GameEventOfSlot;
using OverseerDecisions::SpawnStandsNow;

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

constexpr uint32_t EVENTS = 191;  // highest event id on the realm, plus one

void TheSlotLayoutIsTheCores()
{
    // internal_event_id = _gameEvent.size() + eventId - 1
    Check("event 31 sits in slot 221", GameEventOfSlot(EVENTS + 31 - 1, EVENTS) == 31);
    Check("event 10 sits in slot 200", GameEventOfSlot(EVENTS + 10 - 1, EVENTS) == 10);
    Check("event 1 sits in slot 191", GameEventOfSlot(EVENTS, EVENTS) == 1);
    Check("event -10 sits in slot 180", GameEventOfSlot(EVENTS - 10 - 1, EVENTS) == -10);
    Check("slot 0 is the lowest negative event", GameEventOfSlot(0, EVENTS) == -190);
    Check("the middle slot belongs to no event", GameEventOfSlot(EVENTS - 1, EVENTS) == 0);
    Check("the last slot is event 190", GameEventOfSlot(2 * EVENTS - 2, EVENTS) == 190);
    Check("past the end is no event", GameEventOfSlot(2 * EVENTS - 1, EVENTS) == 0);
    Check("an empty event map has no events", GameEventOfSlot(0, 0) == 0);
}

void OnlyTheSeasonsSpawnsWait()
{
    Check("a spawn with no event always stands", SpawnStandsNow(0, false));
    Check("...whatever runs", SpawnStandsNow(0, true));
    Check("the Arena Tournament trainer is absent while the tournament does not run",
          !SpawnStandsNow(31, false));
    Check("...and stands while it runs", SpawnStandsNow(31, true));
    Check("the Children's Week vendor is absent out of the week", !SpawnStandsNow(10, false));
    Check("a spawn an event takes away stands while it does not run",
          SpawnStandsNow(-10, false));
    Check("...and is absent while it runs", !SpawnStandsNow(-10, true));
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void EverySpawnSearchAsks()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    auto count = [&](char const* text) {
        std::size_t n = 0;
        for (std::size_t at = source.find(text); at != std::string::npos;
             at = source.find(text, at + 1))
            ++n;
        return n;
    };
    Check("the index records each spawn's event",
          source.find("spawn.gameEvent = GameEventOfSpawn(itr.first);") != std::string::npos);
    Check("both index searches skip a spawn out of the world",
          count("if (!SpawnInWorldNow(spawn.gameEvent))") == 2);
    Check("the guild-bot walk skips it too",
          source.find("if (!SpawnInWorldNow(GameEventOfSpawn(itr.first)))") != std::string::npos);
    Check("the event lists are read through the core's slot layout",
          source.find("OverseerDecisions::GameEventOfSlot(slot, eventCount)") != std::string::npos);
}

}  // namespace

int main()
{
    TheSlotLayoutIsTheCores();
    OnlyTheSeasonsSpawnsWait();
    EverySpawnSearchAsks();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_seasonal_spawn: all passed\n");
    return 0;
}
