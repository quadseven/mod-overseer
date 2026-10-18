/*
 * A worldserver roll gets its runs' people back (wow-overseer#1192).
 *
 * The live failure (dev realm, 19 worldserver starts, 2026-10-10 to 10-18),
 * with every layer of wow-overseer#816 on main:
 *
 *   1. 39 adopted guild runs ended "lost at the restart: its tank was not back
 *      in the world within a quarter hour", each after the full wait: nothing
 *      asked for the tank, and the random login cycle did not pick it.
 *   2. At 4 of 5 starts with a family run inside, the run ended `client_lost`
 *      383 to 511 s after the start: the 300 s leader-client window ran out
 *      before the stream clients were back.
 *
 * This file pins the two answers: a waiting adoption asks the bot manager for
 * every member not in the world, the tank first and never a family character,
 * and the leader-client hold is not turned into an evacuation inside the
 * startup hold CloseAbandonedRuns already has.
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
#include <vector>

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

GuildRunAdoptLoginMember Member(char const* name, bool tank = false)
{
    GuildRunAdoptLoginMember m;
    m.name = name;
    m.tank = tank;
    return m;
}

// The 2026-10-18 17:55:14 roll: three runs held with the tank and one or two
// others still out of the world.
void AHeldRunAsksForItsMissingMembers()
{
    std::vector<GuildRunAdoptLoginMember> run{Member("Tankard", true), Member("Mender"),
                                              Member("Stabby"), Member("Zappy"),
                                              Member("Shooty")};
    run[1].inWorld = true;
    run[2].inWorld = true;
    run[3].inWorld = true;
    std::vector<std::string> const ask = GuildRunAdoptLogins(run, true);
    Check("the tank and the other missing member are asked for", ask.size() == 2);
    Check("the tank first", !ask.empty() && ask[0] == "Tankard");
    Check("then the damage dealer", ask.size() == 2 && ask[1] == "Shooty");

    // The tank listed last still comes first.
    std::vector<GuildRunAdoptLoginMember> reordered{Member("Mender"), Member("Tankard", true)};
    std::vector<std::string> const first = GuildRunAdoptLogins(reordered, true);
    Check("the tank is asked for first wherever it is listed",
          first.size() == 2 && first[0] == "Tankard" && first[1] == "Mender");

    // The late healer of run 1019998: the tank is back, the healer is not.
    std::vector<GuildRunAdoptLoginMember> healer{Member("Tankard", true), Member("Mender")};
    healer[0].inWorld = true;
    std::vector<std::string> const late = GuildRunAdoptLogins(healer, true);
    Check("a missing healer is asked for too", late.size() == 1 && late[0] == "Mender");
}

void NobodyIsAskedForTwiceAtOnce()
{
    std::vector<GuildRunAdoptLoginMember> run{Member("Tankard", true)};
    run[0].secondsSinceAsked = 5;
    Check("asked a moment ago: not again yet", GuildRunAdoptLogins(run, true).empty());
    run[0].secondsSinceAsked = GUILD_RUN_ADOPT_LOGIN_RETRY_SECONDS;
    Check("still not there a minute later: asked again", GuildRunAdoptLogins(run, true).size() == 1);
    run[0].secondsSinceAsked = -1;
    run[0].loading = true;
    Check("one already on its way in is not asked for", GuildRunAdoptLogins(run, true).empty());
    run[0].loading = false;
    run[0].inWorld = true;
    Check("one in the world is not asked for", GuildRunAdoptLogins(run, true).empty());
}

void AFamilyCharacterIsNeverLoggedInWithoutItsClient()
{
    std::vector<GuildRunAdoptLoginMember> run{Member("Tankard", true), Member("Kinsman")};
    run[1].familyCharacter = true;
    std::vector<std::string> const ask = GuildRunAdoptLogins(run, true);
    Check("the family character is left out", ask.size() == 1 && ask[0] == "Tankard");
    Check("nobody is asked for while the roster is unread",
          GuildRunAdoptLogins(run, false).empty());

    std::vector<GuildRunAdoptLoginMember> client{Member("Tankard", true)};
    client[0].clientComing = true;
    Check("a member whose client is logging in is left to it",
          GuildRunAdoptLogins(client, true).empty());
    std::vector<GuildRunAdoptLoginMember> unnamed{Member("", true)};
    Check("an empty seat is never asked for", GuildRunAdoptLogins(unnamed, true).empty());
}

// The 2026-10-15 15:57 case: the first poll finds the leader client gone at
// the start, and the run ended `client_lost` a little over five minutes later.
void ARollGivesTheLeaderClientTheStartupHold()
{
    std::time_t const start = 100000;
    std::time_t const lost = start + 20;
    auto at = [&](std::time_t now)
    {
        return DungeonLeaderClientLossAfterStart(false, true, lost, now,
                                                 static_cast<std::int64_t>(now - start));
    };
    Check("383 s after the start the run is held, not evacuated",
          at(start + 383) == LeaderClientLossAction::Hold);
    Check("511 s after the start the run is still held",
          at(start + 511) == LeaderClientLossAction::Hold);
    Check("just inside ten minutes it is still held",
          at(start + RUN_CLOSE_STARTUP_HOLD_SECONDS - 1) == LeaderClientLossAction::Hold);
    Check("past the startup hold a client still gone is evacuated as before",
          at(start + RUN_CLOSE_STARTUP_HOLD_SECONDS) == LeaderClientLossAction::Abandon);
    Check("a client back inside the hold goes on",
          DungeonLeaderClientLossAfterStart(true, true, 0, start + 400, 400) ==
              LeaderClientLossAction::Continue);

    // Long after a start the bound is the old one, to the second.
    std::time_t const later = start + 7200;
    Check("away from a start, 300 s lost is still held",
          DungeonLeaderClientLossAfterStart(false, true, later, later + 300, 7500) ==
              LeaderClientLossAction::Hold);
    Check("away from a start, 301 s lost is evacuated",
          DungeonLeaderClientLossAfterStart(false, true, later, later + 301, 7501) ==
              LeaderClientLossAction::Abandon);
    Check("a lost party issuer is held through the start too",
          DungeonLeaderClientLossAfterStart(true, false, lost, start + 400, 400) ==
              LeaderClientLossAction::Hold);
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

    std::string const ask = Between(source, "void AskGuildRunAdoptLogins(",
                                     "void DriveGuildRunAdoptions(std::time_t now)");
    Check("the login ask is found", !ask.empty());
    Check("it decides with the pure rule",
          Has(ask, "OverseerDecisions::GuildRunAdoptLogins(members, rosterKnown)"));
    Check("it reads who is family from the roster", Has(ask, "OnRoster(") &&
                                                        Has(ask, "g_rosterKnown"));
    Check("it asks the bot manager", Has(ask, "sRandomPlayerbotMgr.AddPlayerBot("));
    Check("it remembers when it asked", Has(ask, "adoption.loginAskedAt[name] = now;"));
    Check("the deploy proof is said", Has(ask, "is asked to log back in after the"));

    std::string const adoptions =
        Between(source, "void DriveGuildRunAdoptions(std::time_t now)", "void DriveGuildRunStrays");
    Check("the adoption drive is found", !adoptions.empty());
    std::string const wait = Between(adoptions, "GuildRunAdoptStep::Wait)",
                                     "GuildRunAdoptStep::Release)");
    Check("a waiting adoption asks for its members",
          Has(wait, "AskGuildRunAdoptLogins(adoption, names, now);"));

    Check("the leader-client check carries the startup hold",
          Has(source, "OverseerDecisions::DungeonLeaderClientLossAfterStart(") &&
              Has(source, "static_cast<std::int64_t>(clientNow) - g_startedAt"));
    Check("the old check without it is gone from the module",
          !Has(source, "OverseerDecisions::DungeonLeaderClientLoss("));
}

}  // namespace

int main()
{
    AHeldRunAsksForItsMissingMembers();
    NobodyIsAskedForTwiceAtOnce();
    AFamilyCharacterIsNeverLoggedInWithoutItsClient();
    ARollGivesTheLeaderClientTheStartupHold();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
