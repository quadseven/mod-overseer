/*
 * Who gets a death row and a level row (wow-overseer#533).
 *
 * overseer_death and the level history covered the five family characters and
 * nobody else, so the leveling research could not measure deaths per hour or
 * level-ups for the guilds. A managed-guild member is recorded now, with a
 * floor on how often one member's deaths are, and a stranger is still not.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::GUILD_DEATH_MIN_GAP_SECONDS;
using OverseerDecisions::RecordsDeath;
using OverseerDecisions::RecordsLevel;

namespace
{
int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got != want)
    {
        std::printf("FAIL %s: got %d want %d\n", what, got, want);
        ++failures;
    }
}

void AGuildMemberIsRecordedAndAStrangerIsNot()
{
    Check("guild member, first death", RecordsDeath(false, true, 0, 1000), true);
    Check("stranger", RecordsDeath(false, false, 0, 1000), false);
    Check("guild member level", RecordsLevel(false, true), true);
    Check("stranger level", RecordsLevel(false, false), false);
}

void TheFamilyKeepsEveryRow()
{
    Check("family, a second after the last", RecordsDeath(true, false, 1000, 1001), true);
    Check("family level", RecordsLevel(true, false), true);
}

void AGuildMemberInADeathLoopIsHeldToTheGap()
{
    std::int64_t const last = 5000;
    Check("one second later", RecordsDeath(false, true, last, last + 1), false);
    Check("just inside the gap",
          RecordsDeath(false, true, last, last + GUILD_DEATH_MIN_GAP_SECONDS - 1), false);
    Check("at the gap",
          RecordsDeath(false, true, last, last + GUILD_DEATH_MIN_GAP_SECONDS), true);
}
}  // namespace

int main()
{
    AGuildMemberIsRecordedAndAStrangerIsNot();
    TheFamilyKeepsEveryRow();
    AGuildMemberInADeathLoopIsHeldToTheGap();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a guild member's deaths and levels are recorded, bounded\n");
    return EXIT_SUCCESS;
}
