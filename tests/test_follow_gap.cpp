/*
 * How far behind its leader a follower is, and whether that is a number at all.
 *
 * The live failure this pins is a silence. On the dev realm 2026-09-05 the
 * leader was in Duskwood and four followers were in the Barrens, and the same
 * pair of players was read twice in one loop by two expressions that
 * disagreed. One of them subtracted two coordinate systems and acted on the
 * result:
 *
 *   17:42:30 WARN 'Grog' has not moved more than 10 yards in over 5 minutes and
 *                 is 10560 yards from 'Grug' - clearing its movement so the
 *                 next follow tick starts fresh
 *   17:45:30 WARN 'Ugga' ... and is 9463 yards from 'Grug' ...
 *   17:46:30 WARN 'Og'   ... and is 10642 yards from 'Grug' ...
 *
 * The other folded the same pair to a sentinel of -1, which is less than the
 * formation line, so it took the same exit as a follower standing beside its
 * leader and said nothing. Eleven minutes, four followers, not one line.
 *
 * Nothing here reunites anybody, and these cases are written so that a reader
 * cannot mistake them for a fix: `follow` cannot cross a map and neither can an
 * `at:` aim. What the verdict buys is that a split party stops looking exactly
 * like a party that is merely slow.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using OverseerDecisions::FollowGap;
using OverseerDecisions::FollowGapIsBehind;
using OverseerDecisions::FollowGapLimits;
using OverseerDecisions::FollowGapName;
using OverseerDecisions::ReadFollowGap;

namespace
{

int failures = 0;

void CheckGap(char const* what, FollowGap got, FollowGap want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, FollowGapName(got),
                FollowGapName(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// The live constants: FOLLOW_STALL_GAP_YARDS is upstream's own SightDistance,
// and FOLLOW_CATCH_UP_YARDS is five times it.
FollowGapLimits const LIVE{100.f, 500.f};

// THE INCIDENT. Every distance the stall check reported during the split, fed
// in with the map answer that was missing from it. None of them is a distance.
void TheReportedCrossMapDistancesAreNotDistances()
{
    float const reported[] = {10560.f, 9463.f, 10642.f};
    for (float yards : reported)
    {
        CheckGap("a cross-map pair has no distance to be far in",
                 ReadFollowGap(false, yards, LIVE), FollowGap::SplitAcrossMaps);
        Check("and it is not a follower that has fallen behind",
              FollowGapIsBehind(ReadFollowGap(false, yards, LIVE)), false);
    }

    // The distance handed in is not consulted AT ALL when the maps differ, so
    // a caller that computed one anyway cannot change the answer with it.
    float const anything[] = {-1.f, 0.f, 1.f, 99.f, 100.f, 501.f, 1e9f};
    for (float yards : anything)
        CheckGap("no distance overrules a map boundary",
                 ReadFollowGap(false, yards, LIVE), FollowGap::SplitAcrossMaps);
}

// THE GUARD IS ABOUT THE MAP AND NOT ABOUT THE MAGNITUDE. The same ten
// thousand yards on ONE map is a real reading and is acted on exactly as
// before, so this change does not quietly weaken the stall nudge.
void TheSameNumberOnOneMapIsStillARealReading()
{
    CheckGap("10560 yards on one map is a stranded follower",
             ReadFollowGap(true, 10560.f, LIVE), FollowGap::Stranded);
    Check("and that one IS behind", FollowGapIsBehind(ReadFollowGap(true, 10560.f, LIVE)),
          true);
}

// THE OLD SENTINEL, AND WHY IT WAS THE BUG. -1 is less than the formation
// line, so `gap <= FOLLOW_STALL_GAP_YARDS` answered the same for a follower
// standing beside its leader and for one on another continent, and that exit
// returns without a word.
void ASplitIsNoLongerIndistinguishableFromStandingBeside()
{
    FollowGap const beside = ReadFollowGap(true, 3.f, LIVE);
    FollowGap const ocean = ReadFollowGap(false, 10560.f, LIVE);
    CheckGap("beside the leader", beside, FollowGap::InFormation);
    CheckGap("across an ocean", ocean, FollowGap::SplitAcrossMaps);
    Check("the two are now different answers", beside != ocean, true);
    // The old expression could not tell them apart. Shown rather than
    // asserted about, so the regression is legible.
    float const oldBeside = 3.f;
    float const oldOcean = -1.f;
    Check("the old reading gave both the same answer",
          (oldBeside <= LIVE.formationYards) == (oldOcean <= LIVE.formationYards),
          true);
}

// The two lines, and both are inclusive at the near side so a follower sitting
// exactly on one does not flap between two verdicts poll to poll.
void TheLinesAreWhereTheConstantsPutThem()
{
    CheckGap("just inside the formation line", ReadFollowGap(true, 99.9f, LIVE),
             FollowGap::InFormation);
    CheckGap("exactly on it", ReadFollowGap(true, 100.f, LIVE),
             FollowGap::InFormation);
    CheckGap("just past it", ReadFollowGap(true, 100.1f, LIVE),
             FollowGap::Trailing);
    CheckGap("exactly on the catch-up line", ReadFollowGap(true, 500.f, LIVE),
             FollowGap::Trailing);
    CheckGap("just past that", ReadFollowGap(true, 500.1f, LIVE),
             FollowGap::Stranded);
    CheckGap("standing on the leader", ReadFollowGap(true, 0.f, LIVE),
             FollowGap::InFormation);
}

// Only a follower with a real distance and a real gap is worth nudging. This
// is the predicate the stall check reads, and the second half of it is the fix.
void OnlyARealGapIsWorthANudge()
{
    Check("in formation is not behind", FollowGapIsBehind(FollowGap::InFormation),
          false);
    Check("trailing is behind", FollowGapIsBehind(FollowGap::Trailing), true);
    Check("stranded is behind", FollowGapIsBehind(FollowGap::Stranded), true);
    Check("split across maps is NOT behind, which is the fix",
          FollowGapIsBehind(FollowGap::SplitAcrossMaps), false);
}

// A verdict a log line names has to have four distinct names.
void EveryVerdictHasItsOwnName()
{
    FollowGap const all[] = {FollowGap::SplitAcrossMaps, FollowGap::InFormation,
                             FollowGap::Trailing, FollowGap::Stranded};
    for (FollowGap a : all)
    {
        Check("a name is never empty", FollowGapName(a)[0] != '\0', true);
        for (FollowGap b : all)
        {
            if (a == b)
                continue;
            Check("two verdicts never share a name",
                  std::strcmp(FollowGapName(a), FollowGapName(b)) != 0, true);
        }
    }
}

// THE INVARIANT: a map boundary is answered before anything else, always, over
// every distance and every pair of limits a caller could pass. The point is
// that no tuning of the two lines can ever make a cross-map pair look like a
// follower this module could walk somewhere.
void AMapBoundaryIsAlwaysAnsweredFirst()
{
    float const yards[] = {-1e6f, -1.f, 0.f, 0.5f, 99.f, 100.f, 250.f,
                           500.f, 501.f, 10560.f, 1e6f};
    float const formation[] = {0.f, 1.f, 100.f, 500.f};
    float const catchUp[] = {0.f, 100.f, 500.f, 1e6f};
    for (float f : formation)
        for (float c : catchUp)
        {
            FollowGapLimits const limits{f, c};
            for (float y : yards)
            {
                Check("a cross-map pair is always a split",
                      ReadFollowGap(false, y, limits) == FollowGap::SplitAcrossMaps,
                      true);
                Check("and is never something to nudge",
                      FollowGapIsBehind(ReadFollowGap(false, y, limits)), false);
                // And a same-map pair is never reported as a split, whatever
                // the numbers are, so the verdict cannot invent one.
                Check("a same-map pair is never a split",
                      ReadFollowGap(true, y, limits) != FollowGap::SplitAcrossMaps,
                      true);
            }
        }
}

}  // namespace

int main()
{
    TheReportedCrossMapDistancesAreNotDistances();
    TheSameNumberOnOneMapIsStillARealReading();
    ASplitIsNoLongerIndistinguishableFromStandingBeside();
    TheLinesAreWhereTheConstantsPutThem();
    OnlyARealGapIsWorthANudge();
    EveryVerdictHasItsOwnName();
    AMapBoundaryIsAlwaysAnsweredFirst();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a follower on another map is a split, not a distance\n");
    return EXIT_SUCCESS;
}
