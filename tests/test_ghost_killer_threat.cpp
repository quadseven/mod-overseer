/*
 * The creature that killed a character at its corpse is a threat there,
 * whatever its level (wow-overseer#599).
 *
 * The live threat count near a corpse counted only creatures at or above the
 * character's own level. On wow-dev 2026-10-04, in the 49 minutes after a
 * roll, 83 of 311 repeat deaths were corpse runs judged clear while the pack
 * that had just killed the member stood beside the corpse: Barrens
 * plainstriders and zhevras, Bloodmyst and Silverpine wildlife, below the
 * member's level in 79 of the 83. The ghost reclaimed beside them at partial
 * health and died again.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::CorpseThreat;
using OverseerDecisions::CreatureKillMark;
using OverseerDecisions::DecideGhostRecovery;
using OverseerDecisions::GhostRecovery;
using OverseerDecisions::GhostRecoveryFacts;
using OverseerDecisions::GhostRecoveryReason;
using OverseerDecisions::KillerAtCorpse;

namespace
{

int failures = 0;

void Expect(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s\n", what, got ? "true" : "false");
    ++failures;
}

void ExpectEntry(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, got, want);
    ++failures;
}

constexpr uint32_t PLAINSTRIDER = 3244;
constexpr uint32_t ZHEVRA = 3242;
constexpr float RADIUS = 60.f;
constexpr int64_t WINDOW = 600;

}  // namespace

int main()
{
    // A level 12 member killed by a level 10 plainstrider pack.
    Expect("the killer's kind below the member's level is a threat",
           CorpseThreat(10, 12, PLAINSTRIDER, PLAINSTRIDER), true);
    Expect("a creature far below it of the killer's kind is still one",
           CorpseThreat(1, 12, PLAINSTRIDER, PLAINSTRIDER), true);
    Expect("another kind below its level is not",
           CorpseThreat(10, 12, ZHEVRA, PLAINSTRIDER), false);
    Expect("with no killer known, a creature below its level is not",
           CorpseThreat(10, 12, PLAINSTRIDER, 0), false);
    Expect("a creature at its level is, whatever killed it",
           CorpseThreat(12, 12, ZHEVRA, PLAINSTRIDER), true);
    Expect("a creature above its level is, with no killer known",
           CorpseThreat(14, 12, ZHEVRA, 0), true);

    // The kill mark names this corpse's killer only when it is this death.
    CreatureKillMark mark;
    mark.entry = PLAINSTRIDER;
    mark.mapId = 1;
    mark.x = -400.f;
    mark.y = -2000.f;
    mark.at = 5000;
    ExpectEntry("a kill at the corpse a minute ago is its killer",
                KillerAtCorpse(mark, 1, -410.f, -2005.f, RADIUS, 5060, WINDOW), PLAINSTRIDER);
    ExpectEntry("a kill just inside the radius counts",
                KillerAtCorpse(mark, 1, -400.f + 59.f, -2000.f, RADIUS, 5060, WINDOW),
                PLAINSTRIDER);
    ExpectEntry("a kill on another map is not",
                KillerAtCorpse(mark, 0, -410.f, -2005.f, RADIUS, 5060, WINDOW), 0);
    ExpectEntry("a kill farther than the radius away is not",
                KillerAtCorpse(mark, 1, -400.f + 61.f, -2000.f, RADIUS, 5060, WINDOW), 0);
    ExpectEntry("a kill older than the window is not",
                KillerAtCorpse(mark, 1, -410.f, -2005.f, RADIUS, 5000 + WINDOW + 1, WINDOW), 0);
    ExpectEntry("a kill stamped after now is not",
                KillerAtCorpse(mark, 1, -410.f, -2005.f, RADIUS, 4999, WINDOW), 0);
    CreatureKillMark none;
    ExpectEntry("no mark is no killer", KillerAtCorpse(none, 0, 0.f, 0.f, RADIUS, 10, WINDOW), 0);

    // What that does to the decision: the pack still beside the corpse is a
    // live threat, so a first death there waits for it to move off instead of
    // reclaiming beside it, and takes the spirit healer when it does not.
    {
        uint32_t const killer =
            KillerAtCorpse(mark, 1, -410.f, -2005.f, RADIUS, 5060, WINDOW);
        uint32_t const levels[] = {10, 11, 9};
        unsigned live = 0;
        for (uint32_t level : levels)
            live += CorpseThreat(level, 12, PLAINSTRIDER, killer) ? 1u : 0u;

        GhostRecoveryFacts f;
        f.level = 12;
        f.deathsHere = 1;
        f.strongestNearCorpse = 11;
        f.liveThreatsNearCorpse = live;
        f.ghostSeconds = 10;
        f.healerGraveyardSafe = true;
        f.corpseRunPossible = true;
        GhostRecovery const got = DecideGhostRecovery(f).choice;
        if (got != GhostRecovery::Wait || DecideGhostRecovery(f).reason !=
                                              GhostRecoveryReason::HostilesNearby)
        {
            std::printf("FAIL a first death beside its lower-level killers does not wait (%u "
                        "live)\n",
                        live);
            ++failures;
        }
        f.ghostSeconds = 90;
        if (DecideGhostRecovery(f).choice != GhostRecovery::SpiritHealer)
        {
            std::printf("FAIL killers that never leave the corpse do not send it to the "
                        "spirit healer\n");
            ++failures;
        }
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ghost killer threat: all checks passed\n");
    return 0;
}
