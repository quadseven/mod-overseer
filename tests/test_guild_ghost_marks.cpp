/*
 * A natural guild member's deaths, remembered by the module.
 *
 * The ghost drive's repeat test counts overseer_death rows, and only the
 * family writes those. The natural guilds' random-bot members die in the loop
 * that test was cut for: sampled on the dev realm every 30 s for ten minutes
 * (2026-09-27), 66 of them died 194 times, alive a median 83 s between deaths
 * and a ghost a median 101 s, beside the same corpse. These pin the marks the
 * module keeps for them instead.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::CountGuildDeathsNear;
using OverseerDecisions::GuildGhostDriven;
using OverseerDecisions::GuildDeathMark;
using OverseerDecisions::NoteGuildDeath;
using OverseerDecisions::PruneGuildDeathMarks;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    int64_t const now = 1790546400;  // 2026-09-27 22:00:00 UTC
    std::vector<GuildDeathMark> marks;

    // Westfall, two deaths 83 s apart and 20 yards apart: the loop.
    Check(NoteGuildDeath(marks, {now - 190, 0, -10004.f, 1788.f}), "the first death is noted");
    Check(!NoteGuildDeath(marks, {now - 190, 0, -10004.f, 1788.f}),
          "the same death seen on the next poll is not noted twice");
    Check(NoteGuildDeath(marks, {now - 5, 0, -9991.f, 1772.f}), "the second death is noted");
    Check(CountGuildDeathsNear(marks, now, 0, -9991.f, 1772.f, 60.f, 10) == 2,
          "two deaths within 60 yards in 10 minutes count as a repeat");

    // Another map, or too far away, is somewhere else.
    Check(CountGuildDeathsNear(marks, now, 1, -9991.f, 1772.f, 60.f, 10) == 0,
          "a corpse on another map shares no deaths");
    Check(CountGuildDeathsNear(marks, now, 0, -9900.f, 1772.f, 60.f, 10) == 0,
          "a corpse 91 yards off shares no deaths");

    // Older than the window: neither counted nor kept.
    Check(NoteGuildDeath(marks, {now - 11 * 60, 0, -9991.f, 1772.f}), "an old death is noted");
    Check(CountGuildDeathsNear(marks, now, 0, -9991.f, 1772.f, 60.f, 10) == 2,
          "a death older than the window does not count");
    PruneGuildDeathMarks(marks, now, 10);
    Check(marks.size() == 2, "pruning drops only the death older than the window");

    // Who the drive steers: a random bot off the roster, and nobody else. The
    // first deploy gated on the roster's own test and steered nobody.
    Check(GuildGhostDriven(false, true, false, true), "a guild random bot is steered");
    Check(!GuildGhostDriven(true, true, false, true), "a family member is left to its own drive");
    Check(!GuildGhostDriven(false, false, true, false), "a player's character is never steered");
    Check(!GuildGhostDriven(false, true, false, false), "a bot with no AI is not steered");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild ghost marks: all checks passed\n");
    return 0;
}
