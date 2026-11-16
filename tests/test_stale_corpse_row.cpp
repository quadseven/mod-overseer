/*
 * A corpse row left over from an earlier death is not the clock of the
 * death in front of us.
 *
 * The defect: the stuck-revival drive reads the ghost time from the `corpse`
 * table whenever the map-scoped Player::GetCorpse() finds nothing, and it did
 * so for ANY character, ghost or not, and for ANY row. A character that died
 * and was raised without that row being deleted kept it. Its next death, still
 * on the release prompt with no corpse yet, was then timed from the row: a
 * character dead for three seconds was reported as dead for 31153 seconds
 * (the age of its previous death). Two wrong decisions follow. The drive's
 * grace wait is skipped, so the ladder starts at once and the healer in the
 * party never gets its window. And the release-on-its-behalf block, which only
 * runs when there is no ghost time, is skipped, so a character with nobody at
 * the keyboard is never released.
 *
 * Compiled against src/overseer_decisions.cpp; the last part reads
 * src/mod_overseer.cpp (run from the repo root) to pin the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

// Times from the live case: the old row, and the death three seconds old.
constexpr std::int64_t OLD_ROW = 1763207832;       // the earlier death's release
constexpr std::int64_t NEW_DEATH = OLD_ROW + 31150;  // booked by the death hook

void AnUnreleasedBodyHasNoCorpseRow()
{
    Check("a character still on the release prompt owns no corpse row",
          !CorpseRowBelongsToThisDeath(/*ghost*/ false, OLD_ROW, NEW_DEATH));
    Check("even a row newer than the death is not read before a release",
          !CorpseRowBelongsToThisDeath(false, NEW_DEATH + 5, NEW_DEATH));
    Check("and with no death booked at all (after a restart)",
          !CorpseRowBelongsToThisDeath(false, OLD_ROW, 0));
}

void AGhostReadsOnlyARowFromItsOwnDeath()
{
    Check("a ghost's row made after the death is its own",
          CorpseRowBelongsToThisDeath(true, NEW_DEATH + 2, NEW_DEATH));
    Check("a row made in the same second as the death is its own",
          CorpseRowBelongsToThisDeath(true, NEW_DEATH, NEW_DEATH));
    Check("a ghost's row from before the booked death is a stale one",
          !CorpseRowBelongsToThisDeath(true, OLD_ROW, NEW_DEATH));
}

void WithoutADeathBookedTheGhostFlagStands()
{
    Check("after a restart no death is booked, so a ghost's row is read",
          CorpseRowBelongsToThisDeath(true, OLD_ROW, 0));
    Check("an empty row (time 0) is never a clock",
          !CorpseRowBelongsToThisDeath(true, 0, 0));
}

void TheDriveIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }

    auto const at = source.find("FROM corpse WHERE guid = {}");
    Check("the corpse-table fallback was found", at != std::string::npos);
    if (at == std::string::npos)
        return;
    std::string const around = source.substr(at, 900);
    Check("the fallback row is judged by CorpseRowBelongsToThisDeath with the ghost flag and the booked death",
          Has(around, "CorpseRowBelongsToThisDeath(") && Has(around, "LastDeathAt(name)") &&
              Has(around, "PLAYER_FLAGS_GHOST"));

    // ORDER, not presence: no corpse field may be taken from the row before the
    // helper has judged it, or the drive could regress and this stay green.
    auto const judged = around.find("CorpseRowBelongsToThisDeath(");
    auto const firstUse = around.find("= f[");
    auto const firstRead = around.find("f[0]");
    Check("the row's time is read only to be judged, then fields are taken inside the guard",
          judged != std::string::npos && firstRead != std::string::npos &&
              firstUse != std::string::npos && judged < firstUse);
    auto const ghostAssigned = around.find("ghostTime = ");
    Check("ghostTime is never assigned from the row before the helper has judged it",
          ghostAssigned != std::string::npos && judged < ghostAssigned);
    auto const mapAssigned = around.find("corpseMap = ");
    Check("the corpse map is never assigned from the row before the helper has judged it",
          mapAssigned != std::string::npos && judged < mapAssigned);
}

}  // namespace

int main()
{
    AnUnreleasedBodyHasNoCorpseRow();
    AGhostReadsOnlyARowFromItsOwnDeath();
    WithoutADeathBookedTheGhostFlagStands();
    TheDriveIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_stale_corpse_row: all passed\n");
    return 0;
}
