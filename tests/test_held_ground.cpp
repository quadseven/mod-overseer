#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, HeldGroundFacts facts, HeldGroundStep want)
    {
        HeldGroundStep const got = DecideHeldGround(facts).step;
        if (got == want)
            return;
        std::printf("FAIL %s: got %u, wanted %u\n", name, unsigned(got), unsigned(want));
        ++failures;
    };

    HeldGroundFacts facts;
    facts.memberLevel = 35;
    facts.groundTopLevel = 40;
    facts.hearthReady = true;
    check("lethal ground with ready hearth", facts, HeldGroundStep::Hearth);
    facts.hearthReady = false;
    facts.safeSpotKnown = true;
    check("lethal ground with safe spot", facts, HeldGroundStep::WalkToSafety);
    facts.groundTopLevel = 37;
    check("ground below lethal margin", facts, HeldGroundStep::Hold);
    return failures ? 1 : 0;
}
