/*
 * Which transports the crossing catalogue holds (quadseven/mod-overseer#1029).
 *
 * The catalogue dropped every transport whose path named fewer than two maps,
 * so The Purple Princess (176495), the Undercity to Grom'gol zeppelin, which
 * never leaves map 0, was never read and every `ride-to-stop` row it is meant
 * for was refused with no route. AdmitToCrossingCatalogue admits it, and keeps
 * out what was kept out before.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>

namespace D = OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

D::CatalogueCandidate Candidate(bool inInstance, std::size_t maps, std::size_t stops)
{
    D::CatalogueCandidate c;
    c.inInstance = inInstance;
    c.mapsUsed = maps;
    c.stopFrames = stops;
    return c;
}

void TheZeppelinThatNeverLeavesOneMapIsAdmitted()
{
    // 176495: map 0 only, a stop at the Undercity tower and one at Grom'gol.
    Check("one map, two stops: admitted", D::AdmitToCrossingCatalogue(Candidate(false, 1, 2)));
    Check("one map, three stops: admitted", D::AdmitToCrossingCatalogue(Candidate(false, 1, 3)));
}

void ACrossingBetweenTwoMapsIsStillAdmitted()
{
    Check("two maps, two stops: admitted", D::AdmitToCrossingCatalogue(Candidate(false, 2, 2)));
    Check("three maps: admitted", D::AdmitToCrossingCatalogue(Candidate(false, 3, 3)));
    // Stops are not read for a crossing; its pricing judges its stops itself.
    Check("two maps, stops unread: admitted", D::AdmitToCrossingCatalogue(Candidate(false, 2, 0)));
}

void AOneMapTransportWithNowhereToRideToIsNot()
{
    Check("one map, one stop: refused", !D::AdmitToCrossingCatalogue(Candidate(false, 1, 1)));
    Check("one map, no stop: refused", !D::AdmitToCrossingCatalogue(Candidate(false, 1, 0)));
    Check("no map: refused", !D::AdmitToCrossingCatalogue(Candidate(false, 0, 2)));
}

void AnInstancesOwnTransportIsNeverARoute()
{
    Check("instance, one map, two stops: refused", !D::AdmitToCrossingCatalogue(Candidate(true, 1, 2)));
    Check("instance, two maps: refused", !D::AdmitToCrossingCatalogue(Candidate(true, 2, 2)));
}

}  // namespace

int main()
{
    TheZeppelinThatNeverLeavesOneMapIsAdmitted();
    ACrossingBetweenTwoMapsIsStillAdmitted();
    AOneMapTransportWithNowhereToRideToIsNot();
    AnInstancesOwnTransportIsNeverARoute();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("crossing catalogue admission: all checks passed\n");
    return 0;
}
