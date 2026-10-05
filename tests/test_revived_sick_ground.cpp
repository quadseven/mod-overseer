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

    // Zrog on wow-dev 2026-10-05: level 27, Hillsbrad Footmen at 26, 21 deaths
    // at the one spirit healer in ten minutes. Its own deaths make it lethal.
    RevivedSickGroundFacts loop;
    loop.sick = true;
    loop.memberLevel = 27;
    loop.groundTopLevel = 26;
    loop.repeatDeaths = 2;
    loop.deathsHere = 21;
    loop.hearthReady = true;
    check("a death loop hearths out though no hostile outlevels it", loop,
          RevivedSickGroundStep::Hearth);
    loop.hearthReady = false;
    check("a death loop without a ready hearth is held out of combat", loop,
          RevivedSickGroundStep::HoldOutOfCombat);
    loop.deathsHere = 1;
    check("one death here is not a loop", loop, RevivedSickGroundStep::Stay);
    loop.deathsHere = 21;
    loop.repeatDeaths = 0;
    check("no repeat count asked keeps the level rule alone", loop,
          RevivedSickGroundStep::Stay);
    loop.repeatDeaths = 2;
    loop.restWhileSick = true;
    loop.hearthReady = true;
    check("a guild member in a death loop hearths too", loop, RevivedSickGroundStep::Hearth);
    return failures ? 1 : 0;
}
