// A family whose roster head is not leading holds; no member leads in his
// place, and a catch-up aim is only ever the roster head (#736).
//
// Measured on the dev realm on 2026-09-26: while the Alliance head had no
// client, the server promoted a level 35 follower, the module granted it
// `new rpg` as the family's leader, and it wandered onto level 55 ground in
// Winterspring and died four times.
#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* name, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", name);
    ++failures;
}
}

int main()
{
    FamilyLeadership const absent = DecideFamilyLeadership(false, false, false);
    Check("an absent head leaves nobody leading", absent.leader == FamilyLeader::None);
    Check("an absent head holds the family", absent.holdFollowers);
    Check("an absent head grants no follower new rpg", !absent.grantNewRpg);

    FamilyLeadership const promoted = DecideFamilyLeadership(true, false, false);
    Check("a server-promoted member is not the leader", promoted.leader == FamilyLeader::None);
    Check("a server-promoted member gets no new rpg", !promoted.grantNewRpg);

    FamilyLeadership const present = DecideFamilyLeadership(true, true, false);
    Check("the head leads when present", present.leader == FamilyLeader::RosterLeader);
    Check("the head's family is not held", !present.holdFollowers);
    Check("the head is granted new rpg when it lacks it", present.grantNewRpg);
    Check("a head that carries new rpg is not granted it again",
          !DecideFamilyLeadership(true, true, true).grantNewRpg);

    Check("no aim without the head", DecideCatchUpAimSource(false, true, true) ==
                                         CatchUpAimSource::Unavailable);
    Check("no aim at a follower", DecideCatchUpAimSource(true, false, false) ==
                                      CatchUpAimSource::Unavailable);
    Check("the head's position is an aim", DecideCatchUpAimSource(true, true, false) ==
                                               CatchUpAimSource::RosterLeaderPosition);
    Check("a surveyed route point is an aim", DecideCatchUpAimSource(true, true, true) ==
                                                  CatchUpAimSource::SurveyedRoutePoint);

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
