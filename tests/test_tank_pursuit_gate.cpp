/*
 * Where the warrior tank pursuit may act (mod-overseer#1055).
 *
 * #1049 had the engagement drive MoveChase a warrior tank that is in combat
 * and out of melee reach, on every poll, everywhere. Inside a dungeon that
 * fights mod-dungeon-clear's LOS-break drag, which keeps the tank out of melee
 * reach on purpose while it pulls a caster pack round a corner. This file pins
 * the gate: no pursuit on an instance map, under an overseer hold, mounted or
 * dead, and #1049's pursuit unchanged everywhere else.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::TankMayPursue;
using OverseerDecisions::TankPursuitFacts;

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

// A protection warrior with the tank strategy, fighting a caster it cannot
// reach, in the open world, free to move: the case #1049 was written for.
TankPursuitFacts TankOutOfReachInTheOpen()
{
    TankPursuitFacts facts;
    facts.classId = 1;
    facts.tanks = true;
    facts.inCombat = true;
    facts.inMeleeRange = false;
    facts.onInstanceMap = false;
    facts.heldStill = false;
    facts.mounted = false;
    facts.alive = true;
    return facts;
}

void TheOpenWorldKeepsThePursuit()
{
    Check("a fighting warrior tank out of reach in the open world chases",
          TankMayPursue(TankOutOfReachInTheOpen()));

    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.inMeleeRange = true;
    Check("a tank already in melee reach is left to its combat engine", !TankMayPursue(facts));

    facts = TankOutOfReachInTheOpen();
    facts.inCombat = false;
    Check("an idle tank does not start a fight", !TankMayPursue(facts));

    facts = TankOutOfReachInTheOpen();
    facts.tanks = false;
    Check("a warrior without the tank strategy keeps its combat engine", !TankMayPursue(facts));

    facts = TankOutOfReachInTheOpen();
    facts.classId = 2;
    Check("other classes keep their combat engine", !TankMayPursue(facts));
}

void ADungeonRunOwnsTheTank()
{
    // The #1055 case: the LOS-break drag has the tank in combat and out of
    // melee reach of the caster on purpose, on a dungeon map.
    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.onInstanceMap = true;
    Check("a tank out of reach on an instance map does not chase (the LOS-break drag)",
          !TankMayPursue(facts));
}

void AHoldKeepsTheTankStill()
{
    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.heldStill = true;
    Check("a tank under an overseer hold (the far hold among them) does not chase",
          !TankMayPursue(facts));
}

void MountedOrDeadDoesNotChase()
{
    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.mounted = true;
    Check("a mounted tank does not chase", !TankMayPursue(facts));

    facts = TankOutOfReachInTheOpen();
    facts.alive = false;
    Check("a dead tank does not chase", !TankMayPursue(facts));
}

}  // namespace

int main()
{
    TheOpenWorldKeepsThePursuit();
    ADungeonRunOwnsTheTank();
    AHoldKeepsTheTankStill();
    MountedOrDeadDoesNotChase();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the tank pursuit acts only in the open world, unheld, unmounted and alive\n");
    return EXIT_SUCCESS;
}
