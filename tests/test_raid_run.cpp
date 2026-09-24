/*
 * The raid run's first slice: which door, which job arms it, how one poll
 * moves FORM -> ASSEMBLE -> ENTER -> INSIDE, who is knocked through the door in
 * which order, and that the dungeon brain is never armed on a raid map this
 * slice did not order cleared.
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
#include <string>
#include <vector>

using OverseerDecisions::DungeonClearMayArmHere;
using OverseerDecisions::RAID_ASSEMBLE_WAIT_SECONDS;
using OverseerDecisions::RAID_ENTER_WAIT_SECONDS;
using OverseerDecisions::RAID_FORM_WAIT_SECONDS;
using OverseerDecisions::RaidDoor;
using OverseerDecisions::RaidDoorFor;
using OverseerDecisions::RaidKeywordForJob;
using OverseerDecisions::RaidKnockOrder;
using OverseerDecisions::RaidRunFacts;
using OverseerDecisions::RaidRunPhase;
using OverseerDecisions::RaidRunPhaseName;
using OverseerDecisions::RaidRunStep;
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
    facts.headAssembled = false;
    RaidRunStep const inside = StepRaidRun(RaidRunPhase::Enter, facts);
    CheckPhase("the head inside is INSIDE", inside.phase, RaidRunPhase::Inside);
    Check("stragglers at the door are still knocked", inside.knockMembers);
    Check("nothing walks the head anywhere", !inside.aimDoor && !inside.aimStaging);
    Check("and nothing forms", !inside.form);

    facts.headInside = false;
    RaidRunStep const left = StepRaidRun(RaidRunPhase::Inside, facts);
    CheckPhase("a head who left holds INSIDE rather than walking back",
               left.phase, RaidRunPhase::Inside);
    Check("with nothing moving", !left.aimDoor && !left.aimStaging && !left.knockMembers);
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
    TheMoltenCoreDoorIsTheWorldsOwnNumbers();
    OnlyAnExactRaidJobNamesADoor();
    NoOrderIsIdleAndAnEndedOrderReleases();
    AHeadWithoutAClientHoldsTheRunWhereItIs();
    AnOrderFormsFirstAndKeepsFormingWhileSeatsCanJoin();
    AssembleWaitsForTheRaidThenForNobodyForever();
    TheHeadCrossesLast();
    InsideHoldsAndLetsStragglersThrough();
    ARaidMapIsNotArmedUntilClearingIsOrdered();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the raid run decisions hold\n");
    return EXIT_SUCCESS;
}
