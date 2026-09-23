/*
 * A leader never flies away from a follower that nothing is walking.
 *
 * Measured 2026-09-23 on the dev realm. The Horde leader was sent toward the
 * Ragefire Chasm staging point 5,174 yards away and flew node 22 to node 23
 * alone. His four followers stood in Mulgore, each carrying an old bridge aim
 * the travel drive refused to walk ("does not carry `new rpg` - nothing walks
 * it anywhere"). The seat reader read each non-empty column as a walk of the
 * member's own, so PlanPartyFlight saw a party of one and the family split
 * across a continent.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::MemberFollowsForFlight;
using OverseerDecisions::PartyFlightMember;
using OverseerDecisions::PartyFlightVerdict;
using OverseerDecisions::PlanPartyFlight;

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

PartyFlightMember Leader()
{
    PartyFlightMember m;
    m.name = "Leader";
    m.leader = true;
    m.hasDepartureNode = true;
    m.masterInReach = true;
    m.routeKnown = true;
    m.canPayFare = true;
    return m;
}

// A follower the module cannot put on a taxi, standing far behind.
PartyFlightMember Stranded(std::string const& name, bool follows)
{
    PartyFlightMember m;
    m.name = name;
    m.followingTheLeader = follows;
    m.steerable = false;
    return m;
}

void TheRuleItself()
{
    Check("an empty column follows", MemberFollowsForFlight(true, false, false, false),
          true);
    Check("a catch-up walk follows", MemberFollowsForFlight(false, true, false, false),
          true);
    Check("an aim nothing walks still follows",
          MemberFollowsForFlight(false, false, true, false), true);
    Check("a member held waiting for this leader follows",
          MemberFollowsForFlight(false, false, false, true), true);
    Check("an aim the drive is walking is its own errand",
          MemberFollowsForFlight(false, false, false, false), false);
}

void TheMeasuredSplit()
{
    // The four followers as the seat reader now reads them: each column held
    // an aim nothing walked, so each is behind the leader.
    std::vector<PartyFlightMember> party{Leader()};
    for (char const* name : {"Oz", "Uzza", "Zork", "Zrog"})
        party.push_back(Stranded(name, MemberFollowsForFlight(false, false, true, false)));
    Check("the leader does not fly away from followers nothing walks",
          PlanPartyFlight(party).verdict == PartyFlightVerdict::Fly, false);

    // A follower genuinely off on an errand the drive is walking is still not
    // dragged along, which is what the exemption has always been for.
    std::vector<PartyFlightMember> errand{Leader()};
    errand.push_back(Stranded("Busy", MemberFollowsForFlight(false, false, false, false)));
    Check("a member walking its own errand does not hold the flight",
          PlanPartyFlight(errand).verdict == PartyFlightVerdict::Fly, true);
}

}  // namespace

int main()
{
    TheRuleItself();
    TheMeasuredSplit();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
