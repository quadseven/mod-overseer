/*
 * Corpse run, spirit healer or wait: what a released character does next.
 *
 * The live failure this pins is a death loop. On the dev realm a Horde family
 * at levels 25 to 27 kept dying in Ashenvale near Demon Fall Canyon to Roaming
 * Felguards (level 28-30, spawned 18 yards from the corpse point) and Searing
 * Infernals (level 29-30, 46 yards). The dead engine walked each ghost back to
 * its corpse and reclaimed it there:
 *
 *   09:05:36  Zork dies at (2025, -3035) to a Searing Infernal
 *   09:06:33  Zork dies at (2056, -3046) to a Roaming Felguard, 32 yards away,
 *             with no revival from the module in between: the corpse run
 *   07:08:22  Oz dies at (2051, -3023)
 *   07:11:47  Oz dies at (2051, -3023) again
 *
 * A player takes the spirit healer after the second death in one place, or
 * when what stands near the corpse is well above it, and waits a while when
 * what stands there could move off.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

using OverseerDecisions::DecideGhostRecovery;
using OverseerDecisions::GhostRecovery;
using OverseerDecisions::GhostRecoveryFacts;
using OverseerDecisions::GhostRecoveryLimits;
using OverseerDecisions::GhostRecoveryReason;
using OverseerDecisions::GhostRecoveryVerdict;
using OverseerDecisions::GhostRecoveryWord;

namespace
{

int failures = 0;

void Check(char const* what, GhostRecoveryVerdict got, GhostRecovery want,
           GhostRecoveryReason wantWhy)
{
    if (got.choice == want && got.reason == wantWhy)
        return;
    std::printf("FAIL %s: got %s (reason %d), wanted %s (reason %d)\n", what,
                GhostRecoveryWord(got.choice), static_cast<int>(got.reason),
                GhostRecoveryWord(want), static_cast<int>(wantWhy));
    ++failures;
}

// A level 26 ghost with a safe graveyard, dead ten seconds, nothing near.
GhostRecoveryFacts Quiet()
{
    GhostRecoveryFacts f;
    f.level = 26;
    f.deathsHere = 1;
    f.strongestNearCorpse = 0;
    f.liveThreatsNearCorpse = 0;
    f.ghostSeconds = 10;
    f.healerGraveyardSafe = true;
    f.choseHealer = false;
    return f;
}

}  // namespace

int main()
{
    // The ordinary death: nothing near the corpse. Corpse run, as before.
    Check("a first death with nothing near the corpse runs back",
          DecideGhostRecovery(Quiet()), GhostRecovery::CorpseRun, GhostRecoveryReason::Clear);

    {
        GhostRecoveryFacts f = Quiet();
        f.deathsHere = 2;
        f.healerUsedRecently = true;
        f.corpseRunPossible = true;
        Check("recent spirit-healer use prefers a possible corpse run",
              DecideGhostRecovery(f), GhostRecovery::CorpseRun, GhostRecoveryReason::Clear);
    }

    // Zork at 09:06:33: the second death inside the window, in one place.
    {
        GhostRecoveryFacts f = Quiet();
        f.deathsHere = 2;
        Check("a second death here takes the spirit healer", DecideGhostRecovery(f),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::RepeatDeaths);
        f.deathsHere = 3;
        Check("a third death here takes the spirit healer", DecideGhostRecovery(f),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::RepeatDeaths);
    }

    // Zork at 09:05:36, the FIRST death: a level 30 Felguard spawns 18 yards
    // from the corpse and Zork is level 26. That is the death the loop starts
    // from, so it must not be a corpse run either.
    {
        GhostRecoveryFacts f = Quiet();
        f.strongestNearCorpse = 30;
        Check("a first death beside a level 30 at level 26 takes the spirit healer",
              DecideGhostRecovery(f), GhostRecovery::SpiritHealer,
              GhostRecoveryReason::Outlevelled);
    }

    // The gap is a gap: exactly three levels is well above, two is not.
    {
        GhostRecoveryFacts f = Quiet();
        f.strongestNearCorpse = 29;
        Check("three levels above is well above", DecideGhostRecovery(f),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::Outlevelled);
        f.strongestNearCorpse = 28;
        Check("two levels above with nothing alive there now is a corpse run",
              DecideGhostRecovery(f), GhostRecovery::CorpseRun, GhostRecoveryReason::Clear);
    }

    // Something at or above its level is standing there now, not well above:
    // wait for it to move off.
    {
        GhostRecoveryFacts f = Quiet();
        f.strongestNearCorpse = 27;
        f.liveThreatsNearCorpse = 1;
        Check("a live threat near the corpse is waited for", DecideGhostRecovery(f),
              GhostRecovery::Wait, GhostRecoveryReason::HostilesNearby);

        f.ghostSeconds = 89;
        Check("still waiting one second before the limit", DecideGhostRecovery(f),
              GhostRecovery::Wait, GhostRecoveryReason::HostilesNearby);

        f.ghostSeconds = 90;
        Check("a corpse that never cleared goes to the spirit healer", DecideGhostRecovery(f),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::WaitedOut);

        f.ghostSeconds = 30;
        f.liveThreatsNearCorpse = 0;
        Check("once it clears the ghost runs back", DecideGhostRecovery(f),
              GhostRecovery::CorpseRun, GhostRecoveryReason::Clear);
    }

    // A choice made is kept, even when the corpse has since cleared.
    {
        GhostRecoveryFacts f = Quiet();
        f.choseHealer = true;
        Check("the spirit healer once chosen is kept", DecideGhostRecovery(f),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::AlreadyChose);
    }

    // Wherever the spirit healer would be the answer and its graveyard is
    // unsafe, the ghost is still kept off the corpse and handed to the ladder.
    {
        GhostRecoveryFacts f = Quiet();
        f.healerGraveyardSafe = false;
        f.deathsHere = 2;
        Check("repeat deaths with an unsafe graveyard go to the ladder",
              DecideGhostRecovery(f), GhostRecovery::Ladder, GhostRecoveryReason::RepeatDeaths);

        f = Quiet();
        f.healerGraveyardSafe = false;
        f.strongestNearCorpse = 30;
        Check("outlevelled with an unsafe graveyard goes to the ladder",
              DecideGhostRecovery(f), GhostRecovery::Ladder, GhostRecoveryReason::Outlevelled);

        f = Quiet();
        f.healerGraveyardSafe = false;
        Check("a clear corpse runs back whatever the graveyard is like",
              DecideGhostRecovery(f), GhostRecovery::CorpseRun, GhostRecoveryReason::Clear);
    }

    // The limits are the caller's to set.
    {
        GhostRecoveryLimits limits;
        limits.repeatDeaths = 3;
        limits.levelGap = 5;
        limits.waitSeconds = 30;
        GhostRecoveryFacts f = Quiet();
        f.deathsHere = 2;
        f.strongestNearCorpse = 30;
        Check("a looser limit lets a second death and a four-level gap run back",
              DecideGhostRecovery(f, limits), GhostRecovery::CorpseRun,
              GhostRecoveryReason::Clear);
        f.liveThreatsNearCorpse = 1;
        f.ghostSeconds = 30;
        Check("and waits only as long as it is told", DecideGhostRecovery(f, limits),
              GhostRecovery::SpiritHealer, GhostRecoveryReason::WaitedOut);
    }

    // The column words are fixed; overseer_death is queried by them.
    if (std::strcmp(GhostRecoveryWord(GhostRecovery::CorpseRun), "corpse_run") ||
        std::strcmp(GhostRecoveryWord(GhostRecovery::SpiritHealer), "spirit_healer") ||
        std::strcmp(GhostRecoveryWord(GhostRecovery::Wait), "wait") ||
        std::strcmp(GhostRecoveryWord(GhostRecovery::Ladder), "ladder"))
    {
        std::printf("FAIL the ghost_recovery column words changed\n");
        ++failures;
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ghost recovery: all checks passed\n");
    return 0;
}
