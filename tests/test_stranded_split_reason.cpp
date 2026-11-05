/*
 * A member stranded on another continent starts its crossing, and a split is
 * never silent (#1143).
 *
 * Measured on the dev realm 2026-10-20: a member hearthed to map 1 by a failed
 * EXIT while its leader stood on map 0 stayed there 11 h 50 min. The log had,
 * for it, only the town trip leaving it behind and its ordinary job lines: no
 * split line, no stand-down line, no priced crossing. A restart cleared what
 * the module remembered, and 8 minutes later it walked to the berth, boarded
 * and landed on map 0. So the crossing works, and what held it was memory.
 *
 * The state reproduced here is the one the ticket names: alive on map 1, its
 * leader on map 0, a quest or town-run job (so no dungeon door decides for
 * it), and an escort or crossing record left from the exit episode. The party
 * poll read that state as StandDown: any escort that was not a catch-up or its
 * own crossing counted as a run's escort, and the family's crossing counted
 * from its clock alone. A stand-down is said once per change of way, and
 * DriveCatchUp returns without a word for a member an escort holds, so it was
 * held in silence. WhoOwnsStrandedWay is the rule that replaces both readings;
 * the reason line bounds the silence whatever the verdict.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <string>

using OverseerDecisions::DecideStrandedWay;
using OverseerDecisions::STRANDED_SPLIT_REASON_SECONDS;
using OverseerDecisions::StrandedFacts;
using OverseerDecisions::StrandedOwner;
using OverseerDecisions::StrandedOwners;
using OverseerDecisions::StrandedOwnerWhy;
using OverseerDecisions::StrandedSplitReasonDue;
using OverseerDecisions::StrandedSplitReasonLine;
using OverseerDecisions::StrandedWay;
using OverseerDecisions::StrandedWayExplanation;
using OverseerDecisions::StrandedWayName;
using OverseerDecisions::WhoOwnsStrandedWay;

namespace
{

int failures = 0;
int checks = 0;

void CheckWay(char const* what, StrandedWay got, StrandedWay want)
{
    ++checks;
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, StrandedWayName(got),
                StrandedWayName(want));
    ++failures;
}

void Check(char const* what, bool holds)
{
    ++checks;
    if (holds)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckSays(char const* what, std::string const& said, std::string const& fragment)
{
    ++checks;
    if (said.find(fragment) != std::string::npos)
        return;
    std::printf("FAIL %s: '%s' does not mention '%s'\n", what, said.c_str(), fragment.c_str());
    ++failures;
}

// Alive on map 1 with its leader on map 0, out of combat, its stone bound on
// its own side (Ratchet, as it was), and its own faction's boat serving both.
StrandedFacts OnMapOne()
{
    StrandedFacts f;
    f.readable = true;
    f.alive = true;
    f.memberMap = 1;
    f.leaderMap = 0;
    f.carriesStone = true;
    f.stoneReady = true;
    f.boundOnMemberMap = true;
    f.transportServes = true;
    return f;
}

// The facts with the owners read the way the party poll reads them.
StrandedFacts Owned(StrandedFacts f, StrandedOwners const& owners)
{
    f.familyOwnsTheWay = WhoOwnsStrandedWay(owners) != StrandedOwner::Nobody;
    return f;
}

// What the exit episode left behind: the family's crossing clock nothing reads
// any more (the family is on a town run, so its coordinator never reaches the
// crossing), and an escort entry that is not a run's.
StrandedOwners LeftFromTheExit()
{
    StrandedOwners o;
    o.familyCrossingOpen = true;
    o.familyCrossingRead = false;
    o.escort = true;
    o.escortRejoin = true;
    return o;
}

void TheStrandedMemberCrosses()
{
    StrandedOwners const left = LeftFromTheExit();
    Check("what the exit left behind decides nothing for the member",
          WhoOwnsStrandedWay(left) == StrandedOwner::Nobody);
    CheckWay("alive on map 1 with a boat home, it sails",
             DecideStrandedWay(Owned(OnMapOne(), left)), StrandedWay::Sail);

    StrandedFacts bound = OnMapOne();
    bound.boundOnMemberMap = false;
    bound.boundOnLeaderMap = true;
    CheckWay("bound on its leader's map with the stone ready, it hearths",
             DecideStrandedWay(Owned(bound, left)), StrandedWay::Hearth);

    // Each leftover alone, so neither can come back on its own.
    StrandedOwners rejoin;
    rejoin.escort = true;
    rejoin.escortRejoin = true;
    CheckWay("a walk back in left from the exit does not hold the boat",
             DecideStrandedWay(Owned(OnMapOne(), rejoin)), StrandedWay::Sail);

    StrandedOwners home;
    home.escort = true;
    home.escortHomeBind = true;
    CheckWay("a home errand does not hold the boat",
             DecideStrandedWay(Owned(OnMapOne(), home)), StrandedWay::Sail);

    StrandedOwners catchUp;
    catchUp.escort = true;
    catchUp.escortCatchUp = true;
    CheckWay("a catch-up walk never held it", DecideStrandedWay(Owned(OnMapOne(), catchUp)),
             StrandedWay::Sail);

    StrandedOwners ownBerth;
    ownBerth.escort = true;
    ownBerth.escortCrossing = true;
    CheckWay("nor its own walk to a berth", DecideStrandedWay(Owned(OnMapOne(), ownBerth)),
             StrandedWay::Sail);

    StrandedOwners clock;
    clock.familyCrossingOpen = true;
    CheckWay("a family crossing clock no poll reads does not hold it",
             DecideStrandedWay(Owned(OnMapOne(), clock)), StrandedWay::Sail);
}

void TheRealOwnersStillHoldIt()
{
    StrandedOwners crossing;
    crossing.familyCrossingOpen = true;
    crossing.familyCrossingRead = true;
    Check("a crossing the coordinator read is the family's",
          WhoOwnsStrandedWay(crossing) == StrandedOwner::FamilyCrossing);
    CheckWay("so it stands down", DecideStrandedWay(Owned(OnMapOne(), crossing)),
             StrandedWay::StandDown);

    StrandedOwners aboard;
    aboard.familyCrossingOpen = true;
    aboard.familyAboard = true;
    Check("a crossing with the family aboard is the family's however it was read",
          WhoOwnsStrandedWay(aboard) == StrandedOwner::FamilyCrossing);

    StrandedOwners run;
    run.escort = true;
    Check("a run's own escort is the run's", WhoOwnsStrandedWay(run) == StrandedOwner::RunEscort);
    CheckWay("so it stands down", DecideStrandedWay(Owned(OnMapOne(), run)),
             StrandedWay::StandDown);

    StrandedOwners inside;
    inside.insideRun = true;
    Check("a member inside an open run is the run's",
          WhoOwnsStrandedWay(inside) == StrandedOwner::InsideRun);

    StrandedOwners coming;
    coming.familyComesHere = true;
    Check("a family walking to a door on this continent is coming here",
          WhoOwnsStrandedWay(coming) == StrandedOwner::FamilyComesHere);

    Check("with none of them, nobody owns it",
          WhoOwnsStrandedWay(StrandedOwners{}) == StrandedOwner::Nobody);

    for (StrandedOwner owner : {StrandedOwner::Nobody, StrandedOwner::FamilyCrossing,
                                StrandedOwner::RunEscort, StrandedOwner::InsideRun,
                                StrandedOwner::FamilyComesHere})
        Check("every owner has a clause of its own",
              std::string(StrandedOwnerWhy(owner)) != "unknown" &&
                  !std::string(StrandedOwnerWhy(owner)).empty());
}

void ASplitIsNeverSilent()
{
    std::int64_t const bound = STRANDED_SPLIT_REASON_SECONDS;
    Check("the bound is fifteen minutes", bound == 15 * 60);
    Check("a fresh split says nothing yet", !StrandedSplitReasonDue(0, -1));
    Check("nor a second inside the bound", !StrandedSplitReasonDue(bound - 1, -1));
    Check("at the bound it says why", StrandedSplitReasonDue(bound, -1));
    Check("said, it is not said again at once", !StrandedSplitReasonDue(bound + 30, 30));
    Check("nor a second short of the next", !StrandedSplitReasonDue(bound * 2 - 1, bound - 1));
    Check("fifteen minutes on it is said again", StrandedSplitReasonDue(bound * 2, bound));
    Check("and for as long as the split lasts: twelve hours in",
          StrandedSplitReasonDue(12 * 60 * 60, bound));

    // ANY VERDICT: the line carries the member, both maps, how long, the
    // verdict's name and the sentence that verdict gives.
    for (StrandedWay way :
         {StrandedWay::NotStranded, StrandedWay::StandDown, StrandedWay::Wait, StrandedWay::Hearth,
          StrandedWay::Sail, StrandedWay::WaitForStone, StrandedWay::Refuse, StrandedWay::Portal})
    {
        std::string const why = StrandedWayExplanation(OnMapOne(), way);
        std::string const line =
            StrandedSplitReasonLine("Member", 1, 0, bound, StrandedWayName(way), why);
        Check("every verdict has a reason to name", !why.empty() && why != "unknown");
        CheckSays("the line names the member", line, "'Member'");
        CheckSays("the line names its map", line, "on map 1");
        CheckSays("the line names its leader's map", line, "its leader on map 0");
        CheckSays("the line says how long", line, "for 15 minutes");
        CheckSays("the line names the verdict", line, std::string("'") + StrandedWayName(way) + "'");
        CheckSays("the line says what holds it", line, why);
    }

    std::string const held = StrandedSplitReasonLine(
        "Member", 1, 0, 12 * 60 * 60, StrandedWayName(StrandedWay::StandDown),
        StrandedWayExplanation(OnMapOne(), StrandedWay::StandDown) + " (" +
            StrandedOwnerWhy(StrandedOwner::FamilyCrossing) + ")");
    CheckSays("a stand-down names which owner holds it", held, "the family's own crossing");
    CheckSays("and counts the whole split", held, "for 720 minutes");
}

}  // namespace

int main()
{
    TheStrandedMemberCrosses();
    TheRealOwnersStillHoldIt();
    ASplitIsNeverSilent();
    if (failures)
    {
        std::printf("%d of %d stranded split checks failed\n", failures, checks);
        return 1;
    }
    std::printf("all %d stranded split checks passed: the member left by the exit crosses, "
                "and a split past 15 minutes names its verdict and reason\n",
                checks);
    return 0;
}
