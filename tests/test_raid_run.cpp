/*
 * The raid run's first slice: which door, which job arms it, how one poll
 * moves FORM -> ASSEMBLE -> ENTER -> INSIDE, who is knocked through the door in
 * which order, that the dungeon brain is never armed on a raid map this
 * slice did not order cleared, and who is summoned at the mountain's meeting
 * stones once the raid has walked in (#922).
 *
 * WHAT THIS FILE PROTECTS, because "the machine steps" is not it:
 *
 *   1. ONLY AN ORDER STARTS IT. A job that is not exactly `raid:<known door>`
 *      is Idle, and an order that ends releases whatever the run held.
 *   2. NOTHING WALKS WITHOUT A STREAMING HEAD. The operator's rule for any
 *      instance run; a head with no client holds the run where it is.
 *   3. THE HEAD GOES LAST. Crossing first takes the anchor the raid follows
 *      away from it, which is the dungeon coordinator's measured lesson.
 *   4. A RAID MAP IS NOT CLEARED BY ACCIDENT. DriveDungeonClear arms any
 *      instance map; this slice walks forty characters into one.
 *
 * It compiles src/overseer_decisions.cpp and this file and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ChooseRaidStoneSummon;
using OverseerDecisions::DungeonClearMayArmHere;
using OverseerDecisions::RAID_ASSEMBLE_WAIT_SECONDS;
using OverseerDecisions::RAID_ENTER_WAIT_SECONDS;
using OverseerDecisions::RAID_FORM_WAIT_SECONDS;
using OverseerDecisions::RAID_STONE_SUMMON_AFTER_SECONDS;
using OverseerDecisions::RAID_STONE_SUMMON_TRIES;
using OverseerDecisions::RaidDoor;
using OverseerDecisions::RaidDoorFor;
using OverseerDecisions::RaidKeywordForJob;
using OverseerDecisions::RaidKnockOrder;
using OverseerDecisions::RaidRunFacts;
using OverseerDecisions::RaidSeatNeedsRepair;
using OverseerDecisions::RaidPullRepairAction;
using OverseerDecisions::RaidPullRepairDecision;
using OverseerDecisions::RaidPullRepairForSeats;
using OverseerDecisions::RaidPullSeatRepairStep;
using OverseerDecisions::RaidPullSeatStep;
using OverseerDecisions::RaidRunPhase;
using OverseerDecisions::RaidRunPhaseName;
using OverseerDecisions::RaidRunStep;
using OverseerDecisions::RaidStone;
using OverseerDecisions::RaidStoneSeat;
using OverseerDecisions::RaidStoneSummon;
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

void TheRepairFloorIsStrictAndConfigurable()
{
    Check("below the floor needs repair", RaidSeatNeedsRepair(69, 70));
    Check("at the floor does not need repair", !RaidSeatNeedsRepair(70, 70));
    Check("above the floor does not need repair", !RaidSeatNeedsRepair(100, 70));
    Check("zero floor disables this gate", !RaidSeatNeedsRepair(0, 0));
}

void OnlyMajordomoAndRagnarosPullsWaitForRepair()
{
    Check("Majordomo pull holds below-floor seats",
          RaidPullRepairDecision(8, true, 10, 300) == RaidPullRepairAction::Hold);
    Check("Ragnaros pull holds below-floor seats",
          RaidPullRepairDecision(9, true, 10, 300) == RaidPullRepairAction::Hold);
    Check("Majordomo resumes when all seats meet floor",
          RaidPullRepairDecision(8, false, 10, 300) == RaidPullRepairAction::Resume);
    Check("Ragnaros resumes at the time limit",
          RaidPullRepairDecision(9, true, 300, 300) == RaidPullRepairAction::TimedOut);
    Check("other boss pulls do not hold",
          RaidPullRepairDecision(7, true, 10, 300) == RaidPullRepairAction::Ignore);
    Check("first boss pull does not hold",
          RaidPullRepairDecision(0, true, 10, 300) == RaidPullRepairAction::Ignore);
}

// The run feeds each readable seat's lowest durability to the gate; this is
// the decision the pull acts on, including the timeout that must be logged as
// a timeout and not as every seat having repaired.
void ThePullHoldReadsEverySeat()
{
    std::vector<unsigned> const mixed = {100, 69, 90};
    std::vector<unsigned> const healthy = {100, 70, 90};
    Check("one seat under the floor holds Majordomo",
          RaidPullRepairForSeats(8, mixed, 70, 10, 300) == RaidPullRepairAction::Hold);
    Check("one seat under the floor holds Ragnaros",
          RaidPullRepairForSeats(9, mixed, 70, 10, 300) == RaidPullRepairAction::Hold);
    Check("seats at the floor resume the pull",
          RaidPullRepairForSeats(9, healthy, 70, 10, 300) == RaidPullRepairAction::Resume);
    Check("the time limit ends the hold as TimedOut",
          RaidPullRepairForSeats(9, mixed, 70, 300, 300) == RaidPullRepairAction::TimedOut);
    Check("a mid-raid boss never holds",
          RaidPullRepairForSeats(4, mixed, 70, 10, 300) == RaidPullRepairAction::Ignore);
    Check("no readable seats resume",
          RaidPullRepairForSeats(8, {}, 70, 10, 300) == RaidPullRepairAction::Resume);
}

// Each seat's step inside the hold, as the run's seat loop acts on it: a seat
// under the floor walks or repairs and holds the pull, is charged once, and is
// let go (not walked) once the hold has timed out.
void EachSeatInThePullHoldWalksRepairsOrWaits()
{
    Check("a seat at the floor is ready",
          RaidPullSeatRepairStep(70, 70, false, false, false) == RaidPullSeatStep::Ready);
    Check("a seat under the floor with no repairer walks",
          RaidPullSeatRepairStep(69, 70, false, false, false) == RaidPullSeatStep::Walk);
    Check("a seat under the floor at a repairer repairs",
          RaidPullSeatRepairStep(69, 70, false, false, true) == RaidPullSeatStep::Repair);
    Check("a seat already sent through repair is not charged again",
          RaidPullSeatRepairStep(69, 70, false, true, true) == RaidPullSeatStep::Attempted);
    Check("a timed-out hold stops walking the seat",
          RaidPullSeatRepairStep(69, 70, true, false, false) == RaidPullSeatStep::TimedOut);
    Check("a timed-out hold leaves a ready seat ready",
          RaidPullSeatRepairStep(90, 70, true, false, false) == RaidPullSeatStep::Ready);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

// The decisions above only matter if the raid run acts on them. This reads the
// run's pre-pull block in src/mod_overseer.cpp (run from the repo root) and
// checks it logs every pull, gates the hold on the decision, repairs through
// the ordinary verb, logs an unpaid seat, and pauses the brain only while held.
void ThePrePullHoldIsWiredIntoTheRun()
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
    std::size_t const at = source.find("// Read the seats before every next encounter.");
    Check("the pre-pull block exists", at != std::string::npos);
    std::size_t const end = at == std::string::npos ? at : source.find("RaidClearStall(", at);
    std::string const body = end == std::string::npos ? "" : source.substr(at, end - at);
    Check("every pull logs the lowest durability line",
          Has(body, "overseer: raid run for family '{}': lowest durability {} {}%"));
    Check("a pull with no readable seat is logged, not skipped",
          Has(body, "cannot log pre-pull"));
    Check("the hold is gated on the boss decision",
          Has(body, "RaidPullRepairDecision(bossesDone, false, 0, 300) !=\n"
                    "                    OverseerDecisions::RaidPullRepairAction::Ignore"));
    Check("the hold reads every seat", Has(body, "RaidPullRepairForSeats("));
    Check("each seat steps through the seat decision", Has(body, "RaidPullSeatRepairStep("));
    Check("the wall clock is read once per poll",
          Has(body, "long const heldNow = long(std::time(nullptr) - run.repairHoldSince);"));
    Check("a seat at a repairer pays through the ordinary verb",
          Has(body, "DoRepair(member, \"all\", status, evidence)"));
    Check("a seat that cannot pay is logged", Has(body, "could not repair '{}'"));
    Check("the end of the hold is logged as a timeout or as complete",
          Has(body, "timed out after 300 seconds") &&
              Has(body, "complete - every seat is at or above"));
    Check("the brain pauses only while held and not timed out",
          Has(body, "bool const shouldPause = repairHold && !repairHoldTimedOut;"));
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

// A streaming head, an ordered raid, formed, everyone in the world at the door.
RaidRunFacts Ready()
{
    RaidRunFacts facts;
    facts.ordered = true;
    facts.headStreaming = true;
    facts.raidFormed = true;
    facts.addable = 0;
    facts.inWorld = 35;
    facts.assembled = 35;
    facts.othersAssembled = 34;
    facts.headAssembled = true;
    return facts;
}

void TheMoltenCoreDoorIsTheWorldsOwnNumbers()
{
    RaidDoor const* door = RaidDoorFor("moltencore");
    Check("molten core has a door", door != nullptr);
    if (!door)
        return;
    Check("entered from Blackrock Mountain's own map", door->outsideMapId == 0);
    Check("through the window trigger", door->entryTriggerId == 3529);
    Check("onto the Molten Core map", door->insideMapId == 409);
    Check("and out by the exit trigger", door->exitTriggerId == 2890);
    Check("staged on the exit's landing", door->stageX == -7508.32f &&
                                              door->stageY == -1039.74f &&
                                              door->stageZ == 180.912f);
    Check("the access row's level", door->minLevel == 50);
    Check("no other spelling is a door", RaidDoorFor("molten-core") == nullptr);
    Check("nor a dungeon keyword", RaidDoorFor("blackrock-depths") == nullptr);
}

void OnlyAnExactRaidJobNamesADoor()
{
    CheckString("the ordered job", RaidKeywordForJob("raid:moltencore"), "moltencore");
    CheckString("a dungeon job is not a raid", RaidKeywordForJob("dungeon:ragefire"), "");
    CheckString("a raid with no door", RaidKeywordForJob("raid:onyxia"), "");
    CheckString("the bare prefix", RaidKeywordForJob("raid:"), "");
    CheckString("quest", RaidKeywordForJob("quest"), "");
    CheckString("raid prep is a job mode, not a raid", RaidKeywordForJob("raid prep"), "");
}

void NoOrderIsIdleAndAnEndedOrderReleases()
{
    RaidRunFacts facts = Ready();
    facts.ordered = false;
    RaidRunStep const idle = StepRaidRun(RaidRunPhase::Idle, facts);
    CheckPhase("no order stays idle", idle.phase, RaidRunPhase::Idle);
    Check("and releases nothing it never held", !idle.release);
    Check("and forms nothing", !idle.form && !idle.aimStaging && !idle.knockMembers);

    RaidRunStep const ended = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("an ended order goes idle", ended.phase, RaidRunPhase::Idle);
    Check("and releases what it held", ended.release);
}

void AHeadWithoutAClientHoldsTheRunWhereItIs()
{
    RaidRunFacts facts = Ready();
    facts.headStreaming = false;
    RaidRunStep const idle = StepRaidRun(RaidRunPhase::Idle, facts);
    CheckPhase("an order with no streaming head does not start", idle.phase,
               RaidRunPhase::Idle);
    Check("and forms nothing", !idle.form);

    RaidRunStep const held = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("a run in progress holds its phase", held.phase, RaidRunPhase::Assemble);
    Check("and walks nobody", !held.aimStaging && !held.aimDoor && !held.knockMembers &&
                                  !held.knockHead);
}

void AnOrderFormsFirstAndKeepsFormingWhileSeatsCanJoin()
{
    RaidRunFacts facts = Ready();
    facts.raidFormed = false;
    RaidRunStep const start = StepRaidRun(RaidRunPhase::Idle, facts);
    CheckPhase("an order starts in FORM", start.phase, RaidRunPhase::Form);
    Check("and forms", start.form);

    RaidRunStep const notYet = StepRaidRun(RaidRunPhase::Form, facts);
    CheckPhase("not a raid yet stays in FORM", notYet.phase, RaidRunPhase::Form);

    facts.raidFormed = true;
    facts.addable = 3;
    RaidRunStep const more = StepRaidRun(RaidRunPhase::Form, facts);
    CheckPhase("seats still addable stay in FORM", more.phase, RaidRunPhase::Form);
    Check("and keep inviting", more.form);

    facts.heldSeconds = RAID_FORM_WAIT_SECONDS;
    RaidRunStep const stuck = StepRaidRun(RaidRunPhase::Form, facts);
    CheckPhase("a seat that will not join does not hold the raid in FORM for ever",
               stuck.phase, RaidRunPhase::Assemble);
    Check("but is still asked while the head walks", stuck.form && stuck.aimStaging);

    facts.heldSeconds = 0;
    facts.addable = 0;
    RaidRunStep const done = StepRaidRun(RaidRunPhase::Form, facts);
    CheckPhase("formed with everyone addable moves to ASSEMBLE", done.phase,
               RaidRunPhase::Assemble);
    Check("and walks the head to the door", done.aimStaging);
    Check("and invites nobody more", !done.form);
    Check("and knocks nobody yet", !done.knockMembers && !done.knockHead);
}

void AssembleWaitsForTheRaidThenForNobodyForever()
{
    RaidRunFacts facts = Ready();
    facts.assembled = 20;
    facts.heldSeconds = 60;
    RaidRunStep const waiting = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("half the raid at the door waits", waiting.phase, RaidRunPhase::Assemble);
    Check("with the head held on the staging point", waiting.aimStaging);

    facts.headAssembled = false;
    facts.assembled = 35;
    RaidRunStep const walking = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("the raid without its head does not enter", walking.phase,
               RaidRunPhase::Assemble);

    facts.headAssembled = true;
    RaidRunStep const all = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("everyone at the door enters", all.phase, RaidRunPhase::Enter);
    Check("the head walks onto the door", all.aimDoor);
    Check("and the members are knocked", all.knockMembers);
    Check("but not the head, on the first poll", !all.knockHead);

    facts.assembled = 20;
    facts.heldSeconds = RAID_ASSEMBLE_WAIT_SECONDS;
    RaidRunStep const late = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("after the wait, the raid goes with who is there", late.phase,
               RaidRunPhase::Enter);

    facts.raidFormed = false;
    RaidRunStep const broken = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("a group that stopped being a raid goes back to FORM", broken.phase,
               RaidRunPhase::Form);

    facts.raidFormed = true;
    facts.addable = 2;
    facts.assembled = 20;
    facts.heldSeconds = 60;
    RaidRunStep const latecomers = StepRaidRun(RaidRunPhase::Assemble, facts);
    Check("a seat that logged in during ASSEMBLE is still invited", latecomers.form);
}

void TheHeadCrossesLast()
{
    RaidRunFacts facts = Ready();
    facts.assembled = 12;   // the head and eleven still outside
    facts.othersAssembled = 11;
    facts.inside = 23;
    facts.heldSeconds = 30;
    RaidRunStep const waiting = StepRaidRun(RaidRunPhase::Enter, facts);
    CheckPhase("members still outside keep ENTER", waiting.phase, RaidRunPhase::Enter);
    Check("members are knocked", waiting.knockMembers);
    Check("the head waits for them", !waiting.knockHead);

    facts.assembled = 1;   // only the head
    facts.othersAssembled = 0;
    facts.inside = 34;
    RaidRunStep const last = StepRaidRun(RaidRunPhase::Enter, facts);
    Check("the head crosses once he is the last one out", last.knockHead);

    // THE HEAD NOT COUNTED AT THE DOOR (out of the world, or walked off) is not
    // subtracted from the others: one member still outside keeps him waiting.
    facts.assembled = 1;
    facts.othersAssembled = 1;
    facts.headAssembled = false;
    RaidRunStep const notHim = StepRaidRun(RaidRunPhase::Enter, facts);
    Check("a member still outside is not mistaken for the head", !notHim.knockHead);
    facts.headAssembled = true;

    facts.assembled = 12;
    facts.othersAssembled = 11;
    facts.heldSeconds = RAID_ENTER_WAIT_SECONDS;
    RaidRunStep const waited = StepRaidRun(RaidRunPhase::Enter, facts);
    Check("and after the wait at the door, whatever is left", waited.knockHead);

    std::vector<std::string> const order =
        RaidKnockOrder({"Grug", "Annian", "Selie"}, "Grug", true);
    Check("three knocked", order.size() == 3);
    if (order.size() == 3)
    {
        CheckString("members first", order[0], "Annian");
        CheckString("in the order they stand", order[1], "Selie");
        CheckString("the head last", order[2], "Grug");
    }
    std::vector<std::string> const held =
        RaidKnockOrder({"Grug", "Annian"}, "Grug", false);
    Check("a head who may not cross is not knocked", held.size() == 1 && held[0] == "Annian");
    std::vector<std::string> const away = RaidKnockOrder({"Annian"}, "Grug", true);
    Check("a head not in the door is not knocked", away.size() == 1 && away[0] == "Annian");
}

void InsideHoldsAndLetsStragglersThrough()
{
    RaidRunFacts facts = Ready();
    facts.headInside = true;
    facts.headAlive = true;
    facts.headAssembled = false;
    RaidRunStep const inside = StepRaidRun(RaidRunPhase::Enter, facts);
    CheckPhase("the head inside is HOLD", inside.phase, RaidRunPhase::Hold);
    Check("stragglers at the door are still knocked", inside.knockMembers);
    Check("nothing walks the head anywhere", !inside.aimDoor && !inside.aimStaging);
    Check("and nothing forms", !inside.form);
    Check("and the brain is not armed on arrival", !inside.armClear);
}

// A raid member in the world, alive, movable, nowhere near the door.
RaidStoneSeat Seat(char const* name, unsigned level)
{
    RaidStoneSeat seat;
    seat.name = name;
    seat.inWorld = true;
    seat.alive = true;
    seat.summonable = true;
    seat.level = level;
    return seat;
}

RaidStoneSeat AtStone(char const* name, unsigned level, int stone)
{
    RaidStoneSeat seat = Seat(name, level);
    seat.atStone = stone;
    return seat;
}

// The two Blackrock Mountain stones as the dev realm's templates read them.
std::vector<RaidStone> Stones()
{
    RaidStone high;
    high.entry = 179587;
    high.minLevel = 55;
    RaidStone low;
    low.entry = 179586;
    low.minLevel = 48;
    return {high, low};
}

void TheDoorNamesTheMountainsTwoStones()
{
    RaidDoor const* door = RaidDoorFor("moltencore");
    if (!door)
        return;
    Check("the level 55 stone", door->meetingStones[0] == 179587);
    Check("the level 48 stone", door->meetingStones[1] == 179586);
    CheckString("named for the log", door->stonesName, "Blackrock Mountain");
}

void StragglersAreSummonedOnlyAfterTheWalk()
{
    RaidRunFacts facts = Ready();
    facts.assembled = 30;
    facts.heldSeconds = RAID_STONE_SUMMON_AFTER_SECONDS - 1;
    RaidRunStep const walking = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("still assembling", walking.phase, RaidRunPhase::Assemble);
    Check("nobody is summoned while the raid is still walking in", !walking.summonStragglers);

    facts.heldSeconds = RAID_STONE_SUMMON_AFTER_SECONDS;
    RaidRunStep const late = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("after the walk it still assembles", late.phase, RaidRunPhase::Assemble);
    Check("and the stragglers are summoned", late.summonStragglers);

    facts.headAssembled = false;
    RaidRunStep const headAway = StepRaidRun(RaidRunPhase::Assemble, facts);
    Check("not while the head is still walking to the door", !headAway.summonStragglers);
    facts.headAssembled = true;

    facts.assembled = facts.inWorld;
    RaidRunStep const all = StepRaidRun(RaidRunPhase::Assemble, facts);
    CheckPhase("everyone there enters", all.phase, RaidRunPhase::Enter);
    Check("and nobody is summoned at ENTER", !all.summonStragglers);
}

void AStragglerIsSummonedByASeatAtTheStone()
{
    RaidStoneSeat head = AtStone("Grug", 60, 0);
    head.head = true;
    RaidStoneSeat atDoor = Seat("Annian", 60);
    atDoor.assembled = true;
    std::vector<RaidStoneSeat> seats{head, atDoor, Seat("Selie", 60),
                                     AtStone("Bork", 60, 0), AtStone("Og", 52, 0)};
    RaidStoneSummon const choice = ChooseRaidStoneSummon(seats, Stones());
    Check("a summon is chosen", choice.summon);
    CheckString("the seat away from the door", choice.straggler, "Selie");
    CheckString("clicked by a seat at the stone, never the head", choice.clicker, "Bork");
    CheckString("the portal by another seat there", choice.helper, "Og");
    Check("at the stone they stand by", choice.stone == 0);

    // One seat at the stone is not a ritual.
    std::vector<RaidStoneSeat> alone{head, Seat("Selie", 60), AtStone("Bork", 60, 0)};
    RaidStoneSummon const one = ChooseRaidStoneSummon(alone, Stones());
    Check("one clicker summons nobody", !one.summon);

    // Nobody away is nobody to summon.
    std::vector<RaidStoneSeat> there{head, atDoor, AtStone("Bork", 60, 0),
                                     AtStone("Og", 60, 0)};
    Check("everyone at the door is nothing to do",
          !ChooseRaidStoneSummon(there, Stones()).summon);

    // A seat that cannot be moved is skipped for the next.
    RaidStoneSeat dead = Seat("Annian", 60);
    dead.alive = false;
    RaidStoneSeat noAi = Seat("Selie", 60);
    noAi.summonable = false;
    RaidStoneSeat tried = Seat("Kaz", 60);
    tried.tries = RAID_STONE_SUMMON_TRIES;
    std::vector<RaidStoneSeat> skips{dead, noAi, tried, Seat("Mog", 60),
                                     AtStone("Bork", 60, 0), AtStone("Og", 60, 0)};
    RaidStoneSummon const next = ChooseRaidStoneSummon(skips, Stones());
    CheckString("the dead, the unmovable and the tried out are passed over", next.straggler,
                "Mog");
}

void TheStonesMinimumIsKeptForBothEnds()
{
    // A level 50 straggler cannot use the level 55 stone; at the 48 stone he can.
    std::vector<RaidStoneSeat> high{Seat("Selie", 50), AtStone("Bork", 60, 0),
                                    AtStone("Og", 60, 0)};
    RaidStoneSummon const below = ChooseRaidStoneSummon(high, Stones());
    Check("a seat below the stone's minimum is not summoned", !below.summon);
    Check("and the reason says it keeps walking",
          below.why.find("minimum") != std::string::npos);

    // The minimum itself is enough, at either stone.
    std::vector<RaidStoneSeat> atFloor{Seat("Selie", 55), AtStone("Bork", 60, 0),
                                       AtStone("Og", 60, 0)};
    RaidStoneSummon const exact = ChooseRaidStoneSummon(atFloor, Stones());
    Check("a level 55 seat is summoned at the 55 stone", exact.summon && exact.stone == 0);
    std::vector<RaidStoneSeat> atLowFloor{Seat("Selie", 48), AtStone("Bork", 60, 1),
                                          AtStone("Og", 60, 1)};
    RaidStoneSummon const lowExact = ChooseRaidStoneSummon(atLowFloor, Stones());
    Check("a level 48 seat is summoned at the 48 stone",
          lowExact.summon && lowExact.stone == 1);

    std::vector<RaidStoneSeat> low{Seat("Selie", 50), AtStone("Bork", 60, 1),
                                   AtStone("Og", 60, 1)};
    RaidStoneSummon const lowStone = ChooseRaidStoneSummon(low, Stones());
    Check("the 48 stone takes him", lowStone.summon && lowStone.stone == 1);

    std::vector<RaidStoneSeat> floor{Seat("Selie", 47), AtStone("Bork", 60, 1),
                                     AtStone("Og", 60, 1)};
    Check("below every stone, he keeps walking",
          !ChooseRaidStoneSummon(floor, Stones()).summon);

    // The clicker is checked too; the portal's second clicker is not.
    std::vector<RaidStoneSeat> clickers{Seat("Selie", 60), AtStone("Og", 52, 0),
                                        AtStone("Bork", 60, 0)};
    RaidStoneSummon const picked = ChooseRaidStoneSummon(clickers, Stones());
    CheckString("a clicker below the stone does not click it", picked.clicker, "Bork");
    CheckString("but may click the portal", picked.helper, "Og");

    std::vector<RaidStoneSeat> lowClickers{Seat("Selie", 60), AtStone("Og", 52, 0),
                                           AtStone("Kaz", 50, 0)};
    Check("no clicker who meets the stone is no summon",
          !ChooseRaidStoneSummon(lowClickers, Stones()).summon);
}

void ARaidMapIsNotArmedUntilClearingIsOrdered()
{
    Check("a dungeon map arms as before", DungeonClearMayArmHere(false, false));
    Check("a raid map with no clearing order does not", !DungeonClearMayArmHere(true, false));
    Check("a raid map with a clearing order does", DungeonClearMayArmHere(true, true));
}

}  // namespace

int main()
{
    TheRepairFloorIsStrictAndConfigurable();
    OnlyMajordomoAndRagnarosPullsWaitForRepair();
    ThePullHoldReadsEverySeat();
    EachSeatInThePullHoldWalksRepairsOrWaits();
    ThePrePullHoldIsWiredIntoTheRun();
    TheMoltenCoreDoorIsTheWorldsOwnNumbers();
    OnlyAnExactRaidJobNamesADoor();
    NoOrderIsIdleAndAnEndedOrderReleases();
    AHeadWithoutAClientHoldsTheRunWhereItIs();
    AnOrderFormsFirstAndKeepsFormingWhileSeatsCanJoin();
    AssembleWaitsForTheRaidThenForNobodyForever();
    TheHeadCrossesLast();
    InsideHoldsAndLetsStragglersThrough();
    ARaidMapIsNotArmedUntilClearingIsOrdered();
    TheDoorNamesTheMountainsTwoStones();
    StragglersAreSummonedOnlyAfterTheWalk();
    AStragglerIsSummonedByASeatAtTheStone();
    TheStonesMinimumIsKeptForBothEnds();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the raid run decisions hold\n");
    return EXIT_SUCCESS;
}
