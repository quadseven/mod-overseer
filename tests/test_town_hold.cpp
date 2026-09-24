/*
 * Who may travel on its own while the family's campaign waits in town.
 *
 * Measured on the dev realm 2026-09-24, with both families' dungeon campaigns
 * held "town first" for bag room and the family put on the `quest` job for the
 * wait:
 *
 *   04:53:33 'Grug' now working quest 3881 (Expedition Salvation) - chosen by
 *            the council
 *   05:00:14 Grug crossed into Dustwallow.     (a flight out of Tanaris)
 *   05:06:53 'Grog' is 13450 yards behind 'Grug'
 *   05:23:20 Grug crossed into UngoroCrater.   (a second flight)
 *
 *   02:55:13 'Oz' is cut off from its leader with no errand of its own -
 *            granting `new rpg` ...            (and Uzza, Zork, Zrog)
 *   02:57:57 Oz crossed into Ashenvale.
 *   03:00:22 Zrog crossed into Mulgore.
 *
 * The first is the questing leader's grant, the second the cut-off follower's.
 * Both are right for a family that is questing and wrong for one waiting on a
 * vendor, so the bridge writes `town run` for the wait and these answers keep
 * everybody without an errand where the family is.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * src/mod_overseer.cpp to pin the three places that ask and the family read
 * they ask with.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::CutOffFollowerRoams;
using OverseerDecisions::FamilyHoldsInTown;
using OverseerDecisions::HoldsInTown;
using OverseerDecisions::LeaderCarriesNewRpg;
using OverseerDecisions::SplitFollowerDrivesItself;

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

void TheWaitIsTheTownRunJob()
{
    Check("town run", HoldsInTown("town run"), true);
    Check("the constant", HoldsInTown(OverseerDecisions::TOWN_HOLD_JOB), true);
    Check("quest", HoldsInTown("quest"), false);
    Check("empty", HoldsInTown(""), false);
    Check("a dungeon job", HoldsInTown("dungeon:ragefire"), false);
    Check("craft", HoldsInTown("craft"), false);
    Check("not a prefix", HoldsInTown("town"), false);
}

void AFamilyHalfMovedIntoTownWaitsInTown()
{
    // The job is written one row at a time. A family with one row on the wait
    // waits, whichever row it is, and a row not read yet is questing.
    Check("all five", FamilyHoldsInTown({"town run", "town run", "town run", "town run",
                                         "town run"}),
          true);
    Check("only the leader", FamilyHoldsInTown({"town run", "", "", "", ""}), true);
    Check("only a follower", FamilyHoldsInTown({"quest", "quest", "town run"}), true);
    Check("questing", FamilyHoldsInTown({"", "quest", "quest"}), false);
    Check("a dungeon", FamilyHoldsInTown({"dungeon:ragefire", "dungeon:ragefire"}), false);
    Check("nobody", FamilyHoldsInTown(std::vector<std::string>{}), false);
}

void AQuestingLeaderAlwaysCarriesIt()
{
    // Unchanged: the questing family's one traveller, errand or not. An empty
    // job is how a row off LoadJobs reads, and it is questing.
    Check("quest, no errand", LeaderCarriesNewRpg("quest", false), true);
    Check("quest, errand", LeaderCarriesNewRpg("quest", true), true);
    Check("unread job", LeaderCarriesNewRpg("", false), true);
    Check("dungeon, no errand", LeaderCarriesNewRpg("dungeon:zulfarrak", false), true);
}

void ALeaderInTownCarriesItOnlyToWalkAnErrand()
{
    Check("town, no errand", LeaderCarriesNewRpg("town run", false), false);
    Check("town, errand", LeaderCarriesNewRpg("town run", true), true);
}

void AQuestingCutOffFollowerRoamsAsBefore()
{
    // Exactly SplitFollowerDrivesItself for every non-town job.
    char const* const targets[] = {"", "vendor", "innkeeper", "profession trainer",
                                   "at:1:-610.9,-4320.0,39.7", "trigger:226", "3310"};
    for (char const* t : targets)
    {
        Check(t, CutOffFollowerRoams("quest", t), SplitFollowerDrivesItself(t));
        Check(t, CutOffFollowerRoams("", t), SplitFollowerDrivesItself(t));
    }
}

void ACutOffFollowerInTownStaysUnlessItHasAnErrand()
{
    Check("town, empty column", CutOffFollowerRoams("town run", ""), false);
    Check("town, a trainer", CutOffFollowerRoams("town run", "profession trainer"), true);
    Check("town, an innkeeper", CutOffFollowerRoams("town run", "innkeeper"), true);
    // Still refused, in town as anywhere: the family's own point aim.
    Check("town, a point aim", CutOffFollowerRoams("town run", "at:1:-610.9,-4320.0,39.7"),
          false);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheModuleAsksInTheThreePlaces()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    // The leader's grant, and its strip.
    Check("leader grant asks", source.find("OverseerDecisions::LeaderCarriesNewRpg(") !=
                                   std::string::npos,
          true);
    Check("leader grant is gated",
          source.find("if (mayCarry && !leaderAI->HasStrategy(\"new rpg\"") != std::string::npos,
          true);
    Check("leader strip", source.find("if (!mayCarry && leaderAI->HasStrategy(\"new rpg\"") !=
                              std::string::npos,
          true);
    // The cut-off follower's grant, and MaySteerItself's take-back.
    Check("cut-off grant is gated", source.find("if (cutOffRoams)") != std::string::npos, true);
    Check("MaySteerItself asks",
          source.find("OverseerDecisions::CutOffFollowerRoams(TownJobFor(name),") !=
              std::string::npos,
          true);
    // The job they read is the family's, off the roster on this very poll,
    // before the first grant - not a cache another drive fills later, which
    // is empty for the first poll after a restart.
    std::size_t const read = source.find("OverseerDecisions::FamilyHoldsInTown(familyJobs)");
    std::size_t const grant = source.find("OverseerDecisions::LeaderCarriesNewRpg(");
    Check("the family is read", read != std::string::npos, true);
    Check("read before the leader's grant", read != std::string::npos && read < grant, true);
    Check("read off the roster",
          source.find("std::map<std::string, std::string> const jobs = LoadJobs();") !=
              std::string::npos,
          true);
}

}  // namespace

int main()
{
    TheWaitIsTheTownRunJob();
    AFamilyHalfMovedIntoTownWaitsInTown();
    AQuestingLeaderAlwaysCarriesIt();
    ALeaderInTownCarriesItOnlyToWalkAnErrand();
    AQuestingCutOffFollowerRoamsAsBefore();
    ACutOffFollowerInTownStaysUnlessItHasAnErrand();
    TheModuleAsksInTheThreePlaces();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a family waiting in town stays in town\n");
    return EXIT_SUCCESS;
}
