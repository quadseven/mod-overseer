/*
 * The facts line Jev is asked with carries every input the heuristic reads.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * WHY. Over 24 hours on the dev realm Jev's answer was acted on in 36% of
 * run_recovery calls and 11% of staging_stall calls, against a goal of 60%.
 * Jev answered with low confidence, so the module's heuristic stood almost
 * every time, and the heuristic was reading facts the line sent to Jev did not
 * carry: whether a member is held off a lethal leg, the recovery streak, where
 * the summon rung opens, whether a stone and a summoner are ready, whether the
 * dungeon finder is ready. A judge cannot be compared fairly with a heuristic
 * that sees more than it does.
 *
 * HOW. For each input the heuristic reads, two facts that differ only in that
 * input must give two different lines: if they gave the same line, Jev could
 * not tell the two cases apart while the heuristic could. A new input to
 * RunRecoveryHeuristic or StagingStallHeuristic belongs in a row here.
 *
 * The outcome and the reason are not in the facts line: the request row's
 * failure column carries them ("outcome: reason"), and the bridge sends that
 * column to Jev as well.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <functional>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void CheckDiffers(char const* what, std::string const& a, std::string const& b)
{
    if (a != b)
        return;
    std::printf("FAIL %s: both facts give the same line: %s\n", what, a.c_str());
    ++failures;
}

void CheckHas(char const* what, std::string const& line, std::string const& part)
{
    if (line.find(part) != std::string::npos)
        return;
    std::printf("FAIL %s: '%s' is not in: %s\n", what, part.c_str(), line.c_str());
    ++failures;
}

RunFailureFacts BaseFailure()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.reason = "the staging clock ran out";
    f.leaderYardsFromStaging = 400.f;
    f.farthestMemberYards = 60.f;
    return f;
}

void RecoveryInput(char const* what, std::function<void(RunFailureFacts&)> const& change)
{
    RunFailureFacts const before = BaseFailure();
    RunFailureFacts after = before;
    change(after);
    CheckDiffers(what, RunRecoveryFactsLine(before), RunRecoveryFactsLine(after));
}

void TestEveryRecoveryHeuristicInputIsInTheLine()
{
    RecoveryInput("bagsFull", [](RunFailureFacts& f) { f.bagsFull = true; });
    RecoveryInput("everyoneSteerable", [](RunFailureFacts& f) { f.everyoneSteerable = false; });
    RecoveryInput("leaderYardsFromStaging",
                  [](RunFailureFacts& f) { f.leaderYardsFromStaging = 1800.f; });
    RecoveryInput("leaderYardsFromStaging unmeasured",
                  [](RunFailureFacts& f) { f.leaderYardsFromStaging = -1.f; });
    RecoveryInput("farthestMemberYards", [](RunFailureFacts& f) { f.farthestMemberYards = 900.f; });
    RecoveryInput("membersApart", [](RunFailureFacts& f) { f.membersApart = 2; });
    RecoveryInput("stagingRearms", [](RunFailureFacts& f) { f.stagingRearms = 4; });
    RecoveryInput("tried", [](RunFailureFacts& f) { f.tried = {RunRecovery::Regroup}; });
    // TriedTwiceRunning: once and twice must read differently.
    {
        RunFailureFacts once = BaseFailure();
        once.tried = {RunRecovery::Regroup};
        RunFailureFacts twice = once;
        twice.tried.push_back(RunRecovery::Regroup);
        CheckDiffers("tried twice", RunRecoveryFactsLine(once), RunRecoveryFactsLine(twice));
    }
    // Each of these was read by the heuristic and missing from the line.
    RecoveryInput("hearthRegroupReady", [](RunFailureFacts& f) { f.hearthRegroupReady = true; });
    RecoveryInput("memberHeldOffLethalLeg",
                  [](RunFailureFacts& f) { f.memberHeldOffLethalLeg = true; });
    RecoveryInput("streak", [](RunFailureFacts& f) { f.streak = 3; });
    RecoveryInput("summonAfter", [](RunFailureFacts& f) { f.summonAfter = 3; });
    RecoveryInput("summonReady", [](RunFailureFacts& f) { f.summonReady = true; });
    RecoveryInput("summonerReady", [](RunFailureFacts& f) { f.summonerReady = true; });
    RecoveryInput("dungeonFinderReady", [](RunFailureFacts& f) { f.dungeonFinderReady = true; });
    RecoveryInput("finderIsDefault", [](RunFailureFacts& f) { f.finderIsDefault = true; });
}

void TestTheRecoveryLineSaysTheLadderInWords()
{
    RunFailureFacts f = BaseFailure();
    f.streak = 4;
    f.summonAfter = 3;
    f.summonReady = true;
    f.summonerReady = false;
    f.memberHeldOffLethalLeg = true;
    f.hearthRegroupReady = true;
    f.hearthRegroupNote = "four of five are bound at one inn";
    std::string const line = RunRecoveryFactsLine(f);
    CheckHas("old clauses kept", line, "leader 400y from the staging point");
    CheckHas("old clauses kept", line, "farthest member 60y from the leader");
    CheckHas("lethal leg", line, "a member is held off a lethal leg");
    CheckHas("hearth", line, "hearth regroup ready");
    CheckHas("hearth note kept", line, "four of five are bound at one inn");
    CheckHas("streak", line, "attempt 4 in a row failed, summon rung opens at 3");
    CheckHas("stone and summoner", line, "meeting stone usable, summoner not ready");
    CheckHas("finder", line, "dungeon finder not ready");

    RunFailureFacts off = BaseFailure();
    CheckHas("summon rung off", RunRecoveryFactsLine(off), "summon rung off");
    CheckHas("no lethal leg", RunRecoveryFactsLine(off), "no member held off a lethal leg");
}

StagingStallFacts BaseStall()
{
    StagingStallFacts s;
    s.rearms = 3;
    s.lastEndedBy = "travel errand released";
    s.leaderYards = 240.f;
    return s;
}

ApproachGap Gap(float horizontal, float vertical)
{
    ApproachGap g;
    g.horizontalYards = horizontal;
    g.verticalYards = vertical;
    g.measured = true;
    return g;
}

void StallInput(char const* what, std::function<void(StagingStallFacts&)> const& change)
{
    StagingStallFacts const before = BaseStall();
    StagingStallFacts after = before;
    change(after);
    ApproachGap const gap = Gap(240.f, 0.f);
    CheckDiffers(what, StagingStallFactsLine(before, gap, 90, 600),
                 StagingStallFactsLine(after, gap, 90, 600));
}

void TestEveryStallHeuristicInputIsInTheLine()
{
    StallInput("rearms", [](StagingStallFacts& s) { s.rearms = 6; });
    StallInput("lastEndedBy", [](StagingStallFacts& s) { s.lastEndedBy = "a mail run"; });
    StallInput("lastEndedBy empty", [](StagingStallFacts& s) { s.lastEndedBy.clear(); });
    StallInput("endedFromOutside", [](StagingStallFacts& s) { s.endedFromOutside = true; });
    StallInput("leaderYards", [](StagingStallFacts& s) { s.leaderYards = 1400.f; });
    StallInput("leaderYards unmeasured", [](StagingStallFacts& s) { s.leaderYards = -1.f; });
}

void TestTheStallLineCarriesTheStagingGeometry()
{
    StagingStallFacts s = BaseStall();
    s.leaderYards = 1400.f;
    s.endedFromOutside = true;
    std::string const above = StagingStallFactsLine(s, Gap(1400.f, 35.f), 312, 600);
    CheckHas("rearms", above, "rearms 3");
    CheckHas("ended by", above, "last ended by travel errand released (outside this module)");
    CheckHas("distance and vertical", above, "leader 1400y out and 35y above it");
    CheckHas("elapsed of backstop", above, "staging clock 312s of 600s");

    std::string const below = StagingStallFactsLine(s, Gap(1400.f, -20.f), 312, 600);
    CheckHas("below", below, "leader 1400y out and 20y below it");

    s.endedFromOutside = false;
    CheckHas("inside", StagingStallFactsLine(s, Gap(1400.f, 0.f), 0, 600),
             "(inside this module)");

    // A clock read before it started is said as zero, never negative.
    CheckHas("no negative clock", StagingStallFactsLine(s, Gap(1400.f, 0.f), -5, 600),
             "staging clock 0s of 600s");

    StagingStallFacts unmeasured = BaseStall();
    unmeasured.leaderYards = -1.f;
    ApproachGap none;
    CheckHas("no reading", StagingStallFactsLine(unmeasured, none, 0, 600), "leader no reading");
}

} // namespace

int main()
{
    TestEveryRecoveryHeuristicInputIsInTheLine();
    TestTheRecoveryLineSaysTheLadderInWords();
    TestEveryStallHeuristicInputIsInTheLine();
    TestTheStallLineCarriesTheStagingGeometry();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
