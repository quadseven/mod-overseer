/*
 * Whether an ordered raid's head may lead the run without a game client
 * (mod-overseer#1544). A realm no client can reach has a head that is in the
 * world as a headless bot; the run used to hold in IDLE for ever on it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::RaidHeadMayLead;

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
}  // namespace

int main()
{
    // The default: a client is required, so a headless head holds the raid.
    Check("client required, client attached", RaidHeadMayLead(true, true, true), true);
    Check("client required, headless head", RaidHeadMayLead(true, false, true), false);
    // The requirement off: any head in the world leads.
    Check("not required, headless head", RaidHeadMayLead(true, false, false), true);
    Check("not required, client attached", RaidHeadMayLead(true, true, false), true);
    // Never a head that is not in the world, whatever the setting.
    Check("not in the world, required", RaidHeadMayLead(false, true, true), false);
    Check("not in the world, not required", RaidHeadMayLead(false, false, false), false);
    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
