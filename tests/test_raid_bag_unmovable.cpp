/*
 * The raid bag-room gate does not wait on a seat this module cannot move
 * (mod-overseer#1547). On a realm where most seats are guild bots driven by the
 * playerbots module, the gate used to claim a vendor errand for each seat under
 * the free-slot floor, which nothing carried out, and then held the pull for the
 * whole hold wait before naming each one as unfreed.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::RAID_HOLD_WAIT_SECONDS;
using OverseerDecisions::RaidBagStep;
using OverseerDecisions::RaidSeatBagStep;

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

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    unsigned const floor = 6;

    Check("a seat the module cannot move is unmovable at once, not walked",
          RaidSeatBagStep(2, floor, 0, false, false) == RaidBagStep::Unmovable);
    Check("an unmovable seat is not stood at a vendor either",
          RaidSeatBagStep(2, floor, 10, true, false) == RaidBagStep::Unmovable);
    Check("an unmovable seat does not wait out the hold",
          RaidSeatBagStep(2, floor, RAID_HOLD_WAIT_SECONDS + 1, false, false) ==
              RaidBagStep::Unmovable);
    Check("an unmovable seat with room is simply ready",
          RaidSeatBagStep(floor, floor, 0, false, false) == RaidBagStep::Ready);
    // A movable seat reads exactly as before.
    Check("a movable seat under the floor still walks",
          RaidSeatBagStep(2, floor, 10, false, true) == RaidBagStep::Walk);
    Check("a movable seat at a vendor still stands",
          RaidSeatBagStep(2, floor, 10, true, true) == RaidBagStep::Stand);
    Check("a movable seat still times out",
          RaidSeatBagStep(2, floor, RAID_HOLD_WAIT_SECONDS, true, true) == RaidBagStep::TimedOut);

    // The adapter feeds the decision the module's own steering rule and says,
    // once per gate, how many seats it will not wait on.
    std::ifstream in("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    Check("the adapter reads whether the module can move each seat",
          Has(source, "Steerable(member)"));
    Check("the adapter names the seats it does not wait on",
          Has(source, "cannot move") && Has(source, "RaidBagStep::Unmovable"));

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
