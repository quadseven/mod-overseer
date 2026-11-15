#include "Ai/Base/Strategy/GrindResourceHold.h"

#include <cstdlib>
#include <iostream>

using GrindResourceHold::GrindResourceState;
using GrindResourceHold::ShouldHoldGrindPull;

namespace
{
int Fail(char const* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

GrindResourceState ReadySolo()
{
    return {true, false, 1, true, 100, 15, true, 100, 20, true};
}
}  // namespace

int main()
{
    GrindResourceState state = ReadySolo();
    state.manaPercent = 9;
    state.hasDrink = false;
    if (!ShouldHoldGrindPull(state))
        return Fail("a solo mana user below its floor with no drink must hold");

    state.hasDrink = true;
    if (ShouldHoldGrindPull(state))
        return Fail("carried drink must release the mana hold");
    state.hasDrink = false;
    state.manaPercent = 15;
    if (ShouldHoldGrindPull(state))
        return Fail("recovered mana must release the mana hold");

    state = ReadySolo();
    state.healthPercent = 19;
    state.hasFood = false;
    if (!ShouldHoldGrindPull(state))
        return Fail("a solo character below its health floor with no food must hold");
    state.hasFood = true;
    if (ShouldHoldGrindPull(state))
        return Fail("carried food must release the health hold");
    state.hasFood = false;
    state.healthPercent = 20;
    if (ShouldHoldGrindPull(state))
        return Fail("recovered health must release the health hold");

    state = ReadySolo();
    state.manaPercent = 0;
    state.hasDrink = false;
    state.inCombat = true;
    if (ShouldHoldGrindPull(state))
        return Fail("resource hold must not interrupt combat");
    state.inCombat = false;
    state.groupMemberCount = 5;
    if (ShouldHoldGrindPull(state))
        return Fail("resource hold must not block a group run");
    state.groupMemberCount = 1;
    state.alive = false;
    if (ShouldHoldGrindPull(state))
        return Fail("resource hold must not run for a dead character");
    state.alive = true;
    state.hasMana = false;
    if (ShouldHoldGrindPull(state))
        return Fail("a character without mana must not enter the mana hold");

    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
