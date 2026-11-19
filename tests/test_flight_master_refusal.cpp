/*
 * A character that would have flown and cannot reach a flight master able to
 * sell it the ticket says so rather than silently walking (#970). This pins
 * the pure layer ConsiderFlight asks: whether the flight master it resolved
 * can board it at the departure node, and the clause the refusal line says
 * when it cannot.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::FlightMasterBoarding;
using OverseerDecisions::FlightMasterCandidate;
using OverseerDecisions::FlightMasterRefusalReason;
using OverseerDecisions::PickFlightMasterForNode;
using OverseerDecisions::ReadFlightMasterBoarding;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// NO FLIGHT MASTER AT ALL OUTRANKS THE NODE MATCH. The positions of a flight
// master that was never resolved are whatever the caller's default is, and
// one that happens to sit on the node must not read as a boarding.
void NothingResolvedIsNoneInReach()
{
    Check("an unresolved flight master is NoneInReach even on the node",
          ReadFlightMasterBoarding(false, 100.f, 200.f, 100.f, 200.f, 100.f) ==
              FlightMasterBoarding::NoneInReach);
    Check("an unresolved flight master far from the node is still NoneInReach",
          ReadFlightMasterBoarding(false, 0.f, 0.f, 5000.f, 5000.f, 100.f) ==
              FlightMasterBoarding::NoneInReach);
}

// THE SAME NODE MATCH FlightMasterAnswersForNode MAKES, edge included.
void TheNodeMatchDecidesBetweenBoardAndWrongNode()
{
    Check("a flight master on the node boards",
          ReadFlightMasterBoarding(true, 100.f, 200.f, 100.f, 200.f, 100.f) ==
              FlightMasterBoarding::Board);
    Check("a flight master exactly at the match radius boards",
          ReadFlightMasterBoarding(true, 100.f, 200.f, 100.f, 300.f, 100.f) ==
              FlightMasterBoarding::Board);
    Check("one yard past the match radius is WrongNode",
          ReadFlightMasterBoarding(true, 100.f, 200.f, 100.f, 301.f, 100.f) ==
              FlightMasterBoarding::WrongNode);
    Check("a contested zone's other flight master is WrongNode",
          ReadFlightMasterBoarding(true, 0.f, 0.f, 5000.f, 5000.f, 100.f) ==
              FlightMasterBoarding::WrongNode);
}

// EACH REFUSAL HAS ITS OWN SENTENCE, so the log says which of the two it was,
// and a boarding has none because it is not a refusal.
void EachRefusalSaysItsOwnReason()
{
    std::string const none = FlightMasterRefusalReason(FlightMasterBoarding::NoneInReach, 0);
    std::string const wrong = FlightMasterRefusalReason(FlightMasterBoarding::WrongNode, 3838);
    Check("NoneInReach has a reason", !none.empty());
    Check("NoneInReach does not name a creature it never found",
          none.find("creature") == std::string::npos);
    Check("WrongNode names the creature that does not answer",
          wrong.find("creature 3838") != std::string::npos);
    Check("the two refusals say different things", none != wrong);
    Check("Board is not a refusal and says nothing",
          FlightMasterRefusalReason(FlightMasterBoarding::Board, 3838).empty());
}

// THE MASTER IS CHOSEN AMONG THOSE THAT SERVE THE NODE. A nearer master that
// serves another node, or one the character may not use, is never picked.
void AMasterThatDoesNotServeTheNodeIsNeverPicked()
{
    std::vector<FlightMasterCandidate> c;
    c.push_back({37888, 1000.f, 1000.f, true});   // nearest to the walker, far from the node
    c.push_back({111, 5000.f, 5000.f, true});     // stands on the node
    c.push_back({222, 5010.f, 5000.f, false});    // on the node but unusable
    Check("the master that serves the node is picked over a nearer stranger",
          PickFlightMasterForNode(c, 5000.f, 5000.f, 100.f) == 1);
    Check("nothing serving the node picks nothing",
          PickFlightMasterForNode(c, 9000.f, 9000.f, 100.f) == -1);
    c[1].usable = false;
    Check("an unusable master on the node is not picked",
          PickFlightMasterForNode(c, 5000.f, 5000.f, 100.f) == -1);
}

}  // namespace

int main()
{
    NothingResolvedIsNoneInReach();
    TheNodeMatchDecidesBetweenBoardAndWrongNode();
    EachRefusalSaysItsOwnReason();
    AMasterThatDoesNotServeTheNodeIsNeverPicked();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("an unreachable flight master is said, not silently walked past\n");
    return EXIT_SUCCESS;
}
