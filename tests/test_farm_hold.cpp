/*
 * A farming family stays in its field between hops (#1521).
 *
 * The overseer app's farm drive walks the family's leader from one surveyed
 * herb or ore spawn to the next, one ground aim per hop, and each aim ends on
 * arrival. Between two hops the leader has no aim. On a questing family that is
 * the moment the quest drive picks the next quest; on a farming family the quest
 * drive is stood down, and an idle leader that keeps `new rpg` is handed one of
 * upstream's own statuses instead: a wander, a far grind spot, or a flight when
 * a flight master is near. That is the scatter the `town run` hold already
 * stops for a family waiting in town, so a farming family is held the same way.
 *
 * DoJob must also accept the order that names what to farm: `farm:<item
 * entry>` or `farm:<mining|herbalism>`.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * src/mod_overseer.cpp to pin DoJob's acceptance.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::CounterArrival;
using OverseerDecisions::CutOffFollowerRoams;
using OverseerDecisions::FamilyHoldsInTown;
using OverseerDecisions::IsFarmJob;
using OverseerDecisions::LeaderCarriesNewRpg;
using OverseerDecisions::TakesNewRpgOffOnArrival;

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

void TheFarmOrdersAreFarmJobs()
{
    Check("bare farm", IsFarmJob("farm"), true);
    Check("Dreamfoil by entry", IsFarmJob("farm:13463"), true);
    Check("one digit", IsFarmJob("farm:7"), true);
    Check("nine digits", IsFarmJob("farm:123456789"), true);
    Check("mining", IsFarmJob("farm:mining"), true);
    Check("herbalism", IsFarmJob("farm:herbalism"), true);
}

void AnythingElseIsNot()
{
    Check("empty", IsFarmJob(""), false);
    Check("quest", IsFarmJob("quest"), false);
    Check("town run", IsFarmJob("town run"), false);
    Check("a dungeon job", IsFarmJob("dungeon:ragefire"), false);
    Check("no suffix", IsFarmJob("farm:"), false);
    Check("zero", IsFarmJob("farm:0"), false);
    Check("leading zero", IsFarmJob("farm:013463"), false);
    Check("ten digits", IsFarmJob("farm:1234567890"), false);
    Check("a name", IsFarmJob("farm:dreamfoil"), false);
    Check("skinning has no nodes", IsFarmJob("farm:skinning"), false);
    Check("upper case", IsFarmJob("farm:Mining"), false);
    Check("a sign", IsFarmJob("farm:-1"), false);
    Check("trailing text", IsFarmJob("farm:13463 "), false);
    Check("not a prefix", IsFarmJob("farmer"), false);
    Check("farming", IsFarmJob("farming"), false);
}

void AFamilyWithAFarmRowIsHeld()
{
    Check("all five", FamilyHoldsInTown({"farm:13463", "farm:13463", "farm:13463",
                                         "farm:13463", "farm:13463"}),
          true);
    Check("half moved", FamilyHoldsInTown({"farm:mining", "quest", "quest"}), true);
    Check("bare farm", FamilyHoldsInTown({"farm"}), true);
    Check("still questing", FamilyHoldsInTown({"quest", "quest"}), false);
    Check("a malformed order is not a hold", FamilyHoldsInTown({"farm:"}), false);
}

void AFarmingLeaderCarriesItOnlyToWalkAHop()
{
    // The party poll hands the family's hold to these answers as the town job
    // (TownJobFor), so the farming family is asked with that job.
    std::string const held = OverseerDecisions::TOWN_HOLD_JOB;
    Check("between hops", LeaderCarriesNewRpg(held, false), false);
    Check("walking a hop", LeaderCarriesNewRpg(held, true), true);
    Check("a hop lands", TakesNewRpgOffOnArrival(CounterArrival::Done, held), true);
    Check("a cut-off follower stays", CutOffFollowerRoams(held, ""), false);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void DoJobAcceptsTheFarmOrder()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::size_t const asks = source.find("bool const knownFarm = OverseerDecisions::IsFarmJob(mode);");
    std::size_t const gate =
        source.find("if (!knownMode && !knownDungeon && !knownRaid && !knownFarm)");
    Check("DoJob asks IsFarmJob", asks != std::string::npos, true);
    Check("DoJob's gate admits it", gate != std::string::npos, true);
    Check("asked before the gate", asks != std::string::npos && gate != std::string::npos &&
                                       asks < gate,
          true);
}

} // namespace

int main()
{
    TheFarmOrdersAreFarmJobs();
    AnythingElseIsNot();
    AFamilyWithAFarmRowIsHeld();
    AFarmingLeaderCarriesItOnlyToWalkAHop();
    DoJobAcceptsTheFarmOrder();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_farm_hold: all checks passed\n");
    return 0;
}
