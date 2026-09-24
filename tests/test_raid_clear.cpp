/*
 * The raid run inside the door (#640): HOLD, CLEAR and RECOVER, the stall
 * ladder, the wipe rule, when the dungeon brain may be armed on a raid map, and
 * the rows the run writes to the timeline.
 *
 * WHAT THIS FILE PROTECTS:
 *
 *   1. THE BRAIN IS ARMED ONLY IN CLEAR, and CLEAR is reached only through
 *      HOLD: a raid that is still gathering, eating or running back is never
 *      pulled for.
 *   2. HOLD IS BOUNDED BOTH WAYS. It settles before it clears, and it does not
 *      wait for ever on a straggler, a corpse or a drinker.
 *   3. A WIPE IS RECOVERED, NEVER ENDED. The dead are released only when nobody
 *      standing can raise them, the ghosts are walked back for a bounded while,
 *      and the head crosses last. No step of any phase releases the order.
 *   4. THE STALL LADDER REGROUPS BEFORE IT SKIPS, and has no rung that ends
 *      the run.
 *   5. THE TIMELINE WRITES EVERY STEP ONCE, and nothing for a poll that
 *      changed nothing.
 *
 * It compiles src/overseer_decisions.cpp and this file and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::DungeonClearMayArmHere;
using OverseerDecisions::RAID_CLEAR_REGROUPS;
using OverseerDecisions::RAID_ENTER_WAIT_SECONDS;
using OverseerDecisions::RAID_HOLD_SETTLE_SECONDS;
using OverseerDecisions::RAID_HOLD_WAIT_SECONDS;
using OverseerDecisions::RAID_RUNBACK_WAIT_SECONDS;
using OverseerDecisions::RAID_WIPE_PCT;
using OverseerDecisions::RaidClearStallAction;
using OverseerDecisions::RaidClearStallDecision;
using OverseerDecisions::RaidRunFacts;
using OverseerDecisions::RaidRunPhase;
using OverseerDecisions::RaidRunPhaseName;
using OverseerDecisions::RaidRunStep;
using OverseerDecisions::RaidTimelineEvents;
using OverseerDecisions::RaidTimelineSnapshot;
using OverseerDecisions::RaidWiped;
using OverseerDecisions::RunTimelineEvent;
using OverseerDecisions::StepRaidRun;

namespace
{

int failures = 0;

void Check(char const* what, bool held)
{
    if (held)
        return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

void CheckString(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected '%s', got '%s'\n", what, want.c_str(), got.c_str());
}

void CheckPhase(char const* what, RaidRunPhase got, RaidRunPhase want)
{
    CheckString(what, RaidRunPhaseName(got), RaidRunPhaseName(want));
}

// A streaming, living head inside with the whole raid in, alive, rested and
// settled.
RaidRunFacts InsideReady()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.raidFormed = true;
    facts.headInside = true;
    facts.headAlive = true;
    facts.inWorld = 40;
    facts.inside = 40;
    facts.insideAlive = 40;
    facts.heldSeconds = RAID_HOLD_SETTLE_SECONDS;
    return facts;
}

// The raid wiped inside: everyone dead, the head among them.
RaidRunFacts Wiped()
{
    RaidRunFacts facts = InsideReady();
    facts.headAlive = false;
    facts.insideAlive = 0;
    facts.insideDead = 40;
    facts.heldSeconds = 0;
    return facts;
}

void TheNamesAreThePhases()
{
    OverseerDecisions::RaidDoor const* door = OverseerDecisions::RaidDoorFor("moltencore");
    Check("molten core names its playerbots fight strategy",
          door && std::string(door->fightStrategy) == "moltencore");
    CheckString("HOLD", RaidRunPhaseName(RaidRunPhase::Hold), "HOLD");
    CheckString("CLEAR", RaidRunPhaseName(RaidRunPhase::Clear), "CLEAR");
    CheckString("RECOVER", RaidRunPhaseName(RaidRunPhase::Recover), "RECOVER");
}

void HoldSettlesThenClearsAndArmsOnlyThere()
{
    RaidRunFacts facts = InsideReady();
    RaidRunStep const arrived = StepRaidRun(RaidRunPhase::Enter, facts);
    CheckPhase("crossing lands in HOLD", arrived.phase, RaidRunPhase::Hold);
    Check("never straight into CLEAR", !arrived.armClear);
    Check("HOLD prepares the raid", arrived.prepare);

    facts.heldSeconds = RAID_HOLD_SETTLE_SECONDS - 1;
    RaidRunStep const settling = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("a ready raid still settles first", settling.phase, RaidRunPhase::Hold);
    Check("unarmed while it settles", !settling.armClear);

    facts.heldSeconds = RAID_HOLD_SETTLE_SECONDS;
    RaidRunStep const ready = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("in, alive, rested and settled clears", ready.phase, RaidRunPhase::Clear);
    Check("and CLEAR arms the brain", ready.armClear);
    Check("with nothing walked or released", !ready.aimDoor && !ready.aimStaging &&
                                                 !ready.releaseDead && !ready.runBack);

    RaidRunStep const clearing = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("CLEAR holds CLEAR", clearing.phase, RaidRunPhase::Clear);
    Check("and keeps the brain armed", clearing.armClear);
    Check("and does not stand it down", !clearing.standDown);

    facts.cleared = true;
    RaidRunStep const done = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("a cleared raid stays in CLEAR for the operator", done.phase,
               RaidRunPhase::Clear);
    Check("with nothing left to arm", !done.armClear);
    Check("and the order is not released by the run itself", !done.release);

    Check("a raid map arms in CLEAR", DungeonClearMayArmHere(true, true));
    Check("and in no other phase", !DungeonClearMayArmHere(true, false));
}

void HoldWaitsForTheRaidButNotForEver()
{
    RaidRunFacts facts = InsideReady();
    facts.inside = 35;
    facts.insideAlive = 35;
    RaidRunStep const outside = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("members still outside hold", outside.phase, RaidRunPhase::Hold);
    Check("and are knocked through if at the door", outside.knockMembers);

    facts = InsideReady();
    facts.insideAlive = 38;
    facts.insideDead = 2;
    RaidRunStep const corpses = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("two corpses inside hold for the healers", corpses.phase, RaidRunPhase::Hold);
    Check("and are not released", !corpses.releaseDead);

    facts = InsideReady();
    facts.insideResting = 6;
    RaidRunStep const drinking = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("a raid still drinking holds", drinking.phase, RaidRunPhase::Hold);

    facts = InsideReady();
    facts.insideCombat = true;
    facts.heldSeconds = RAID_HOLD_WAIT_SECONDS;
    RaidRunStep const fighting = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("a fight at the entrance is never cleared into", fighting.phase,
               RaidRunPhase::Hold);

    facts = InsideReady();
    facts.inside = 34;
    facts.insideAlive = 32;
    facts.insideDead = 2;
    facts.insideResting = 5;
    facts.heldSeconds = RAID_HOLD_WAIT_SECONDS;
    RaidRunStep const waited = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("after the wait the raid clears with whoever is ready", waited.phase,
               RaidRunPhase::Clear);
    Check("armed", waited.armClear);
}

void TheStallLadderRegroupsTwiceThenSkipsAndNeverEnds()
{
    Check("no stall, nothing", RaidClearStallDecision(false, 0, RAID_CLEAR_REGROUPS) ==
                                   RaidClearStallAction::Nothing);
    Check("first stall regroups", RaidClearStallDecision(true, 0, RAID_CLEAR_REGROUPS) ==
                                      RaidClearStallAction::Regroup);
    Check("second stall regroups", RaidClearStallDecision(true, 1, RAID_CLEAR_REGROUPS) ==
                                       RaidClearStallAction::Regroup);
    Check("third stall skips", RaidClearStallDecision(true, 2, RAID_CLEAR_REGROUPS) ==
                                   RaidClearStallAction::Skip);
    Check("the ladder has two regroups", RAID_CLEAR_REGROUPS == 2);

    RaidRunFacts facts = InsideReady();
    facts.stallRegroup = true;
    RaidRunStep const regroup = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("a regroup goes back to HOLD", regroup.phase, RaidRunPhase::Hold);
    Check("and tells the brain to let go", regroup.standDown);
    Check("and is not armed", !regroup.armClear);
    Check("and does not end the order", !regroup.release);

    RaidRunStep const holding = StepRaidRun(RaidRunPhase::Hold, facts);
    Check("the stall flag means nothing outside CLEAR", !holding.standDown);
}

void AWipeIsTheBrainsNinetyPercent()
{
    RaidRunFacts facts = InsideReady();
    Check("nobody dead is no wipe", !RaidWiped(facts));
    facts.insideAlive = 0;
    facts.insideDead = 40;
    Check("nobody alive inside is a wipe", RaidWiped(facts));
    facts.insideAlive = 4;
    facts.insideDead = 36;
    Check("ninety percent dead and quiet is a wipe", RaidWiped(facts));
    facts.insideCombat = true;
    Check("but not while somebody is still fighting", !RaidWiped(facts));
    facts.insideCombat = false;
    facts.insideAlive = 5;
    facts.insideDead = 35;
    Check("eighty-seven percent is not", !RaidWiped(facts));
    facts.insideAlive = 0;
    facts.insideDead = 0;
    Check("an empty map is not a wipe", !RaidWiped(facts));
    Check("the brain's own number", RAID_WIPE_PCT == 90);
}

void AWipeIsRecoveredByRunningBack()
{
    RaidRunFacts facts = Wiped();
    RaidRunStep const wipe = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("a wipe in CLEAR is RECOVER", wipe.phase, RaidRunPhase::Recover);
    Check("the brain is told to let go", wipe.standDown);
    Check("the dead are released", wipe.releaseDead);
    Check("the ghosts are walked back", wipe.runBack);
    Check("the order is not released", !wipe.release);
    Check("nothing is armed", !wipe.armClear);

    // The dead released: every ghost is outside now, the head among them.
    facts.headInside = false;
    facts.inside = 0;
    facts.insideDead = 0;
    facts.ghosts = 40;
    facts.heldSeconds = 30;
    RaidRunStep const running = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("the run-back is RECOVER", running.phase, RaidRunPhase::Recover);
    Check("and still walks the ghosts", running.runBack);
    Check("and knocks whoever reaches the door", running.knockMembers);
    Check("with nothing left inside to release", !running.releaseDead);
    Check("and the brain already told once", !running.standDown);

    facts.headAssembled = true;
    facts.othersAssembled = 3;
    RaidRunStep const waits = StepRaidRun(RaidRunPhase::Recover, facts);
    Check("the head at the door waits for ghosts standing in it", !waits.knockHead);
    facts.othersAssembled = 0;
    RaidRunStep const crosses = StepRaidRun(RaidRunPhase::Recover, facts);
    Check("and crosses last", crosses.knockHead);
    Check("a ghost head is not aimed by the travel drive", !crosses.aimDoor);
    facts.othersAssembled = 3;
    facts.heldSeconds = RAID_ENTER_WAIT_SECONDS;
    Check("or when the wait at the door is over",
          StepRaidRun(RaidRunPhase::Recover, facts).knockHead);

    Check("a ghost is walked inside its window",
          OverseerDecisions::RaidGhostMayBeWalked(RAID_RUNBACK_WAIT_SECONDS - 1));
    Check("and left to the ordinary revival after it",
          !OverseerDecisions::RaidGhostMayBeWalked(RAID_RUNBACK_WAIT_SECONDS));

    // The head crossed first (raised at the entrance); the raid's ghosts are
    // still on their way. RECOVER holds until the last walked ghost is in.
    facts = InsideReady();
    facts.inside = 12;
    facts.insideAlive = 12;
    facts.ghosts = 28;
    facts.heldSeconds = 200;
    RaidRunStep const coming = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("ghosts still walking keep RECOVER", coming.phase, RaidRunPhase::Recover);
    Check("and are still walked", coming.runBack);
    Check("with nobody released", !coming.releaseDead);
    Check("and nothing armed", !coming.armClear);

    // A ghost past its window counts no more: the run goes on without it.
    facts.ghosts = 0;
    RaidRunStep const onward = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("no ghost left to walk is HOLD", onward.phase, RaidRunPhase::Hold);

    // The head back inside, raised by the core at the entrance, the raid
    // coming in behind him: HOLD again, and CLEAR only through it.
    facts = InsideReady();
    facts.inside = 30;
    facts.insideAlive = 30;
    facts.heldSeconds = 400;
    RaidRunStep const back = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("back inside is HOLD, not CLEAR", back.phase, RaidRunPhase::Hold);
    Check("and unarmed", !back.armClear);
    Check("and prepared again", back.prepare);

    // A single ghost during HOLD or CLEAR (a death nobody raised) is walked
    // back too, without leaving the phase.
    facts = InsideReady();
    facts.ghosts = 1;
    RaidRunStep const clearing = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("one ghost does not leave CLEAR", clearing.phase, RaidRunPhase::Clear);
    Check("but is walked back", clearing.runBack);
    facts.headStreaming = false;
    Check("unless the head has no client", !StepRaidRun(RaidRunPhase::Clear, facts).runBack);
}

void AHeadOutsideWithTheRaidStandingWalksBack()
{
    RaidRunFacts facts = InsideReady();
    facts.headInside = false;
    facts.inside = 39;
    facts.insideAlive = 39;
    RaidRunStep const out = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("a head outside after the raid is in is RECOVER", out.phase,
               RaidRunPhase::Recover);
    Check("the living raid inside is not released", !out.releaseDead);
    Check("the head walks to the staging point", out.aimStaging);
    Check("the brain is told to let go", out.standDown);

    facts.headStreaming = false;
    RaidRunStep const noClient = StepRaidRun(RaidRunPhase::Recover, facts);
    CheckPhase("a head without a client holds RECOVER", noClient.phase,
               RaidRunPhase::Recover);
    Check("and nothing walks", !noClient.aimStaging && !noClient.aimDoor &&
                                   !noClient.runBack && !noClient.knockMembers &&
                                   !noClient.knockHead);
}

void AHeadDeadInsideIsLeftToTheHealers()
{
    RaidRunFacts facts = InsideReady();
    facts.headAlive = false;
    facts.insideAlive = 39;
    facts.insideDead = 1;
    RaidRunStep const dead = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("CLEAR holds with the head dead and the raid standing", dead.phase,
               RaidRunPhase::Clear);
    Check("and nobody is released", !dead.releaseDead);
    Check("and the brain stays armed for the next tank", dead.armClear);

    RaidRunStep const holding = StepRaidRun(RaidRunPhase::Hold, facts);
    CheckPhase("HOLD waits for him", holding.phase, RaidRunPhase::Hold);
    Check("unarmed", !holding.armClear);
}

void AnEndedOrderStillReleasesFromInside()
{
    RaidRunFacts facts = InsideReady();
    facts.ordered = false;
    RaidRunStep const ended = StepRaidRun(RaidRunPhase::Clear, facts);
    CheckPhase("an ended order goes idle from CLEAR", ended.phase, RaidRunPhase::Idle);
    Check("and releases", ended.release);
}

void TheTimelineWritesEachStepOnce()
{
    RaidTimelineSnapshot idle;
    RaidTimelineSnapshot hold;
    hold.phase = "HOLD";
    hold.why = "the head is inside; the raid gathers at the entrance";
    hold.keyword = "moltencore";
    hold.bossesTotal = 10;

    std::vector<RunTimelineEvent> const nothing = RaidTimelineEvents(hold, hold);
    Check("a poll that changed nothing writes nothing", nothing.empty());

    RaidTimelineSnapshot enter = hold;
    enter.phase = "ENTER";
    std::vector<RunTimelineEvent> const arrived = RaidTimelineEvents(enter, hold);
    Check("a phase change is one row", arrived.size() == 1);
    if (arrived.size() == 1)
    {
        CheckString("kind", arrived[0].kind, "phase");
        CheckString("detail says both phases and why", arrived[0].detail,
                    "ENTER -> HOLD: the head is inside; the raid gathers at the entrance");
        CheckString("the keyword is the portal", arrived[0].portal, "moltencore");
        Check("the first attempt", arrived[0].runNumber == 1);
    }

    RaidTimelineSnapshot marked = hold;
    marked.mainTankMarked = true;
    std::vector<RunTimelineEvent> const tank = RaidTimelineEvents(hold, marked);
    Check("the main tank mark is one row", tank.size() == 1 && tank[0].kind == "main_tank");

    RaidTimelineSnapshot clear = marked;
    clear.phase = "CLEAR";
    clear.why = "the raid is in, alive and rested; the dungeon brain clears";
    RaidTimelineSnapshot lucifron = clear;
    lucifron.bossesDone = 1;
    std::vector<RunTimelineEvent> const boss = RaidTimelineEvents(clear, lucifron);
    Check("a boss is one row", boss.size() == 1);
    if (boss.size() == 1)
    {
        CheckString("boss kind", boss[0].kind, "boss");
        CheckString("boss detail", boss[0].detail, "1 encounter done, 1 of 10 in all");
    }

    RaidTimelineSnapshot recover = lucifron;
    recover.phase = "RECOVER";
    recover.why = "the raid wiped; the dead are released and run back";
    recover.wipes = 1;
    recover.released = 40;
    std::vector<RunTimelineEvent> const wipe = RaidTimelineEvents(lucifron, recover);
    Check("a wipe is three rows", wipe.size() == 3);
    if (wipe.size() == 3)
    {
        CheckString("decisions first", wipe[0].kind, "wipe");
        CheckString("then the release", wipe[1].kind, "released");
        CheckString("released detail", wipe[1].detail,
                    "40 corpses released for the run back");
        CheckString("the phase last", wipe[2].kind, "phase");
        Check("stamped as the second attempt", wipe[2].runNumber == 2);
    }

    RaidTimelineSnapshot regrouped = clear;
    regrouped.phase = "HOLD";
    regrouped.regroups = 1;
    std::vector<RunTimelineEvent> const regroup = RaidTimelineEvents(clear, regrouped);
    Check("a regroup is a decision and a phase", regroup.size() == 2 &&
                                                     regroup[0].kind == "regroup" &&
                                                     regroup[1].kind == "phase");

    RaidTimelineSnapshot skipped = clear;
    skipped.skips = 1;
    std::vector<RunTimelineEvent> const skip = RaidTimelineEvents(clear, skipped);
    Check("a skip is one row", skip.size() == 1 && skip[0].kind == "dc_skip");

    RaidTimelineSnapshot done = clear;
    done.cleared = true;
    std::vector<RunTimelineEvent> const cleared = RaidTimelineEvents(clear, done);
    Check("cleared is one row", cleared.size() == 1 && cleared[0].kind == "cleared");

    std::vector<RunTimelineEvent> const released = RaidTimelineEvents(recover, idle);
    Check("an ended run writes its phase change and no decisions",
          released.size() == 1 && released[0].kind == "phase");
    if (released.size() == 1)
        CheckString("stamped with the run that ended", released[0].portal, "moltencore");
}

}  // namespace

int main()
{
    TheNamesAreThePhases();
    HoldSettlesThenClearsAndArmsOnlyThere();
    HoldWaitsForTheRaidButNotForEver();
    TheStallLadderRegroupsTwiceThenSkipsAndNeverEnds();
    AWipeIsTheBrainsNinetyPercent();
    AWipeIsRecoveredByRunningBack();
    AHeadOutsideWithTheRaidStandingWalksBack();
    AHeadDeadInsideIsLeftToTheHealers();
    AnEndedOrderStillReleasesFromInside();
    TheTimelineWritesEachStepOnce();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the raid clear decisions hold\n");
    return EXIT_SUCCESS;
}
