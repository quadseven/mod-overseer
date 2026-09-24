/*
 * A campaign that fails an attempt recovers rather than stopping, and a
 * staging errand is not ended by a terrain verdict that moves nobody.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 *
 * WHAT IT PINS, AND THE EVENING IT COMES FROM. On 2026-09-23 the operator
 * restarted dungeon campaigns by hand several times, each time after three
 * attempts in a row had failed before entry and the module had stopped the
 * campaign with an ERROR asking for exactly that. The failures were all things
 * the module could answer itself: a staging clock that ran out with the leader
 * thousands of yards from the door, a family split across two copies of the
 * instance, full bags, and a leader whose errand kept being taken off him. The
 * order that followed: never stop a campaign; recover.
 *
 * And the take-backs had one cause, found in the live log for campaign 11: the
 * terrain drive released the leader's travel errand on its "STANDING ON THE
 * GROUND ... NOTHING IS BEING MOVED" give-up, which fires once per overhang in
 * a layered city such as Orgrimmar. Six of seven take-backs in ten minutes
 * came one to three seconds after that line.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckWord(char const* what, RunRecovery got, RunRecovery want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, RunRecoveryWord(got),
                RunRecoveryWord(want));
    ++failures;
}

void CheckUnsigned(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, want %u\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void TodaysFailuresEachGetTheirOwnRecovery()
{
    // Staging timed out with the leader 5945 yards out: the walk never fit in
    // the clock, so the next attempt starts nearer.
    RunFailureFacts far;
    far.outcome = "staging_failed";
    far.reason = "GATHERING held for more than 12 minutes and never opened - Zug (5945y out)";
    far.leaderYardsFromStaging = 5945.f;
    CheckWord("far leader restages nearer", RunRecoveryHeuristic(far),
              RunRecovery::RestageNearer);

    // The family split across two copies of the instance.
    RunFailureFacts split;
    split.outcome = "split_failed";
    CheckWord("split party enters one copy", RunRecoveryHeuristic(split),
              RunRecovery::OneCopy);

    // Bags full: only the town trip clears that, and it runs from IDLE.
    RunFailureFacts bags;
    bags.outcome = "staging_failed";
    bags.bagsFull = true;
    bags.leaderYardsFromStaging = 5000.f;
    CheckWord("bags outrank distance", RunRecoveryHeuristic(bags),
              RunRecovery::TownForBags);
    RunFailureFacts evacuated;
    evacuated.outcome = "evacuated";
    CheckWord("an evacuation goes to town", RunRecoveryHeuristic(evacuated),
              RunRecovery::TownForBags);

    // A member with no client cannot be walked anywhere.
    RunFailureFacts offline;
    offline.outcome = "staging_failed";
    offline.everyoneSteerable = false;
    CheckWord("no client waits for it", RunRecoveryHeuristic(offline),
              RunRecovery::WaitForClient);

    // A straggler, with the leader near the door.
    RunFailureFacts straggler;
    straggler.outcome = "staging_failed";
    straggler.leaderYardsFromStaging = 40.f;
    straggler.farthestMemberYards = 900.f;
    CheckWord("a straggler is regrouped", RunRecoveryHeuristic(straggler),
              RunRecovery::Regroup);

    // A leader held by other claimants (his errand taken back) near the door.
    RunFailureFacts held;
    held.outcome = "staging_failed";
    held.leaderYardsFromStaging = 300.f;
    held.stagingRearms = 7;
    CheckWord("a held leader replans", RunRecoveryHeuristic(held), RunRecovery::Replan);

    RunFailureFacts reset;
    reset.outcome = "reset_failed";
    CheckWord("a refused reset resets again", RunRecoveryHeuristic(reset),
              RunRecovery::ResetInstance);
}

void NoRecoveryIsChosenAThirdTimeRunning()
{
    RunFailureFacts far;
    far.outcome = "staging_failed";
    far.leaderYardsFromStaging = 5945.f;
    far.tried = {RunRecovery::RestageNearer};
    CheckWord("once is allowed again", RunRecoveryHeuristic(far),
              RunRecovery::RestageNearer);
    far.tried = {RunRecovery::RestageNearer, RunRecovery::RestageNearer};
    Check("twice running is not chosen a third time",
          RunRecoveryHeuristic(far) != RunRecovery::RestageNearer, true);
    // And what replaces it is a real recovery, not the bag trip or a wait.
    RunRecovery const next = RunRecoveryHeuristic(far);
    Check("the replacement walks or resets",
          next != RunRecovery::TownForBags && next != RunRecovery::WaitForClient, true);
}

void TheBackoffGrowsIsCappedAndIsNeverZero()
{
    CheckUnsigned("first failure waits the base", RunRecoveryBackoffSeconds(1, 60, 900), 60);
    CheckUnsigned("second doubles", RunRecoveryBackoffSeconds(2, 60, 900), 120);
    CheckUnsigned("third doubles again", RunRecoveryBackoffSeconds(3, 60, 900), 240);
    CheckUnsigned("fifth reaches the cap", RunRecoveryBackoffSeconds(5, 60, 900), 900);
    CheckUnsigned("fiftieth stays at the cap", RunRecoveryBackoffSeconds(50, 60, 900), 900);
    CheckUnsigned("attempt zero is the base", RunRecoveryBackoffSeconds(0, 60, 900), 60);
    CheckUnsigned("a zero base still waits", RunRecoveryBackoffSeconds(1, 0, 0), 1);
    for (unsigned a = 1; a < 40; ++a)
        Check("never shrinks",
              RunRecoveryBackoffSeconds(a + 1, 60, 900) >= RunRecoveryBackoffSeconds(a, 60, 900),
              true);
}

void OnlyOfferedWordsParse()
{
    for (RunRecovery r : {RunRecovery::RestageNearer, RunRecovery::Regroup,
                          RunRecovery::TownForBags, RunRecovery::WaitForClient,
                          RunRecovery::OneCopy, RunRecovery::Replan,
                          RunRecovery::ResetInstance})
    {
        RunRecovery back = RunRecovery::ResetInstance;
        Check(RunRecoveryWord(r), ParseRunRecovery(RunRecoveryWord(r), back), true);
        CheckWord("round trip", back, r);
        Check("offered", RunRecoveryOptions().find(RunRecoveryWord(r)) != std::string::npos,
              true);
    }
    RunRecovery ignored = RunRecovery::Replan;
    Check("an unknown word is refused", ParseRunRecovery("stop_campaign", ignored), false);
    Check("an empty word is refused", ParseRunRecovery("", ignored), false);

    StagingStall stall = StagingStall::KeepRearming;
    Check("run_yields parses", ParseStagingStall("run_yields", stall), true);
    Check("run_yields is itself", stall == StagingStall::RunYields, true);
    Check("a recovery word is not a stall word", ParseStagingStall("replan", stall), false);
}

void ARecoveryWaitEndsOnItsConditionOrItsCeiling()
{
    RecoveryWaitFacts f;
    f.since = 1000;
    f.now = 1100;
    f.ceilingSeconds = 600;
    Check("waiting", RecoveryWaitNext(f) == RecoveryWaitStep::Wait, true);
    f.satisfied = true;
    Check("done when satisfied", RecoveryWaitNext(f) == RecoveryWaitStep::Done, true);
    f.satisfied = false;
    f.now = 1600;
    Check("ceiling ends it", RecoveryWaitNext(f) == RecoveryWaitStep::Ceiling, true);
}

void AStagingStallIsAskedEveryThirdRearm()
{
    Check("not at zero", StagingStallAskAt(0), false);
    Check("not at two", StagingStallAskAt(2), false);
    Check("at three", StagingStallAskAt(3), true);
    Check("not at four", StagingStallAskAt(4), false);
    Check("at six", StagingStallAskAt(6), true);

    StagingStallFacts f;
    f.rearms = 3;
    f.leaderYards = 400.f;
    Check("an in-module ender keeps rearming",
          StagingStallHeuristic(f) == StagingStall::KeepRearming, true);
    f.endedFromOutside = true;
    Check("an outside claimant is yielded to",
          StagingStallHeuristic(f) == StagingStall::RunYields, true);
    f.endedFromOutside = false;
    f.rearms = 6;
    f.leaderYards = 2200.f;
    Check("far out and still stopped recovers now",
          StagingStallHeuristic(f) == StagingStall::RecoverNow, true);
}

void AGiveUpOnTheGroundLeavesTheErrandAlone()
{
    // THE ROOT CAUSE. The on-the-ground give-up moves nothing and must end
    // nothing; before this, every remedy but Nothing released the errand.
    Check("on the ground: the errand stays",
          TerrainRemedyEndsTheErrand(TerrainRemedy::GiveUp, true), false);
    Check("a lift ends it", TerrainRemedyEndsTheErrand(TerrainRemedy::LiftToSurface, true),
          true);
    Check("a lift ends it off the ground too",
          TerrainRemedyEndsTheErrand(TerrainRemedy::LiftToSurface, false), true);
    Check("a give-up under the world ends it",
          TerrainRemedyEndsTheErrand(TerrainRemedy::GiveUp, false), true);
    Check("nothing ends nothing", TerrainRemedyEndsTheErrand(TerrainRemedy::Nothing, false),
          false);
}

void TheTimelineSaysWhatEndedTheErrandAndWhichRecovery()
{
    RunTimelineSnapshot before;
    before.phase = "GATHERING";
    before.campaignId = 11;
    before.runNumber = 1;
    RunTimelineSnapshot after = before;
    after.stagingRearms = 1;
    after.stagingRearmWhy = "the terrain drive";
    std::vector<RunTimelineEvent> const rearm = RunTimelineEvents(before, after);
    Check("one row", rearm.size() == 1, true);
    if (!rearm.empty())
        CheckText("the ender is named", rearm[0].detail,
                  "the leader's staging errand was taken back (1 so far); it was ended "
                  "by the terrain drive");

    RunTimelineSnapshot recovering = before;
    recovering.phase = "RECOVERING";
    recovering.recoveryNote = "attempt 3: restage_nearer in 240s";
    std::vector<RunTimelineEvent> const rec = RunTimelineEvents(before, recovering);
    bool sawRecovery = false;
    bool sawPhase = false;
    for (RunTimelineEvent const& e : rec)
    {
        sawRecovery = sawRecovery || (e.kind == "recovery" &&
                                      e.detail == "attempt 3: restage_nearer in 240s");
        sawPhase = sawPhase || (e.kind == "phase" && e.detail == "GATHERING -> RECOVERING");
    }
    Check("a recovery row", sawRecovery, true);
    Check("a phase row", sawPhase, true);

    RunTimelineSnapshot stalled = before;
    stalled.stallNote = "after 3 take-backs: run_yields (heuristic)";
    std::vector<RunTimelineEvent> const st = RunTimelineEvents(before, stalled);
    Check("a stall row", st.size() == 1 && st[0].kind == "staging_stall", true);
}

void ARunInsideMakesRoomAndLeavesOnlyForALostUpgrade()
{
    // MEASURED 2026-09-24: a Horde Ragefire run credited two bosses and was
    // walked out mid-clear for bag room. Inside, bag pressure makes room.
    DungeonBagPressure const inside =
        DungeonRunBagPressure({15, 0, 6, 10, 6}, 3, true, false);
    Check("the raw pressure inside is still an evacuation",
          inside == DungeonBagPressure::Evacuate, true);
    Check("with no upgrade on the floor the run makes room",
          DungeonRunBagAnswer(inside, false) == DungeonBagPressure::MakeRoom, true);
    Check("an upgrade on the floor for a full member walks it out",
          DungeonRunBagAnswer(inside, true) == DungeonBagPressure::Evacuate, true);
    // The pre-run town-first gate is untouched.
    DungeonBagPressure const outside =
        DungeonRunBagPressure({15, 2, 6, 10, 6}, 3, false, false);
    Check("outside still holds out for the town trip",
          DungeonRunBagAnswer(outside, true) == DungeonBagPressure::HoldOut, true);
    Check("room everywhere is still nothing",
          DungeonRunBagAnswer(DungeonRunBagPressure({15, 12}, 3, true, false), true) ==
              DungeonBagPressure::None,
          true);
}

void AGreyIsDestroyedForRoomDespiteItsPrice()
{
    DestroySpec grey = ParseDestroySpec("destroy guid:42 count:3 allow:grey");
    Check("allow:grey parses", grey.valid && grey.allowGrey, true);
    DestroyFacts facts;
    facts.quality = 0;
    facts.sellPrice = 7;
    facts.stack = 3;
    facts.questHold = QuestItemHold::ActiveQuest;
    CheckText("a grey with a price and a cautious quest reading is destroyed",
              DestroyRefusal(grey, facts), "");
    DestroySpec plain = ParseDestroySpec("destroy guid:42 count:3");
    CheckText("without allow:grey the price still refuses", DestroyRefusal(plain, facts),
              DestroyRefusalText::Quest);
    facts.quality = 1;
    facts.questHold = QuestItemHold::Released;
    CheckText("allow:grey does not reach a white", DestroyRefusal(grey, facts),
              DestroyRefusalText::HasPrice);
    facts.quality = 0;
    facts.equipped = true;
    CheckText("an equipped grey is still refused", DestroyRefusal(grey, facts),
              DestroyRefusalText::Equipped);
}

}  // namespace

int main()
{
    TodaysFailuresEachGetTheirOwnRecovery();
    NoRecoveryIsChosenAThirdTimeRunning();
    TheBackoffGrowsIsCappedAndIsNeverZero();
    OnlyOfferedWordsParse();
    ARecoveryWaitEndsOnItsConditionOrItsCeiling();
    AStagingStallIsAskedEveryThirdRearm();
    AGiveUpOnTheGroundLeavesTheErrandAlone();
    TheTimelineSaysWhatEndedTheErrandAndWhichRecovery();
    ARunInsideMakesRoomAndLeavesOnlyForALostUpgrade();
    AGreyIsDestroyedForRoomDespiteItsPrice();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
