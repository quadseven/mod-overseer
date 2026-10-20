/*
 * A guild ghost still closing on its spirit healer when its walk runs out is
 * given more time, instead of being sent back to the corpse it was walked
 * away from.
 *
 * The live failure this pins (wow-overseer#744, dev realm): guild members
 * killed in Searing Gorge (zone 51) were released at the SE graveyard, which
 * stands on level 48 ground, so the ghost was rerouted to the Thorium Point
 * healer about 600 yards away with about 130 s allowed. Every walk ran out
 * first ("chose the spirit healer ... and never stood at one - the corpse
 * run goes back to the dead engine"), the corpse run took the ghost back
 * among the creatures that had just killed it, and it died again:
 *
 *   2026-10-19 13:47-13:58Z: a level 13 member died 4 times to level 46-50
 *   creatures, the healer walk running out after each of the first three.
 *   2026-10-20 05:16-05:46Z: a level 39 member died 10 times to level 47-49
 *   creatures, the healer walk running out 6 times.
 *
 * Each loop ended only once a corpse lay near Thorium Point, the healer was
 * reached, and the outmatched first-death hearth fired on the living member.
 *
 * GhostHealerWalkRenews renews a walk that has closed on the healer by at
 * least GHOST_HEALER_WALK_PROGRESS_YARDS since it began, up to
 * GHOST_HEALER_WALK_RENEWALS times. A ghost that did not get closer (a path
 * it cannot walk) still falls back to its corpse run as before.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GHOST_HEALER_WALK_PROGRESS_YARDS;
using OverseerDecisions::GHOST_HEALER_WALK_RENEWALS;
using OverseerDecisions::GhostHealerWalkRenews;

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

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main()
{
    // The live case: 600 yards from Thorium Point when the walk began, a
    // third of the way there when it ran out. It is renewed.
    Check("a ghost that closed 200 of 600 yards is renewed", GhostHealerWalkRenews(600.f, 400.f, 0));
    Check("the last renewal is still given",
          GhostHealerWalkRenews(600.f, 400.f, GHOST_HEALER_WALK_RENEWALS - 1));

    // Bounded: a walk renewed as often as allowed falls back as before.
    Check("no renewal past the cap", !GhostHealerWalkRenews(600.f, 400.f, GHOST_HEALER_WALK_RENEWALS));

    // A ghost that did not get closer cannot walk the path: corpse run.
    Check("a ghost that stood still is not renewed", !GhostHealerWalkRenews(600.f, 600.f, 0));
    Check("a ghost that walked away is not renewed", !GhostHealerWalkRenews(600.f, 650.f, 0));
    Check("closing by less than the margin is not progress",
          !GhostHealerWalkRenews(600.f, 600.f - GHOST_HEALER_WALK_PROGRESS_YARDS + 1.f, 0));
    Check("closing by the margin is progress",
          GhostHealerWalkRenews(600.f, 600.f - GHOST_HEALER_WALK_PROGRESS_YARDS, 0));

    // No reading taken when the walk began: nothing to measure progress by.
    Check("an unknown start is not renewed", !GhostHealerWalkRenews(-1.f, 100.f, 0));
    Check("an unknown distance now is not renewed", !GhostHealerWalkRenews(600.f, -1.f, 0));

    // The wiring: the spent-walk branch asks GhostHealerWalkRenews before it
    // hands the ghost back to its corpse run, and the walk's starting
    // distance is taken when the walk begins.
    std::string const src = ReadFile("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp was read", !src.empty());
    std::size_t const spent = src.find("if (now - st.healerSince > st.healerWalkSeconds");
    std::size_t const renews = src.find("OverseerDecisions::GhostHealerWalkRenews(", spent);
    std::size_t const marked = src.find("st.healerWalkSpent = true;", spent);
    Check("the spent-walk branch is found", spent != std::string::npos);
    Check("GhostHealerWalkRenews is asked in the spent-walk branch",
          renews != std::string::npos && marked != std::string::npos && renews < marked);
    Check("the walk's starting distance is recorded",
          src.find("st.healerWalkFromYards = ") != std::string::npos);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ghost healer walk renewal: all checks passed\n");
    return 0;
}
