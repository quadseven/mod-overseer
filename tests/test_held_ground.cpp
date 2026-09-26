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
    // AN INN OR A CITY IS NEVER LETHAL GROUND, whatever spawn level is read.
    facts.groundTopLevel = 80;
    facts.hearthReady = true;
    facts.resting = true;
    check("resting in a city holds, never hearths", facts, HeldGroundStep::Hold);

    RevivedSickGroundFacts sick;
    sick.sick = true;
    sick.memberLevel = 17;
    sick.groundTopLevel = 80;
    sick.hearthReady = true;
    sick.resting = true;
    if (DecideRevivedSickGround(sick) != RevivedSickGroundStep::Stay)
    {
        std::printf("FAIL a sick member resting in a city stays\n");
        ++failures;
    }
    sick.resting = false;
    if (DecideRevivedSickGround(sick) != RevivedSickGroundStep::Hearth)
    {
        std::printf("FAIL a sick member on lethal open ground still hearths\n");
        ++failures;
    }
    return failures ? 1 : 0;
}
