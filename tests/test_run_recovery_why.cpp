/*
 * The run recovery heuristic's reason clause names the branch that chose the
 * recovery, not only the recovery word. Three paths gave a cause the facts in
 * the same row contradicted: the least-tried fallback, the berth split, and
 * the ladder's turn when nothing in the facts points anywhere.
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool condition)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

bool Has(std::string const& text, char const* piece)
{
    return text.find(piece) != std::string::npos;
}

std::string WhyOf(RunFailureFacts const& f)
{
    return RunRecoveryHeuristicWhy(f, RunRecoveryHeuristic(f));
}

void TheFallbackSaysWhatFailedTwice()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.everyoneSteerable = false;
    f.leaderYardsFromStaging = 5.f;
    f.tried = {RunRecovery::WaitForClient, RunRecovery::WaitForClient};
    Check("the wait failed twice, so the ladder restages",
          RunRecoveryHeuristic(f) == RunRecovery::RestageNearer);
    std::string const why = WhyOf(f);
    Check("the fallback does not claim a far leader", !Has(why, "far from the door"));
    Check("it names the recovery the facts pointed at", Has(why, "wait_for_client"));
    Check("it says that one failed twice running", Has(why, "twice running"));
    Check("it says the chosen one is the least tried", Has(why, "least tried"));
}

void TheBerthSplitIsNotCalledASpread()
{
    RunFailureFacts f;
    f.outcome = "split_failed";
    f.reason = "the family is split at the berth";
    f.hearthRegroupReady = true;
    f.farthestMemberYards = 40.f;
    Check("a berth split meets at the inn",
          RunRecoveryHeuristic(f) == RunRecovery::HearthRegroup);
    std::string const why = WhyOf(f);
    Check("a family 40 yards apart is not spread across zones",
          !Has(why, "spread across zones"));
    Check("the clause names the berth", Has(why, "split at the berth"));
    Check("and the inn it meets at", Has(why, "inn"));
}

void TheLaddersTurnIsCalledTheLaddersTurn()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.leaderYardsFromStaging = 5.f;
    f.farthestMemberYards = 10.f;
    Check("nothing in the facts: the ladder starts at the restage",
          RunRecoveryHeuristic(f) == RunRecovery::RestageNearer);
    std::string why = WhyOf(f);
    Check("a leader 5 yards out is not called far", !Has(why, "far from the door"));
    Check("the clause says the ladder took its turn", Has(why, "ladder"));

    // The next rung of the same ladder is a regroup, with nobody far.
    f.tried = {RunRecovery::RestageNearer};
    Check("the second turn regroups", RunRecoveryHeuristic(f) == RunRecovery::Regroup);
    why = WhyOf(f);
    Check("a member 10 yards out is not called far", !Has(why, "far from the leader"));
    Check("the second turn is the ladder's too", Has(why, "ladder"));
}

// Every path the facts do point down keeps its clause, word for word.
void ThePointedPathsKeepTheirClauses()
{
    RunFailureFacts far;
    far.outcome = "staging_failed";
    far.leaderYardsFromStaging = 2000.f;
    Check("a far leader", WhyOf(far) == "the leader was far from the door when the staging clock ran");

    RunFailureFacts straggler;
    straggler.outcome = "staging_failed";
    straggler.leaderYardsFromStaging = 5.f;
    straggler.farthestMemberYards = 400.f;
    Check("a straggler", WhyOf(straggler) == "a member was far from the leader");

    RunFailureFacts apart;
    apart.outcome = "split_failed";
    Check("a split", WhyOf(apart) == "the party was apart (another map or another copy)");

    RunFailureFacts reset;
    reset.outcome = "reset_failed";
    Check("a refused reset", WhyOf(reset) == "the instance reset did not take");

    RunFailureFacts client;
    client.outcome = "staging_failed";
    client.everyoneSteerable = false;
    Check("no client", WhyOf(client) == "a member has no steerable client");

    RunFailureFacts bags;
    bags.outcome = "staging_failed";
    bags.bagsFull = true;
    Check("full bags",
          WhyOf(bags) == "a member has no bag room, and the town trip runs only from IDLE");

    RunFailureFacts spread;
    spread.outcome = "staging_failed";
    spread.hearthRegroupReady = true;
    spread.farthestMemberYards = 4289.f;
    Check("a spread family", Has(WhyOf(spread), "spread across zones"));

    RunFailureFacts ledge;
    ledge.outcome = "staging_failed";
    ledge.leaderYardsFromStaging = 5.f;
    ledge.stagingRearms = 2;
    Check("a failed approach", Has(WhyOf(ledge), "the approach itself failed"));

    // A pointed recovery tried once still carries its count.
    far.tried = {RunRecovery::RestageNearer};
    Check("the tried count stays",
          WhyOf(far) == "the leader was far from the door when the staging clock ran; "
                        "tried 1 time(s) already this streak");
}

// A word the heuristic did not choose (the bridge's answer) reads as before.
void AnotherWordReadsAsBefore()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.leaderYardsFromStaging = 5.f;
    Check("a word that is not the heuristic's own keeps the word's clause",
          RunRecoveryHeuristicWhy(f, RunRecovery::Regroup) == "a member was far from the leader");
}

} // namespace

int main()
{
    TheFallbackSaysWhatFailedTwice();
    TheBerthSplitIsNotCalledASpread();
    TheLaddersTurnIsCalledTheLaddersTurn();
    ThePointedPathsKeepTheirClauses();
    AnotherWordReadsAsBefore();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
