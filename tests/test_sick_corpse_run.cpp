/*
 * A sick ghost's corpse run is not cut short by the ladder (mod-overseer#746).
 *
 * DecideGhostRecovery answers CorpseRun for a member still carrying
 * Resurrection Sickness so the spirit healer does not stack a second dose, and
 * the stuck-revival ladder then took the spirit healer anyway 45 s into the
 * death. Replayed from wow-dev 2026-10-19 03:00:14: the verdict became
 * "corpse_run - it still carries Resurrection Sickness" 57 s into the death,
 * and the ladder walked the ghost to the healer in the same second. The
 * decision is asked poll by poll here, the way the drive asks it.
 */

#include "overseer_decisions.h"

#include <cstdio>

using namespace OverseerDecisions;

namespace
{
// GHOST_HEALER_WALK_SECONDS in mod_overseer.cpp.
constexpr int64_t BASE = 120;
}  // namespace

int main()
{
    int failures = 0;
    auto check = [&failures](char const* name, bool got, bool want)
    {
        if (got == want)
            return;
        std::printf("FAIL %s: got %d, wanted %d\n", name, int(got), int(want));
        ++failures;
    };

    // The 03:00:14 corpse, 90 yards from the ghost: the walk is shorter than
    // the base, so the base allowance stands.
    int64_t const allowance = GhostHealerWalkSeconds(90.f, BASE);
    check("a 90 yard corpse run gets the base allowance", allowance == BASE, true);

    SickCorpseRunFacts facts;
    facts.corpseRunChosen = true;
    facts.sick = true;
    facts.allowanceSeconds = allowance;

    // The #746 failure: the corpse run was chosen this very poll, and the
    // ladder took the spirit healer in the same second.
    facts.corpseRunSeconds = 0;
    check("a sick ghost's corpse run chosen this poll holds the ladder off",
          LadderWaitsForCorpseRun(facts), true);

    // 48 s: when the ladder took the healer in the other six cases.
    facts.corpseRunSeconds = 48;
    check("48 s into a sick ghost's corpse run the ladder still waits",
          LadderWaitsForCorpseRun(facts), true);

    // Every poll of the allowance waits, in order.
    for (int64_t s = 0; s < allowance; ++s)
    {
        facts.corpseRunSeconds = s;
        if (!LadderWaitsForCorpseRun(facts))
        {
            std::printf("FAIL the ladder took a sick ghost at %lld s of a %lld s corpse run\n",
                        (long long)s, (long long)allowance);
            ++failures;
            break;
        }
    }

    // A corpse run that never lands is still a stuck ghost: the ladder takes
    // it once the allowance is spent.
    facts.corpseRunSeconds = allowance;
    check("the ladder takes a sick ghost whose corpse run outlasts its allowance",
          LadderWaitsForCorpseRun(facts), false);

    // A far corpse gets the walk, not the base.
    int64_t const far = GhostHealerWalkSeconds(1400.f, BASE);
    check("a 1400 yard corpse run gets more than the base", far > BASE, true);
    facts.allowanceSeconds = far;
    facts.corpseRunSeconds = BASE;
    check("a far sick corpse run still holds the ladder off past the base",
          LadderWaitsForCorpseRun(facts), true);
    facts.allowanceSeconds = allowance;

    // Not sick: nothing to stack, so the ladder keeps its 45 s as before.
    facts.sick = false;
    facts.corpseRunSeconds = 0;
    check("a ghost that is not sick leaves the ladder as it was",
          LadderWaitsForCorpseRun(facts), false);
    facts.sick = true;

    // Not a corpse run: the ghost recovery's own answer stands.
    facts.corpseRunChosen = false;
    check("no corpse run chosen leaves the ladder as it was", LadderWaitsForCorpseRun(facts),
          false);
    facts.corpseRunChosen = true;

    // A clock that ran backwards is no reason to hold a ghost.
    facts.corpseRunSeconds = -5;
    check("a negative corpse-run age does not hold the ladder off",
          LadderWaitsForCorpseRun(facts), false);

    // The verdict this stands down for: a sick ghost at a clear corpse is a
    // corpse run (rule 2a), and that is the case the ladder must wait for.
    GhostRecoveryFacts ghost;
    ghost.level = 45;
    ghost.deathsHere = 1;
    ghost.ghostSeconds = 57;
    ghost.healerGraveyardSafe = true;
    ghost.sick = true;
    ghost.corpseRunPossible = true;
    GhostRecoveryVerdict const verdict = DecideGhostRecovery(ghost);
    check("the 03:00:14 ghost is a corpse run", verdict.choice == GhostRecovery::CorpseRun,
          true);
    SickCorpseRunFacts replay;
    replay.corpseRunChosen = verdict.choice == GhostRecovery::CorpseRun;
    replay.sick = ghost.sick;
    replay.corpseRunSeconds = 0;
    replay.allowanceSeconds = allowance;
    check("and the ladder waits for it", LadderWaitsForCorpseRun(replay), true);

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sick corpse run: all checks passed\n");
    return 0;
}
