/*
 * Zul'Farrak's End Door opens when nobody is left to blow it (#1337).
 *
 * The defect: Chief Ukorz Sandscalp stands behind the End Door (146084), and
 * only the core's Weegli Blastfuse opens it, with a barrel, after the pyramid's
 * trolls are dead. Four guild finder runs on the dev realm (2026-11-02 12:00 to
 * 2026-11-03 09:50 UTC; runs 1391677, 1396528, 1404157, 1407333) finished the
 * temple event, set out for Ukorz, and stood at the shut door ("door-blocked:
 * can't open 'End Door' -> auto-pausing") until they timed out. Nothing in the
 * module sent Weegli or opened the door when he was gone.
 *
 * Compiles against the pure decision file.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

// The measured stall: the tank near the shut door out of combat, the trolls
// dead, the door never started.
ZulFarrakEndDoorFacts AtTheDoor()
{
    ZulFarrakEndDoorFacts f;
    f.tankNear = true;
    f.doorShut = true;
    f.pyramidPhase = ZF_PYRAMID_KILLED_ALL_TROLLS;
    f.endDoorState = ZF_END_DOOR_NOT_STARTED;
    return f;
}

void AGoneWeegliMeansTheModuleOpensIt()
{
    ZulFarrakEndDoorFacts f = AtTheDoor();
    Check("Weegli dead or gone, the party at the shut door: Open",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Open);
    f.pyramidPhase = ZF_PYRAMID_WAVE_3;
    Check("...also when a temple boss was never credited and the phase stayed at wave 3",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Open);
    f.pyramidPhase = 12;  // PYRAMID_DONE: Bly is dead
    Check("...and after Bly is dead", ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Open);
}

void ALivingWeegliIsSentFirst()
{
    ZulFarrakEndDoorFacts f = AtTheDoor();
    f.weegliAlive = true;
    Check("a living Weegli who was never sent: AskWeegli",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::AskWeegli);
    f.weegliAsked = true;
    f.endDoorState = 1;  // IN_PROGRESS: he is walking
    f.secondsNear = ZF_END_DOOR_WEEGLI_SECONDS - 1;
    Check("...then Wait while he may be on his way",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Wait);
    f.secondsNear = ZF_END_DOOR_WEEGLI_SECONDS;
    Check("...and Open once his time is up and the door is still shut",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Open);

    ZulFarrakEndDoorFacts early = AtTheDoor();
    early.weegliAlive = true;
    early.pyramidPhase = ZF_PYRAMID_WAVE_3;
    Check("a living Weegli before the trolls are dead cannot be sent: Wait",
          ZulFarrakEndDoorNext(early) == ZulFarrakEndDoorStep::Wait);
    early.secondsNear = ZF_END_DOOR_WEEGLI_SECONDS;
    Check("...and Open after the wait", ZulFarrakEndDoorNext(early) == ZulFarrakEndDoorStep::Open);
}

void NothingWhenTheDoorIsNotTheObstacle()
{
    ZulFarrakEndDoorFacts f = AtTheDoor();
    f.doorShut = false;
    Check("an open door: Nothing", ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);

    f = AtTheDoor();
    f.endDoorState = ZF_END_DOOR_DONE;
    Check("the end door already DONE: Nothing",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);

    f = AtTheDoor();
    f.tankNear = false;
    Check("the tank away from the door: Nothing",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);

    f = AtTheDoor();
    f.fighting = true;
    Check("a fight: Nothing", ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);

    f = AtTheDoor();
    f.pyramidPhase = ZF_PYRAMID_WAVE_3 - 1;
    Check("the temple event not played to its last wave: Nothing",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);
    f.pyramidPhase = 0;
    Check("the temple event never started: Nothing",
          ZulFarrakEndDoorNext(f) == ZulFarrakEndDoorStep::Nothing);
}

void TheConstantsAreTheCoresAndTheMeasuredOnes()
{
    Check("map 209", ZF_MAP_ID == 209);
    Check("the End Door is GO 146084", ZF_END_DOOR_ENTRY == 146084);
    Check("Weegli is NPC 7607", ZF_WEEGLI_ENTRY == 7607);
    Check("DATA_PYRAMID 0, DATA_END_DOOR 2", ZF_DATA_PYRAMID == 0 && ZF_DATA_END_DOOR == 2);
    Check("ACTION_DESTROY_GATES is 2", ZF_ACTION_DESTROY_GATES == 2);
    Check("the brain parked 48 to 81 yards from the door, inside the near radius",
          ZF_END_DOOR_NEAR_YARDS > 81.f);
    Check("the summit, about 150 yards off, is outside it", ZF_END_DOOR_NEAR_YARDS < 150.f);
}
}  // namespace

int main()
{
    AGoneWeegliMeansTheModuleOpensIt();
    ALivingWeegliIsSentFirst();
    NothingWhenTheDoorIsNotTheObstacle();
    TheConstantsAreTheCoresAndTheMeasuredOnes();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all Zul'Farrak end door checks pass\n");
    return 0;
}
