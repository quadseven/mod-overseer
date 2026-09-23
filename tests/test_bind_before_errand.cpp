/*
 * A town errand waits for the campaign bind; it does not cancel it (#583).
 *
 * Measured on the dev realm: a family leader held at the Ratchet inn to bind was
 * handed a vendor errand fourteen yards away. The travel drive walked the column,
 * the bind drive saw the leader outside the inn and let the hold go, and the
 * walk home began again: five cycles in ten minutes, no homebind moved.
 *
 * Pinned here: while a bind trip is open the travel drive walks the bind's aim,
 * whatever else the column holds, and with no bind trip the column is walked
 * exactly as before. The drive wiring needs the core and is compiled by the
 * adapter check.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::TravelAimBesideBind;

namespace
{

int failures = 0;

void CheckStr(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

std::string const Inn = "at:1:-1000.5,-3700.25,5.1";

void AVendorErrandWaitsForTheBind()
{
    CheckStr("the measured case: vendor written over a standing bind trip",
             TravelAimBesideBind(Inn, "vendor"), Inn);
    CheckStr("any other town errand waits the same way",
             TravelAimBesideBind(Inn, "repair"), Inn);
    CheckStr("a positional errand waits too",
             TravelAimBesideBind(Inn, "at:1:10,20,30"), Inn);
}

void TheBindTripKeepsItsOwnAim()
{
    CheckStr("the column already holds the bind aim",
             TravelAimBesideBind(Inn, Inn), Inn);
    CheckStr("a column cleared mid-trip does not end the trip",
             TravelAimBesideBind(Inn, ""), Inn);
}

void WithNoBindTripTheErrandRunsAsBefore()
{
    CheckStr("the errand is served once the bind has landed",
             TravelAimBesideBind("", "vendor"), "vendor");
    CheckStr("nothing in the column, nothing walked", TravelAimBesideBind("", ""), "");
}

}  // namespace

int main()
{
    AVendorErrandWaitsForTheBind();
    TheBindTripKeepsItsOwnAim();
    WithNoBindTripTheErrandRunsAsBefore();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a town errand waits for the campaign bind and runs after it\n");
    return EXIT_SUCCESS;
}
