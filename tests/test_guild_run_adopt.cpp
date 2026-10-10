/*
 * A worldserver restart does not end a guild run that is still inside.
 *
 * The live failure this pins: on the dev realm over the three days to
 * 2026-10-10, 27 guild runs ended "lost: claim expired: the run holding it is
 * gone", 49 minutes in on average with 1.6 bosses down and 110.8 member-hours
 * inside between them. Every one ended 28 to 136 seconds after a worldserver
 * start, and none after a bridge restart. The run's `guild` row is claimed
 * under the worldserver's run token and its run lives only in that process's
 * memory, so the next worldserver ended the row as abandoned once the 120 s
 * lease ran out, and then took the group out of the dungeon and disbanded it.
 *
 *   guild run 450, Wailing Caverns: 4 of 7 bosses down, 32 minutes in, no
 *     deaths, ended 69 s after the worldserver start of 2026-10-10 01:20 UTC.
 *
 * The group, its members' instance binds and the instance's boss states are
 * all saved by the core and come back with the restart, and the members log
 * back in inside the dungeon. Only the module's memory of the run was lost.
 * The run is now re-adopted from its row, and ended only when it is really
 * gone: the group disbanded, nobody back inside, or nobody back at all.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GuildRunAdoptFacts;
using OverseerDecisions::GuildRunAdoptNext;
using OverseerDecisions::GuildRunAdoptStep;
using OverseerDecisions::GuildRunAdoptWhy;
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

// Guild run 450's row as the restart left it (command 816866), verbatim.
char const* const RUN_450 =
    "{\"phase\":\"inside\",\"outcome\":\"\",\"why\":\"\",\"keyword\":\"wailing\","
    "\"dungeon_id\":1,\"map\":43,\"seconds_queued\":4,\"seconds_inside\":1935,"
    "\"deaths\":0,\"bosses_done\":4,\"bosses_total\":7,\"dc_on\":true,"
    "\"ilvl_start\":688,\"ilvl_end\":0,\"members\":["
    "{\"name\":\"Dreadlox\",\"seat\":\"tank\",\"level_start\":22,\"level_end\":22,\"deaths\":0},"
    "{\"name\":\"Eavokemup\",\"seat\":\"healer\",\"level_start\":20,\"level_end\":20,\"deaths\":0},"
    "{\"name\":\"Elfabulous\",\"seat\":\"dps\",\"level_start\":24,\"level_end\":25,\"deaths\":0},"
    "{\"name\":\"Cooltusk\",\"seat\":\"dps\",\"level_start\":24,\"level_end\":25,\"deaths\":0},"
    "{\"name\":\"Elbowbone\",\"seat\":\"dps\",\"level_start\":24,\"level_end\":24,\"deaths\":2}]}";

void TheRowSaysWhereTheRunWas()
{
    GuildRunResume const r = ReadGuildRunResume(RUN_450);
    Check("run 450 was inside", r.inside);
    Check("in map 43, finder dungeon 1", r.mapId == 43 && r.dungeonId == 1);
    Check("1935 s inside after 4 s queued", r.secondsInside == 1935 && r.secondsQueued == 4);
    Check("its item level at entry", r.ilvlStart == 688);
    Check("all five members are read", r.members.size() == 5);
    Check("in the row's order, with their levels at entry",
          r.members.size() == 5 && r.members[0].name == "Dreadlox" &&
              r.members[0].levelStart == 22 && r.members[4].name == "Elbowbone" &&
              r.members[4].levelStart == 24);
    Check("and their deaths so far", r.members.size() == 5 && r.members[4].deaths == 2);

    Check("a row still queueing is not inside",
          !ReadGuildRunResume("{\"phase\":\"queued\",\"seconds_inside\":0}").inside);
    Check("an empty row is not inside", !ReadGuildRunResume("").inside);
    Check("nor is a refusal",
          !ReadGuildRunResume("{\"phase\":\"refused\",\"outcome\":\"refused\"}").inside);
}

// What the next worldserver sees of run 450 once its members are back: the
// tank in the saved finder group, all five in the world, all five inside.
GuildRunAdoptFacts Run450Back()
{
    GuildRunAdoptFacts f;
    f.wasInside = true;
    f.tankInWorld = true;
    f.tankInFinderGroup = true;
    f.membersInWorld = 5;
    f.membersBack = 5;
    f.groupSize = 5;
    return f;
}

void ARestartMidRunKeepsTheRun()
{
    GuildRunAdoptFacts f = Run450Back();
    Check("all five back inside the saved group: the run is adopted",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Adopt);

    f.membersBack = 2;
    f.membersInWorld = 5;
    Check("two inside and three ghosts or elsewhere: adopted, the run's own rules judge the rest",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Adopt);

    GuildRunAdoptFacts early;
    early.wasInside = true;
    Check("nobody logged back in yet: wait, never expire",
          GuildRunAdoptNext(early) == GuildRunAdoptStep::Wait);

    early.tankInWorld = true;
    early.tankInFinderGroup = true;
    early.membersInWorld = 2;
    early.membersBack = 2;
    Check("the tank and one more back, three still logging in: wait for them",
          GuildRunAdoptNext(early) == GuildRunAdoptStep::Wait);

    early.expired = true;
    Check("the window over with the tank's group inside: adopt what came back",
          GuildRunAdoptNext(early) == GuildRunAdoptStep::Adopt);
}

void ARunThatIsReallyGoneStillEnds()
{
    GuildRunAdoptFacts f = Run450Back();
    f.tankInFinderGroup = false;
    Check("the group disbanded: ended", GuildRunAdoptNext(f) == GuildRunAdoptStep::Release);
    Check("and says so", std::string(GuildRunAdoptWhy(f)).find("group") != std::string::npos);

    f = Run450Back();
    f.membersBack = 0;
    Check("everybody back in the world and nobody inside: ended",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Release);
    Check("and says so", std::string(GuildRunAdoptWhy(f)).find("inside") != std::string::npos);

    GuildRunAdoptFacts gone;
    gone.wasInside = true;
    gone.expired = true;
    Check("nobody back by the end of the window: ended",
          GuildRunAdoptNext(gone) == GuildRunAdoptStep::Release);
    gone.membersInWorld = 4;
    gone.membersBack = 4;
    Check("four back but never the tank: ended",
          GuildRunAdoptNext(gone) == GuildRunAdoptStep::Release);
    Check("and says the tank", std::string(GuildRunAdoptWhy(gone)).find("tank") != std::string::npos);

    f = Run450Back();
    f.membersBack = 0;
    f.membersInWorld = 3;
    f.expired = true;
    Check("the window over with the group but nobody inside: ended",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Release);

    f = Run450Back();
    f.wasInside = false;
    Check("a run still queueing is ended: the finder's queue does not outlive a restart",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Release);

    f = Run450Back();
    f.liveRunGroup = true;
    Check("the tank already in a run of this process: the old one is ended",
          GuildRunAdoptNext(f) == GuildRunAdoptStep::Release);
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

    // The restart read takes the row over before the expiry sweep can see it
    // as somebody else's: same poll, the guild runs first.
    std::size_t const drive = source.find("        DriveGuildFinderRuns();\n");
    std::size_t const expire = source.find("        ExpireAbandonedClaims();\n");
    Check("the guild runs are driven before abandoned claims are expired",
          drive != std::string::npos && expire != std::string::npos && drive < expire);

    std::string const read = Between(source, "    void DriveGuildRunStrays(std::time_t now)",
                                     "    // HOW OFTEN THE LAST RUNG WAS NEEDED TODAY");
    Check("the restart read is found", !read.empty());
    Check("it reads the row's last word",
          read.find("ReadGuildRunResume(") != std::string::npos);
    Check("a run that was inside is claimed under this worldserver's token",
          read.find("SET claimed_by = '{}', updated_at = NOW()") != std::string::npos);
    Check("and the claim is read back before it is believed",
          read.find("SELECT claimed_by FROM overseer_command WHERE id = {}") != std::string::npos);

    std::string const adopt = Between(source, "    void DriveGuildRunAdoptions(std::time_t now)",
                                      "    void DriveGuildRunStrays(std::time_t now)");
    Check("the adoption drive is found", !adopt.empty());
    Check("it decides with the pure rule",
          adopt.find("OverseerDecisions::GuildRunAdoptNext(facts)") != std::string::npos);
    Check("an adopted run is driven like any other",
          adopt.find("_guildRuns.push_back(") != std::string::npos);
    Check("its tank leads again, as when the run was formed",
          adopt.find("SetMaster(tank)") != std::string::npos);
    Check("the deploy proof is said", adopt.find("is adopted after the restart") != std::string::npos);
    Check("a run that is really gone has its group cleaned up as before",
          adopt.find("_guildRunStrays.push_back(") != std::string::npos);
    Check("nobody is moved or raised by the adoption",
          adopt.find("TeleportTo") == std::string::npos &&
              adopt.find("ResurrectPlayer") == std::string::npos);

    Check("the strays are driven with the adoptions",
          Between(source, "    void DriveGuildFinderRuns()", "    static Group* RunGroup(")
                  .find("DriveGuildRunAdoptions(now);") != std::string::npos);
}

}  // namespace

int main()
{
    TheRowSaysWhereTheRunWas();
    ARestartMidRunKeepsTheRun();
    ARunThatIsReallyGoneStillEnds();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
