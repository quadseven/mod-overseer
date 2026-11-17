/*
 * The realm's deaths and level-ups are counted by hour, level band, zone and
 * killer, not by character (mod-overseer#1552). Compiled against
 * src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::RealmLevelBand;
using OverseerDecisions::RealmTally;
using OverseerDecisions::RealmTallyRow;

namespace
{

int failures = 0;
int checks = 0;

void Check(char const* what, bool got)
{
    ++checks;
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

constexpr std::int64_t HOUR = 3600;
constexpr std::int64_t T0 = 1000 * HOUR;  // the start of an hour

RealmTallyRow const* Find(std::vector<RealmTallyRow> const& rows, std::uint32_t band,
                          std::uint32_t zone, std::uint32_t killer, bool managed)
{
    for (RealmTallyRow const& row : rows)
        if (row.key.band == band && row.key.zone == zone && row.key.killerEntry == killer &&
            row.key.managed == managed)
            return &row;
    return nullptr;
}

void LevelsFallInTenLevelBands()
{
    Check("level 1 opens the first band", RealmLevelBand(1) == 1);
    Check("level 10 is still the first band", RealmLevelBand(10) == 1);
    Check("level 11 opens the second", RealmLevelBand(11) == 11);
    Check("level 60 is in the band opening at 51", RealmLevelBand(60) == 51);
    Check("level 0 is band 0, an unread level and not a crash", RealmLevelBand(0) == 0);
}

void DeathsAndLevelUpsOfOneHourShareARowPerKey()
{
    RealmTally tally;
    tally.NoteDeath(34, 17, 3000, false, T0 + 5);
    tally.NoteDeath(38, 17, 3000, false, T0 + 900);
    tally.NoteDeath(38, 17, 3001, false, T0 + 901);
    tally.NoteDeath(38, 17, 3000, true, T0 + 902);
    tally.NoteLevelUp(31, 17, false, T0 + 1000);
    Check("nothing rolls inside the hour", tally.Roll(T0 + HOUR - 1).empty());

    std::vector<RealmTallyRow> const rows = tally.Roll(T0 + HOUR);
    Check("one row per band, zone, killer and cohort", rows.size() == 4);
    RealmTallyRow const* both = Find(rows, 31, 17, 3000, false);
    Check("two deaths in a band by one killer share a row and carry the level-up key apart",
          both && both->counts.deaths == 2 && both->counts.levelUps == 0);
    RealmTallyRow const* other = Find(rows, 31, 17, 3001, false);
    Check("another killer is another row", other && other->counts.deaths == 1);
    RealmTallyRow const* managed = Find(rows, 31, 17, 3000, true);
    Check("a managed guild's deaths are told apart from the rest",
          managed && managed->counts.deaths == 1);
    RealmTallyRow const* up = Find(rows, 31, 17, 0, false);
    Check("a level-up has no killer and counts as a level-up",
          up && up->counts.levelUps == 1 && up->counts.deaths == 0);
    Check("every row carries the hour it counts", rows[0].hourStart == T0);
    Check("rolled once, rolled for good", tally.Roll(T0 + 2 * HOUR).empty());
}

void ARollWaitsForTheHourToEnd()
{
    RealmTally tally;
    tally.NoteDeath(20, 5, 0, false, T0 + 10);
    Check("the open hour is not written", tally.Roll(T0 + 20).empty());
    Check("the hour after it writes it", tally.Roll(T0 + HOUR + 1).size() == 1);
}

void AnEventOfALaterHourCloses_TheEarlierOne()
{
    RealmTally tally;
    tally.NoteDeath(20, 5, 0, false, T0 + 10);
    tally.NoteDeath(20, 5, 0, false, T0 + HOUR + 10);
    std::vector<RealmTallyRow> const rows = tally.Roll(T0 + HOUR + 20);
    Check("the earlier hour is ready while the later one is still open",
          rows.size() == 1 && rows[0].hourStart == T0 && rows[0].counts.deaths == 1);
    std::vector<RealmTallyRow> const later = tally.Roll(T0 + 2 * HOUR);
    Check("and the later hour follows on its own row",
          later.size() == 1 && later[0].hourStart == T0 + HOUR);
}

void TheTableIsSizedByKeyAndNeverByCharacter()
{
    RealmTally tally;
    // A hundred thousand distinct killers in one hour cannot grow it without bound.
    for (std::uint32_t i = 0; i < 100000; ++i)
        tally.NoteDeath(40, 100 + (i % 50), 10000 + i, false, T0 + 1);
    std::vector<RealmTallyRow> const rows = tally.Roll(T0 + HOUR);
    Check("the keys are capped", rows.size() <= OverseerDecisions::REALM_TALLY_MAX_KEYS + 50);
    std::uint64_t deaths = 0;
    for (RealmTallyRow const& row : rows)
        deaths += row.counts.deaths;
    Check("and no death is lost to the cap", deaths == 100000);
}

}  // namespace

int main()
{
    LevelsFallInTenLevelBands();
    DeathsAndLevelUpsOfOneHourShareARowPerKey();
    ARollWaitsForTheHourToEnd();
    AnEventOfALaterHourCloses_TheEarlierOne();
    TheTableIsSizedByKeyAndNeverByCharacter();
    std::printf("realm tally: %d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
