/*
 * A member is never hearthed to a bind on another map from its family
 * (mod-overseer#274).
 *
 * Measured on wow-dev 2026-10-19. Grog (level 43) was held far from its leader
 * Grug on map 1, on ground spawning level 48. At 08:48:11 the far hold's
 * ground check (#744) and the revived sickness rule (#746) both answered
 * hearth, and at 08:48:27 the hearth read back "went home: map 1 to map 0".
 * Its bind was on map 0. It sailed back by 09:24, was held again on level 54
 * ground, and at 13:28:38 hearthed "map 1 to map 0" a second time. Each time
 * the coordinator then logged the family "is NOT waiting for ... Grog (on
 * another map)" and the family stayed split across two continents.
 *
 * These cases pin the rule: a ready stone counts as a way off lethal ground
 * only when its bind is on the map the member stands on. Otherwise the member
 * walks to a graveyard it can survive, is held, or (when revived sick) is held
 * out of combat, exactly as when the stone is not ready.
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
    auto sickCheck = [&failures](char const* name, RevivedSickGroundFacts const& facts,
                                 RevivedSickGroundStep want)
    {
        RevivedSickGroundStep const got = DecideRevivedSickGround(facts);
        if (got == want)
            return;
        std::printf("FAIL %s: got %u, wanted %u\n", name, unsigned(got), unsigned(want));
        ++failures;
    };

    // Grog at 08:48:11: level 43, held on level 48 ground, stone ready, bound
    // on map 0 while it and its leader stand on map 1.
    HeldGroundFacts grog;
    grog.memberLevel = 43;
    grog.groundTopLevel = 48;
    grog.levelGap = 3;
    grog.hearthReady = true;
    grog.boundOnMemberMap = false;
    check("a ready stone bound on another map is not a hearth", grog, HeldGroundStep::Hold);
    expect("a stone bound on another map still needs a safe spot",
           HeldGroundNeedsSafeSpot(grog), true);

    // With a graveyard it can survive, it walks there instead.
    grog.safeSpotKnown = true;
    grog.safeSpotTopLevel = 0;
    grog.safeSpotYards = 240.0f;
    check("a stone bound on another map walks to the graveyard", grog,
          HeldGroundStep::WalkToSafety);

    // The same member bound on this map hearths, as #744 shipped.
    grog.boundOnMemberMap = true;
    check("a ready stone bound on this map hearths", grog, HeldGroundStep::Hearth);
    expect("a usable stone needs no safe spot", HeldGroundNeedsSafeSpot(grog), false);

    // The bind is unread by default: no hearth until the adapter says where.
    HeldGroundFacts unread;
    unread.memberLevel = 43;
    unread.groundTopLevel = 54;
    unread.hearthReady = true;
    check("an unread bind is never hearthed to", unread, HeldGroundStep::Hold);

    // Grog's #746 line at 08:48:11: revived sick on level 48 ground, stone
    // ready, bound on map 0.
    RevivedSickGroundFacts sick;
    sick.sick = true;
    sick.memberLevel = 43;
    sick.groundTopLevel = 48;
    sick.levelGap = 3;
    sick.hearthReady = true;
    sick.boundOnMemberMap = false;
    sickCheck("a sick member bound on another map is held out of combat", sick,
              RevivedSickGroundStep::HoldOutOfCombat);
    sick.boundOnMemberMap = true;
    sickCheck("a sick member bound on this map hearths", sick, RevivedSickGroundStep::Hearth);

    // A death loop bound elsewhere is held out of combat too.
    RevivedSickGroundFacts loop;
    loop.sick = true;
    loop.memberLevel = 27;
    loop.groundTopLevel = 26;
    loop.repeatDeaths = 2;
    loop.deathsHere = 5;
    loop.hearthReady = true;
    loop.boundOnMemberMap = false;
    sickCheck("a death loop bound on another map is held out of combat", loop,
              RevivedSickGroundStep::HoldOutOfCombat);

    if (!failures)
        std::printf("held hearth other map: all checks passed\n");
    return failures ? 1 : 0;
}
