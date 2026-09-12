/*
 * The family cannot use the flight network because they have never learned
 * most of its nodes (#388). Discovery has existed since #93, and it is a
 * no-op in practice because it only ever runs opportunistically - when some
 * OTHER errand happens to end beside a flight master. This pins the pure
 * layer a DELIBERATE errand needs: an aim that names a specific node rather
 * than "the nearest flight master", and the same three-way arrival decision
 * #378/#379 already established for a counter, applied here because standing
 * within the loose travel-arrived radius of a flight master's spawn row is
 * not the same thing as being close enough for the transaction to land.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::FlightDiscoveryArrival;
using OverseerDecisions::FlightDiscoveryArrivalStep;
using OverseerDecisions::FlightMasterAnswersForNode;
using OverseerDecisions::FlightMasterNodeAim;
using OverseerDecisions::ParseFlightMasterNodeAim;

namespace
{

int failures = 0;

void CheckParse(char const* aim, bool expectOk, uint32_t expectNode = 0)
{
    uint32_t node = 0xDEADBEEF;
    bool const ok = ParseFlightMasterNodeAim(aim, node);
    if (ok != expectOk)
    {
        std::printf("FAIL ParseFlightMasterNodeAim('%s'): expected %s, got %s\n", aim,
                    expectOk ? "ok" : "refused", ok ? "ok" : "refused");
        ++failures;
        return;
    }
    if (ok && node != expectNode)
    {
        std::printf("FAIL ParseFlightMasterNodeAim('%s'): expected node %u, got %u\n", aim,
                    expectNode, node);
        ++failures;
    }
}

// THE KEYWORD TABLE'S OWN PRECEDENT: A PREFIXED AIM IS A WHOLE KEYWORD PLUS A
// VALUE, NOT A LOOSE PREFIX MATCH. `at:` and `trigger:` both refuse anything
// that is not their own exact prefix followed by a well-formed value, and
// this follows the same discipline - including refusing the BARE "flight
// master" keyword, which is a different, already-existing errand meaning
// "nearest" and must keep meaning that.
void OnlyAWellFormedNodeAimParses()
{
    CheckParse("flight master:32", true, 32);
    CheckParse("flight master:1", true, 1);
    CheckParse("flight master:4294967295", true, 4294967295u);

    CheckParse("flight master", false);       // the existing "nearest" keyword
    CheckParse("flight master:", false);      // no digits at all
    CheckParse("flight master:0", false);     // node 0 names no DBC row
    CheckParse("flight master:-1", false);    // a sign is not a digit
    CheckParse("flight master:3.2", false);   // not an integer
    CheckParse("flight master:32 ", false);   // trailing garbage
    CheckParse("flight master:99999999999", false);  // does not fit a uint32
    CheckParse("flightmaster:32", false);     // not the exact keyword
    CheckParse("Flight Master:32", false);    // case matters, as everywhere else
    CheckParse("vendor", false);
    CheckParse("", false);
    CheckParse("at:1:-753.6,-2212.8,14.2", false);
    CheckParse("trigger:78", false);
}

// AND THE AIM IT BUILDS PARSES BACK TO THE SAME NODE, so the refusal line
// that will carry this string and the parser that will read a real errand
// back off `travel_npc` are reading and writing the identical format rather
// than two hand-typed copies of it.
void TheBuiltAimRoundTrips()
{
    for (uint32_t node : {1u, 32u, 37u, 80u, 168u, 999999u})
    {
        std::string const aim = FlightMasterNodeAim(node);
        uint32_t parsed = 0;
        if (!ParseFlightMasterNodeAim(aim, parsed) || parsed != node)
        {
            std::printf("FAIL FlightMasterNodeAim(%u) = '%s' did not round-trip\n", node,
                        aim.c_str());
            ++failures;
        }
    }
}

// THE SAME AGREEMENT CONSIDERFLIGHT ALREADY REQUIRES (TRAVEL_FLIGHT_NODE_-
// MATCH_YARDS). Exact same point is always in reach regardless of how tight
// the radius is; a point on the boundary is inclusive (<=, matching the
// adapter's existing `<=` at both of its call sites); anything further out
// is refused.
void TheNodeMatchIsTheSameAgreementConsiderFlightUses()
{
    if (!FlightMasterAnswersForNode(100.f, 200.f, 100.f, 200.f, 100.f))
    {
        std::printf("FAIL: a flight master standing exactly on the node answers for it\n");
        ++failures;
    }
    if (!FlightMasterAnswersForNode(100.f, 200.f, 100.f, 300.f, 100.f))
    {
        std::printf("FAIL: exactly at the match radius should still answer (<=)\n");
        ++failures;
    }
    if (FlightMasterAnswersForNode(100.f, 200.f, 100.f, 301.f, 100.f))
    {
        std::printf("FAIL: one yard past the match radius must not answer\n");
        ++failures;
    }
    // A contested zone: the nearest flight master to the character and the
    // node's own DBC position are two different places. This is the exact
    // fault #388 says the opportunistic discovery path never checks.
    if (FlightMasterAnswersForNode(0.f, 0.f, 5000.f, 5000.f, 100.f))
    {
        std::printf("FAIL: a flight master 5000+ yards from the node must not answer for it\n");
        ++failures;
    }
}

// THE THREE-WAY ARRIVAL DECISION, THE SAME SHAPE #378/#379 ESTABLISHED FOR A
// COUNTER: in reach outranks everything, nearby-and-out-of-reach is "not
// yet", and neither is "never, from here".
void EveryCombinationIsWhatItSays()
{
    struct Case
    {
        bool inReach;
        bool oneIsNearby;
        FlightDiscoveryArrival want;
    };
    Case const cases[] = {
        {true, true, FlightDiscoveryArrival::StandAndLearn},
        {true, false, FlightDiscoveryArrival::StandAndLearn},
        {false, true, FlightDiscoveryArrival::CloseTheGap},
        {false, false, FlightDiscoveryArrival::Done},
    };
    for (Case const& c : cases)
    {
        FlightDiscoveryArrival const got = FlightDiscoveryArrivalStep(c.inReach, c.oneIsNearby);
        if (got != c.want)
        {
            std::printf("FAIL FlightDiscoveryArrivalStep(inReach=%d, oneIsNearby=%d): "
                        "wrong answer\n",
                        c.inReach, c.oneIsNearby);
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    OnlyAWellFormedNodeAimParses();
    TheBuiltAimRoundTrips();
    TheNodeMatchIsTheSameAgreementConsiderFlightUses();
    EveryCombinationIsWhatItSays();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a deliberate errand can name the one node that is missing\n");
    return EXIT_SUCCESS;
}
