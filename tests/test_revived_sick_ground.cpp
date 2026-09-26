#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, RevivedSickGroundFacts facts,
                             RevivedSickGroundStep want)
    {
        RevivedSickGroundStep const got = DecideRevivedSickGround(facts);
        if (got == want)
            return;
        std::printf("FAIL %s: got %u, wanted %u\n", name, unsigned(got), unsigned(want));
        ++failures;
    };

    RevivedSickGroundFacts facts;
    facts.sick = true;
    facts.memberLevel = 35;
    facts.groundTopLevel = 38;
    facts.hearthReady = true;
    check("sick member hearths on lethal ground when ready", facts,
          RevivedSickGroundStep::Hearth);
    facts.hearthReady = false;
    check("sick member is held out of combat without a ready hearth", facts,
          RevivedSickGroundStep::HoldOutOfCombat);
    facts.sick = false;
    check("member stays when sickness has ended", facts, RevivedSickGroundStep::Stay);
    facts.sick = true;
    facts.groundTopLevel = 37;
    check("sick member stays below the lethal margin", facts, RevivedSickGroundStep::Stay);
    return failures ? 1 : 0;
}
