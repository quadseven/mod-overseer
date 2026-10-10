/*
 * A guild run's finder group gets its dungeon back before anybody logs in.
 *
 * The live failure this pins (dev realm, 2026-10-10 17:00 UTC). The finder
 * groups and the members' instance binds both survived the restart, and the
 * instances were still saved (30, 31 and 34, with up to 3 bosses down). Even
 * so, all four guild runs inside were ended "nobody of it was back inside the
 * dungeon; 5 of 5 member(s) back in the world". Every Deadmines member logged
 * back in at the same spot outside the door in Westfall, and every Ragefire
 * member outside it in Orgrimmar.
 *
 * The cause is in the core. GroupMgr::LoadGroups deletes `lfg_data` for any
 * group whose type is not exactly 12 (finder and finder-restricted). A guild
 * run's group is a premade party queued through the finder, which the core
 * converts with ConvertToLFG(false): type 8. So its finder dungeon is dropped
 * on every load. MapMgr::PlayerCannotEnter then refuses a finder group any map
 * that is not its finder dungeon, the login check fails, and the core sends the
 * member out through the map's go-back trigger.
 *
 * The module now gives each such group its dungeon back at startup, from the
 * run's own row, before the world loop runs and before any bot logs in. That is
 * the same LFGMgr::SetDungeon the core's own load would have made.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GuildRunFinderRestoreFacts;
using OverseerDecisions::GuildRunRestoresFinderDungeon;
using OverseerDecisions::GuildRunResume;
using OverseerDecisions::ReadGuildRunResume;

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

// Guild run 510's row as the 17:00 restart found it (command 826507), cut
// after its first member.
char const* const RUN_510 =
    "{\"phase\":\"inside\",\"outcome\":\"\",\"why\":\"\",\"keyword\":\"deadmines\","
    "\"dungeon_id\":6,\"map\":36,\"seconds_queued\":4,\"seconds_inside\":7009,"
    "\"deaths\":0,\"bosses_done\":3,\"bosses_total\":7,\"dc_on\":true,"
    "\"ilvl_start\":753,\"ilvl_end\":0,\"members\":["
    "{\"name\":\"Bigzug\",\"seat\":\"tank\",\"level_start\":25,\"level_end\":25,\"deaths\":0}]}";

// What the startup sees of run 510: the saved finder group, with no dungeon
// left in the finder, and the Deadmines finder entry's own map.
GuildRunFinderRestoreFacts Run510AtStartup()
{
    GuildRunResume const r = ReadGuildRunResume(RUN_510);
    GuildRunFinderRestoreFacts f;
    f.wasInside = r.inside;
    f.dungeonId = r.dungeonId;
    f.runMap = r.mapId;
    f.dungeonMap = 36;
    f.finderGroup = true;
    f.knownDungeon = 0;
    return f;
}

void AGroupThatLostItsDungeonGetsItBack()
{
    GuildRunFinderRestoreFacts f = Run510AtStartup();
    Check("run 510's row names finder dungeon 6 in map 36", f.dungeonId == 6 && f.runMap == 36);
    Check("the saved finder group with no dungeon left gets it back",
          GuildRunRestoresFinderDungeon(f));
}

void NothingElseIsTouched()
{
    GuildRunFinderRestoreFacts f = Run510AtStartup();
    f.knownDungeon = 6;
    Check("a group the core kept its dungeon for is left alone", !GuildRunRestoresFinderDungeon(f));

    f = Run510AtStartup();
    f.finderGroup = false;
    Check("a plain party, or no group at all, is not given a finder dungeon",
          !GuildRunRestoresFinderDungeon(f));

    f = Run510AtStartup();
    f.wasInside = false;
    Check("a run that was still queueing is not put inside", !GuildRunRestoresFinderDungeon(f));

    f = Run510AtStartup();
    f.dungeonId = 0;
    Check("a row naming no finder dungeon restores nothing", !GuildRunRestoresFinderDungeon(f));

    f = Run510AtStartup();
    f.dungeonMap = 389;
    Check("a finder entry whose map is not the run's is refused", !GuildRunRestoresFinderDungeon(f));
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

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::string const restore =
        Between(source, "void RestoreGuildRunFinderDungeons()", "class OverseerWorldScript");
    Check("the startup restore is found", !restore.empty());
    Check("it reads the run's own row", restore.find("ReadGuildRunResume(") != std::string::npos);
    Check("it finds the group the core loaded, by the tank",
          restore.find("GetCharacterGroupGuidByGuid(") != std::string::npos);
    Check("it decides with the pure rule",
          restore.find("OverseerDecisions::GuildRunRestoresFinderDungeon(facts)") != std::string::npos);
    Check("it hands the core's own finder state back",
          restore.find("sLFGMgr->SetDungeon(") != std::string::npos);
    Check("the deploy proof is said",
          restore.find("is given back its finder dungeon") != std::string::npos);
    Check("nobody is moved by it", restore.find("TeleportTo") == std::string::npos);

    std::string const script = Between(source, "class OverseerWorldScript", "void OnUpdate(uint32 diff)");
    Check("it runs at startup, before the world loop and any login",
          script.find("void OnStartup() override") != std::string::npos &&
              script.find("RestoreGuildRunFinderDungeons();") != std::string::npos);
}

}  // namespace

int main()
{
    AGroupThatLostItsDungeonGetsItBack();
    NothingElseIsTouched();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
