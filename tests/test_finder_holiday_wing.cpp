/*
 * A door's finder entry is its normal dungeon, never the holiday boss on the
 * same map, and a member waiting in a battleground queue is named.
 *
 * The live failure this pins (dev realm, 2026-10-08 to 10-10): all 14 guild
 * runs Bonkers sent to Shadowfang Keep were refused, "'Chillmon' is locked out
 * of it (not in season)", and none ever went in. LFGDungeons.dbc holds two
 * normal-difficulty dungeon rows on map 33: 8, Shadowfang Keep (levels 16-26),
 * and 288, The Crown Chemical Co. (the Love is in the Air boss, levels 78-82,
 * Flags 0xf, which carries LFG_FLAG_SEASONAL). lfg_dungeon_template has no row
 * for 8 and one for 288 at (-238.075, 2166.43); the door's areatrigger lands
 * at (-229.135, 2109.18), 57.9 yards away, inside FINDER_WING_MATCH_YARDS. So
 * the wing match picked 288, and the core locks every member out of a
 * seasonal dungeon out of its season (LFG_LOCKSTATUS_NOT_IN_SEASON, 1031).
 * The same holiday rows sit on Scarlet Monastery (285, the Headless
 * Horseman), Blackrock Depths (287, Coren Direbrew) and the Slave Pens (286,
 * Ahune).
 *
 * Blackrock Depths has two normal wings with one start between them: 30, the
 * Prison (levels 47-57), and 276, the Upper City (51-61), both at (458.32,
 * 26.52). A tie goes to the lower id, the Prison, which is the wing the door
 * opens on and the one a group fresh to the dungeon can be queued for.
 *
 * A guild run whose member waits in a battleground queue was refused as a
 * whole, "the finder did not take the group (a battleground or arena queue)"
 * (LFG_JOIN_USING_BG_SYSTEM), 5 times on 2026-10-09 and 10-10, every time
 * over one hunter waiting hours in a Warsong Gulch queue. The refusal named
 * nobody, so the bridge could not bench the member and form again without
 * them. The adapter now names the member before the party is made.
 *
 * Compiles against the pure decision file; reads src/mod_overseer.cpp as text
 * (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ChooseFinderDungeon;
using OverseerDecisions::FinderDungeonCandidate;

namespace
{

int failures = 0;

void Check(char const* what, bool ok, bool want = true)
{
    if (ok == want)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

FinderDungeonCandidate Row(std::uint32_t id, bool hasEntrance, float x, float y, bool seasonal)
{
    FinderDungeonCandidate c;
    c.id = id;
    c.hasEntrance = hasEntrance;
    c.x = x;
    c.y = y;
    c.seasonal = seasonal;
    return c;
}

void ShadowfangKeepIsNotTheHolidayBoss()
{
    std::string why;
    std::vector<FinderDungeonCandidate> const shadowfang = {
        Row(8, false, 0.f, 0.f, false), Row(288, true, -238.075f, 2166.43f, true)};
    Check("the Shadowfang Keep door is dungeon 8",
          ChooseFinderDungeon(shadowfang, -229.135f, 2109.18f, why) == 8);
    Check("and says nothing is wrong", why.empty());
}

void AHolidayRowAloneIsNoDungeon()
{
    std::string why;
    std::vector<FinderDungeonCandidate> const onlyHoliday = {
        Row(288, true, -238.075f, 2166.43f, true)};
    Check("a map with only a holiday row has no finder dungeon",
          ChooseFinderDungeon(onlyHoliday, -229.135f, 2109.18f, why) == 0);
    Check("and says so", why.find("holiday") != std::string::npos);
}

void TheOtherHolidayMapsKeepTheirWings()
{
    std::string why;
    // Scarlet Monastery, read from lfg_dungeon_template on the dev realm.
    std::vector<FinderDungeonCandidate> const scarlet = {
        Row(18, true, 1688.99f, 1053.48f, false), Row(163, true, 1610.83f, -323.433f, false),
        Row(164, true, 855.683f, 1321.5f, false), Row(165, true, 255.346f, -209.09f, false),
        Row(285, true, 1797.52f, 1347.38f, true)};
    Check("the Graveyard door is the Graveyard",
          ChooseFinderDungeon(scarlet, 1688.99f, 1053.48f, why) == 18);
    // Blackrock Depths: two wings on one start, and Coren Direbrew.
    std::vector<FinderDungeonCandidate> const depths = {
        Row(30, true, 458.32f, 26.52f, false), Row(276, true, 458.32f, 26.52f, false),
        Row(287, true, 897.495f, -141.976f, true)};
    Check("the Blackrock Depths door is the Prison, the lower of a tie",
          ChooseFinderDungeon(depths, 456.929f, 34.0923f, why) == 30);
    std::vector<FinderDungeonCandidate> const depthsReversed = {
        Row(287, true, 897.495f, -141.976f, true), Row(276, true, 458.32f, 26.52f, false),
        Row(30, true, 458.32f, 26.52f, false)};
    Check("whatever order the store gives the rows in",
          ChooseFinderDungeon(depthsReversed, 456.929f, 34.0923f, why) == 30);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
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
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("a candidate carries the DBC's seasonal flag",
        "c.seasonal = (row->Flags & lfg::LFG_FLAG_SEASONAL) != 0;");
    std::size_t const begin = source.find("    char const* DoGuildFinderRun(");
    std::size_t const party = source.find("// THE PARTY, AS AN ACCEPTED INVITE MAKES IT", begin);
    Check("the guild finder run exists", begin != std::string::npos && party != std::string::npos);
    if (begin == std::string::npos || party == std::string::npos)
        return;
    std::string const checks = source.substr(begin, party - begin);
    Check("a member waiting in a battleground queue is named before the party forms",
          checks.find("return refuse(\"'\" + name + \"' is waiting in a battleground queue\");") !=
              std::string::npos);
    Check("by the core's own test",
          checks.find("p->InBattlegroundQueue() && !sWorld->getBoolConfig(CONFIG_ALLOW_JOIN_BG_AND_LFG)") !=
              std::string::npos);
    Check("and the member is never taken out of its queue",
          checks.find("RemoveBattlegroundQueueId") == std::string::npos);
}

}  // namespace

int main()
{
    ShadowfangKeepIsNotTheHolidayBoss();
    AHolidayRowAloneIsNoDungeon();
    TheOtherHolidayMapsKeepTheirWings();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_finder_holiday_wing: ok\n");
    return 0;
}
