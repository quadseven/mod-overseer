/*
 * The crossing's `fetch` step, as a sentence. The step ends the attempt for a
 * family still split at the berth after the bounded wait; the sentence said
 * the leader goes back for a follower held past the foot limit (#1598).
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

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

CrossingWorld Docks()
{
    CrossingWorld w;
    w.originMap = 1;
    w.destinationMap = 0;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.mooringKnown = true;
    return w;
}

CrossingLimits Limits()
{
    CrossingLimits l;
    l.berthArrivedYards = 12.f;
    l.gatherYards = 30.f;
    l.minBoardDwellMs = 15000;
    l.fetchPastYards = 1500.f;
    l.fetchWaitSeconds = 300;
    return l;
}

CrossingMember Member(bool leader, float berthYards, float leaderYards)
{
    CrossingMember m;
    m.readable = true;
    m.isLeader = leader;
    m.mapId = 1;
    m.berthDistance = berthYards;
    m.leaderDistance = leaderYards;
    return m;
}

// One follower 900 yards out, held by nothing, after the wait: within the foot
// limit, no flight asked about, and the leader is not sent back.
void TheSentenceSaysWhatTheStepDoes()
{
    CrossingWorld w = Docks();
    w.leaderWaitSeconds = 301;
    std::vector<CrossingMember> const members = {
        Member(true, 3.f, 0.f), Member(false, 5.f, 4.f), Member(false, 900.f, 900.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    Check("the step is a fetch", step.action == CrossingAction::Fetch);
    std::string const said = CrossingExplanation(step, w);

    Check("no foot limit is claimed", !Has(said, "foot limit"));
    Check("no flight is claimed", !Has(said, "flight"));
    Check("the leader is not said to go back", !Has(said, "goes back"));

    Check("it says how long the leader waited", Has(said, "301s"));
    Check("it counts who is still not gathered", Has(said, "1 member(s)"));
    Check("it names the origin map", Has(said, "map 1"));
    Check("it says the attempt ends", Has(said, "this attempt ends"));
    Check("for a split family", Has(said, "split"));
    Check("that regroups first", Has(said, "regroups before the next crossing"));
}

// Two still out: the count follows the step.
void TheCountFollowsTheStep()
{
    CrossingWorld w = Docks();
    w.leaderWaitSeconds = 400;
    std::vector<CrossingMember> const members = {
        Member(true, 3.f, 0.f), Member(false, 900.f, 900.f), Member(false, 2448.f, 2448.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    Check("still a fetch", step.action == CrossingAction::Fetch);
    std::string const said = CrossingExplanation(step, w);
    Check("two are counted", Has(said, "2 member(s)"));
    Check("and the wait", Has(said, "400s"));
}

// Before the wait is up the same family holds, with the hold's own sentence.
void BeforeTheWaitItHolds()
{
    CrossingWorld w = Docks();
    w.leaderWaitSeconds = 120;
    std::vector<CrossingMember> const members = {
        Member(true, 3.f, 0.f), Member(false, 900.f, 900.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    Check("a hold, not a fetch", step.action == CrossingAction::Hold);
    Check("the hold's sentence is its own",
          Has(CrossingExplanation(step, w), "waits for"));
}

} // namespace

int main()
{
    TheSentenceSaysWhatTheStepDoes();
    TheCountFollowsTheStep();
    BeforeTheWaitItHolds();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
