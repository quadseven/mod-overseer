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

#include <string>
#include <vector>

using OverseerDecisions::TankMayPursue;
using OverseerDecisions::TankPursuitFacts;
using OverseerDecisions::TankPursuitWithheld;
using OverseerDecisions::TankStrategyListedName;

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

// THE LIVE CASE (#1212). After AiFactory hands a protection warrior `tank`,
// its combat engine lists `prot`: the warrior factory maps `tank` to
// TankWarriorStrategy, whose getName() is "prot". This is the list a live
// open-world warrior tank had while it failed melee abilities out of range,
// and the pursuit has to read it as a tank.
bool Lists(std::vector<std::string> const& live, std::string const& name)
{
    for (std::string const& s : live)
        if (s == name)
            return true;
    return false;
}

void TheTankIsFoundByTheNameItIsListedUnder()
{
    std::vector<std::string> const warriorLive = {"aoe", "mark rti", "prot", "pull",
                                                  "pull back", "racials", "tank assist"};
    Check("a protection warrior's live list is read as a tank",
          Lists(warriorLive, TankStrategyListedName(1)));
    Check("a warrior is not asked for the word 'tank', which it never lists",
          TankStrategyListedName(1) != "tank");
    Check("a paladin's tank strategy is listed as 'prot'", TankStrategyListedName(2) == "prot");
    Check("a death knight's tank strategy is listed as 'blood'",
          TankStrategyListedName(6) == "blood");
    Check("a druid's tank strategy is listed as 'bear'", TankStrategyListedName(11) == "bear");

    // Fed from that list, the open-world tank out of reach chases.
    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.tanks = Lists(warriorLive, TankStrategyListedName(facts.classId));
    Check("the live open-world warrior tank out of reach chases", TankMayPursue(facts));
    Check("and nothing is said about withholding it", TankPursuitWithheld(facts).empty());
}

void ARefusalIsSaid()
{
    TankPursuitFacts facts = TankOutOfReachInTheOpen();
    facts.heldStill = true;
    Check("a held tank out of reach says it is held",
          TankPursuitWithheld(facts) == "an overseer hold has it standing still");

    facts = TankOutOfReachInTheOpen();
    facts.mounted = true;
    Check("a mounted tank says so", TankPursuitWithheld(facts) == "it is mounted or in flight");

    facts = TankOutOfReachInTheOpen();
    facts.alive = false;
    Check("a dead tank says so", TankPursuitWithheld(facts) == "it is dead");

    facts = TankOutOfReachInTheOpen();
    facts.hasVictim = false;
    Check("a tank in combat with no victim does not chase", !TankMayPursue(facts));
    Check("and says it has nothing to chase",
          TankPursuitWithheld(facts) == "it has no victim or selection it may attack");

    // Nothing to say: the dungeon gate as meant, in reach, idle, not a tank.
    facts = TankOutOfReachInTheOpen();
    facts.onInstanceMap = true;
    Check("an instance map is the #1055 gate working, not a line per fight",
          TankPursuitWithheld(facts).empty());
    facts = TankOutOfReachInTheOpen();
    facts.inMeleeRange = true;
    Check("a tank in reach has nothing withheld", TankPursuitWithheld(facts).empty());
    facts = TankOutOfReachInTheOpen();
    facts.inCombat = false;
    Check("an idle tank has nothing withheld", TankPursuitWithheld(facts).empty());
    facts = TankOutOfReachInTheOpen();
    facts.tanks = false;
    Check("a warrior that is not a tank is not talked about", TankPursuitWithheld(facts).empty());
    facts = TankOutOfReachInTheOpen();
    facts.classId = 2;
    Check("another class is not talked about", TankPursuitWithheld(facts).empty());
}

}  // namespace

int main()
{
    TheOpenWorldKeepsThePursuit();
    ADungeonRunOwnsTheTank();
    AHoldKeepsTheTankStill();
    MountedOrDeadDoesNotChase();
    TheTankIsFoundByTheNameItIsListedUnder();
    ARefusalIsSaid();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the tank pursuit acts only in the open world, unheld, unmounted and alive\n");
    return EXIT_SUCCESS;
}
