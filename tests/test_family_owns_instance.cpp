/*
 * A family adopts an instance only when it is the family's own: a lone member
 * standing in somebody else's run (a member lent to a guild run that has since
 * ended, left in the group of another guild) is not a run to drive.
 */

#include <cstdio>

#include "overseer_decisions.h"

using namespace OverseerDecisions;

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
}  // namespace

int main()
{
    // The phantom: one of five online members inside, no run row for the family.
    Check("a lone member of five with no run row is not the family's instance",
          !FamilyOwnsInstance(1, 5, false));
    Check("a lone member of three online with no run row is not the family's instance",
          !FamilyOwnsInstance(1, 3, false));

    // A real run after a restart: most of the party is inside.
    Check("a majority inside is the family's instance", FamilyOwnsInstance(3, 5, false));
    Check("half inside is the family's instance", FamilyOwnsInstance(2, 4, false));
    Check("the whole party inside is the family's instance", FamilyOwnsInstance(5, 5, false));

    // A run the family opened keeps its row, and a thinned party keeps it.
    Check("an open run row for the family keeps a thinned party's run",
          FamilyOwnsInstance(1, 5, true));

    // A family of one that is online alone is its own party.
    Check("the only online member inside is the family's instance", FamilyOwnsInstance(1, 1, false));

    // Nobody inside owns nothing, row or not.
    Check("nobody inside owns nothing", !FamilyOwnsInstance(0, 5, false));
    Check("nobody inside owns nothing even with a row", !FamilyOwnsInstance(0, 5, true));

    if (failures)
        return 1;
    std::printf("family owns instance: all checks passed\n");
    return 0;
}
