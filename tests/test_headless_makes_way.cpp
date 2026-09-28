// A character on the headless roster makes way for a real client (infra#4835).
//
// Measured on the dev realm on 2026-09-28: the two family heads were kept off
// the headless roster because a client logs them in, so with nobody watching
// the stream they were offline from 04:12Z, and #736 lets nobody lead in a
// head's place. Both families stood still. The heads now play headless, and
// these are the rules for when that copy must get out of a client's way.
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
    // The agent writes `starting` before it launches and keeps `live` fresh.
    Check("a fresh starting row claims a client", StreamRowClaimsAClient("starting", 5));
    Check("a fresh live row claims a client", StreamRowClaimsAClient("live", 0));
    Check("a starting row queued for the screen is still believed",
          StreamRowClaimsAClient("starting", 300));
    Check("the claim runs to its limit", StreamRowClaimsAClient("live", STREAM_CLAIM_SECONDS));
    Check("a clock skew reads as fresh", StreamRowClaimsAClient("starting", -3));

    // An abandoned row cannot keep a head out of the world for long.
    Check("a row past the limit claims nothing",
          !StreamRowClaimsAClient("live", STREAM_CLAIM_SECONDS + 1));
    Check("a week-old recording row claims nothing", !StreamRowClaimsAClient("live", 604800));

    // A request the viewer's page keeps fresh is not a client: no agent may be
    // running to serve it, and the family must not stall behind it.
    Check("a requested row claims nothing", !StreamRowClaimsAClient("requested", 1));
    Check("a stopping row claims nothing", !StreamRowClaimsAClient("stopping", 1));
    Check("an ended row claims nothing", !StreamRowClaimsAClient("ended", 1));
    Check("an unknown state claims nothing", !StreamRowClaimsAClient("", 1));

    Check("nothing coming: play headless", !HeadlessMakesWayForAClient(false, false));
    Check("a stream claim makes way", HeadlessMakesWayForAClient(true, false));
    Check("a real session on the account makes way", HeadlessMakesWayForAClient(false, true));
    Check("both make way", HeadlessMakesWayForAClient(true, true));

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
