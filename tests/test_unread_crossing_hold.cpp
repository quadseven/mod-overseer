/*
 * A crossing's hold that no crossing read this poll is let go (#758).
 *
 * WHAT WAS MEASURED. On 2026-10-18 the run leader reached the Theramore berth
 * and the crossing held him there to wait for the Lady Mehley. Fifty seconds
 * later a family member's bags filled, and IDLE's bag gate returned before the
 * crossing on every poll after that. Nothing read the crossing again, so
 * nothing let go of its hold: the leader stood rooted on the pier for the
 * hold's whole 1800s ceiling ("its hold reached the ceiling and nothing came
 * back for it"), refused the town trip that would have emptied those bags,
 * with the family split across two maps the whole time.
 *
 * The first half proves the decision. The second half pins, as text, that
 * the coordinator asks it: the flag is cleared before each family is driven,
 * set where the family crossing is read, and the release is asked after the
 * family's coordinator has run, whichever way it returned. A decision nothing
 * feeds is the failure this repository has shipped before.
 *
 * Compiled against src/overseer_decisions.cpp and run from the repository root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::CrossingHoldFacts;
using OverseerDecisions::CrossingHoldIsLeftOver;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The measured state: the leader on the pier under the crossing's hold, and
// the bag gate returned before anything read the crossing.
CrossingHoldFacts Measured()
{
    CrossingHoldFacts facts;
    facts.crossingHold = true;
    facts.crossingRead = false;
    facts.aboard = false;
    facts.crossingAlone = false;
    return facts;
}

std::string Body(std::string const& text, std::string const& signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
} // namespace

int main()
{
    // ---- the decision ----
    Check("the measured state: a crossing hold nothing read is let go",
          CrossingHoldIsLeftOver(Measured()));

    {
        CrossingHoldFacts facts = Measured();
        facts.crossingRead = true;
        Check("a crossing read this poll keeps its own hold", !CrossingHoldIsLeftOver(facts));
    }
    {
        CrossingHoldFacts facts = Measured();
        facts.crossingHold = false;
        Check("a hold placed by anything else is not this one's to lift",
              !CrossingHoldIsLeftOver(facts));
    }
    {
        CrossingHoldFacts facts = Measured();
        facts.aboard = true;
        Check("a leader on a deck keeps the hold, so nothing walks him over the side",
              !CrossingHoldIsLeftOver(facts));
    }
    {
        CrossingHoldFacts facts = Measured();
        facts.crossingAlone = true;
        Check("a member crossing of his own keeps the hold it placed",
              !CrossingHoldIsLeftOver(facts));
    }
    Check("no hold facts at all release nothing", !CrossingHoldIsLeftOver(CrossingHoldFacts{}));

    // ---- the call sites ----
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::fprintf(stderr, "FAIL: cannot open src/mod_overseer.cpp (run from the repo root)\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const loop = Body(text, "    void DriveDungeonRun()\n");
    std::size_t const clear =
        loop.find("_dungeonRunCoordinators[roster->family].crossingReadThisPoll = false;");
    std::size_t const drive =
        loop.find("DriveDungeonRunFor(roster->family, members, roster->leader, jobs);");
    std::size_t const release = loop.find(
        "ReleaseUnreadCrossingHold(_dungeonRunCoordinators[roster->family], roster->leader);");
    Check("DriveDungeonRun clears the flag, drives the family, then asks the release",
          clear != std::string::npos && drive != std::string::npos &&
              release != std::string::npos && clear < drive && drive < release);

    std::string const crossing = Body(text, "    bool DriveCampaignCrossing(");
    std::size_t const marked = crossing.find("coord.crossingReadThisPoll = true;");
    std::size_t const read = crossing.find("ReadCrossingFromWorld(");
    Check("DriveCampaignCrossing marks the crossing read before it reads the world",
          marked != std::string::npos && read != std::string::npos && marked < read);

    std::string const releaser = Body(text, "    void ReleaseUnreadCrossingHold(");
    Check("the release asks the decision and lifts only the crossing's own verb",
          releaser.find("OverseerDecisions::CrossingHoldIsLeftOver(facts)") != std::string::npos &&
              releaser.find("facts.crossingRead = coord.crossingReadThisPoll;") !=
                  std::string::npos &&
              releaser.find("facts.aboard = leader && leader->GetTransport();") !=
                  std::string::npos &&
              releaser.find("CROSSING_HOLD_VERB);") != std::string::npos);

    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("unread crossing hold: all checks passed\n");
    return EXIT_SUCCESS;
}
