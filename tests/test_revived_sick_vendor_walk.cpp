/*
 * A food vendor walk releases the revived sickness hold of a member with
 * nothing to eat (mod-overseer#1377).
 *
 * The revived sickness hold (#746) keeps a member still after a spirit healer
 * revive so that it may eat. A member that carries no food or drink cannot,
 * and the supply step's walk to a food vendor was refused "character is held
 * by another verb" for as long as the hold stood. The rule: a supply walk for
 * food or drink releases that one hold when the member carries none. Every
 * other combination keeps today's refusal.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, std::string const& verb, bool supplyFood,
                             bool carriesFood, bool want)
    {
        bool const got = SupplyWalkReleasesSickHold(verb, supplyFood, carriesFood);
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, got ? 1 : 0, want ? 1 : 0);
        ++failures;
    };

    std::string const sick = REVIVED_SICKNESS_HOLD_VERB;
    check("no food, sickness hold, supply food walk: released", sick, true, false, true);
    check("carries food: the hold stands so it may eat", sick, true, true, false);
    check("not a supply food walk: refused as today", sick, false, false, false);
    check("not a supply food walk with food: refused as today", sick, false, true, false);
    check("another verb's hold is never released", "stage", true, false, false);
    check("the deck settle is not this rule's", DECK_SETTLE_HOLD_VERB, true, false, false);
    check("an empty verb is not the sickness hold", "", true, false, false);

    // THE DRIVE DEFERS TO A WALK, PAUSED OR NOT (#1377, live 2026-11-08). A
    // vendor walk lets go of its own hold while its member fights (#633); the
    // sickness drive took the member back in that gap and the walk ended
    // "held by another verb" (Swoleblood, walk 1509707). So a walk paused for a
    // fight counts the same as a walk hold in force.
    auto defer = [&failures](char const* name, bool walkHold, bool walkPaused, bool want)
    {
        bool const got = RevivedSicknessDefersToWalk(walkHold, walkPaused);
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, got ? 1 : 0, want ? 1 : 0);
        ++failures;
    };
    defer("a walk hold in force: the drive leaves it", true, false, true);
    defer("a walk paused for a fight: the drive still leaves the member", false, true, true);
    defer("no walk at all: the drive holds the member still", false, false, false);

    // The verb the adapter holds under is the one this rule names.
    if (std::string(REVIVED_SICKNESS_HOLD_VERB) != "revived sickness")
    {
        std::printf("FAIL the revived sickness verb changed spelling\n");
        ++failures;
    }

    if (failures)
        return 1;
    std::printf("revived sickness vendor walk: all checks pass\n");
    return 0;
}
