/*
 * A far-held member on lethal ground that is in combat does not wait on a
 * hearth the cast will refuse: it walks to a graveyard it can survive
 * (mod-overseer#744).
 *
 * Measured on wow-dev 2026-10-19 (roll 64b). At 14:26:51 the far hold's
 * ground check answered hearth for Zrog (level 31, held on level 48 ground in
 * Felwood, stone ready and bound on its own map). At 14:27:21 the cast came
 * back "could not hearth to its family (character is in combat)", and Zrog was
 * killed by an Angerclaw Bear at 14:27:30, `driven by idle`, still held. Ugga
 * (level 42) did the same in Searing Gorge: hearth answered at 23:22:09,
 * refused in combat at 23:22:39, dead at 23:23:12. Six of the roll's ten
 * refused hearths were refused for combat. The hearthstone cannot be used in
 * combat (SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL), so in combat a ready stone
 * is no way off lethal ground: the member takes the walk it would take with
 * the stone on cooldown.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, HeldGroundFacts const& facts, HeldGroundStep want)
    {
        HeldGroundStep const got = DecideHeldGround(facts).step;
        if (got == want)
            return;
        std::printf("FAIL %s: got %u, wanted %u\n", name, unsigned(got), unsigned(want));
        ++failures;
    };
    auto expect = [&failures](char const* name, bool got, bool want)
    {
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, int(got), int(want));
        ++failures;
    };

    // Zrog at 14:27:21: level 31, held on level 48 ground, stone ready and
    // bound on its own map, in combat.
    HeldGroundFacts zrog;
    zrog.memberLevel = 31;
    zrog.groundTopLevel = 48;
    zrog.levelGap = 3;
    zrog.hearthReady = true;
    zrog.boundOnMemberMap = true;
    zrog.inCombat = true;
    check("in combat with no safe spot known is not a hearth", zrog, HeldGroundStep::Hold);
    expect("in combat a ready stone still needs a safe spot", HeldGroundNeedsSafeSpot(zrog),
           true);

    // With a graveyard it can survive, it walks there instead.
    zrog.safeSpotKnown = true;
    zrog.safeSpotTopLevel = 0;
    zrog.safeSpotYards = 260.0f;
    check("in combat it walks to the graveyard", zrog, HeldGroundStep::WalkToSafety);

    // A graveyard as lethal as where it stands is still no move.
    zrog.safeSpotTopLevel = 40;
    check("in combat a lethal graveyard is still held", zrog, HeldGroundStep::Hold);

    // Out of combat the same member hearths, as #744 shipped.
    zrog.safeSpotTopLevel = 0;
    zrog.inCombat = false;
    check("out of combat a ready stone hearths", zrog, HeldGroundStep::Hearth);
    expect("out of combat a usable stone needs no safe spot", HeldGroundNeedsSafeSpot(zrog),
           false);

    // Combat does not make survivable ground lethal, and an inn is never lethal.
    HeldGroundFacts calm;
    calm.memberLevel = 31;
    calm.groundTopLevel = 32;
    calm.levelGap = 3;
    calm.inCombat = true;
    check("in combat on survivable ground is still held", calm, HeldGroundStep::Hold);
    calm.groundTopLevel = 48;
    calm.resting = true;
    check("in combat at an inn is still held", calm, HeldGroundStep::Hold);

    if (!failures)
        std::printf("held ground combat: all checks passed\n");
    return failures ? 1 : 0;
}
