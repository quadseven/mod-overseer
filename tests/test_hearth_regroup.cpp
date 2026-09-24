/*
 * A family spread across zones with an armed dungeon campaign hearths to the
 * inn most of it shares, meets there, and walks to the door together; the
 * leader is not fetched across the continent or sent to a distant trainer in
 * the meantime.
 *
 * This compiles against the pure decision file and nothing from AzerothCore,
 * and reads src/mod_overseer.cpp to pin the wiring (run from the repo root).
 *
 * WHAT WAS MEASURED ON THE DEV REALM, 2026-09-24 08:00-08:09. The Horde
 * family's Ragefire Chasm campaign failed its fifth attempt in a row before
 * entry: "GATHERING was closed early: the leader's staging errand was taken
 * back 7 times". The leader stood in Stonetalon 5,549 yards from the staging
 * point, one member elsewhere in Stonetalon, and three in Ashenvale 4,289
 * yards from him, where they had died more than ten times that day. Four of
 * the five were bound at one inn in the Valley of Trials; the fifth in
 * Mulgore. The recovery chosen was restage_nearer, tried twice already that
 * streak. Within ninety seconds the leader was sent to a class trainer 2,375
 * yards away for a talent reset, held still for a member 1,044 yards behind,
 * and flown toward a member 3,964 yards away. The operator's kind='hearth'
 * rows had brought every member home at once, twice, the same morning.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want = true)
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

HomeBind Bind(uint32_t map, float x, float y)
{
    HomeBind b;
    b.known = true;
    b.mapId = map;
    b.x = x;
    b.y = y;
    return b;
}

HearthRegroupMember Member(char const* name, HomeBind bind, bool leader = false)
{
    HearthRegroupMember m;
    m.name = name;
    m.leader = leader;
    m.inWorld = true;
    m.alive = true;
    m.carriesStone = true;
    m.bind = bind;
    return m;
}

// The binds read from character_homebind on the dev realm (map 1).
HomeBind const VALLEY_OF_TRIALS = Bind(1, -619.f, -4252.f);
HomeBind const MULGORE = Bind(1, -2918.f, -258.f);

std::vector<HearthRegroupMember> MeasuredHordeFamily()
{
    return {Member("Zug", VALLEY_OF_TRIALS, true), Member("Oz", VALLEY_OF_TRIALS),
            Member("Uzza", VALLEY_OF_TRIALS), Member("Zork", MULGORE),
            Member("Zrog", VALLEY_OF_TRIALS)};
}

bool Has(std::vector<std::string> const& names, char const* name)
{
    for (std::string const& n : names)
        if (n == name)
            return true;
    return false;
}

RunFailureFacts MeasuredFailure()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.reason = "GATHERING was closed early: the leader's staging errand was taken back 7 "
               "times (last ended by the travel drive (no progress in five stuck attempts)), "
               "and the staging stall answer was to recover now";
    f.leaderYardsFromStaging = 5549.f;
    f.farthestMemberYards = 4289.f;
    f.stagingRearms = 7;
    f.tried = {RunRecovery::Regroup,       RunRecovery::Replan, RunRecovery::RestageNearer,
               RunRecovery::Replan,        RunRecovery::RestageNearer, RunRecovery::Regroup};
    return f;
}

void TheMeasuredFamilyHasAnInnToMeetAt()
{
    HearthRegroupPlan const plan = PlanHearthRegroup(MeasuredHordeFamily(), 1);
    Check("four of five share the Valley of Trials: ready", plan.ready);
    Check("four bound there", plan.boundThere == 4);
    Check("the inn is the leader's own bind", plan.leaderBoundThere);
    Check("at the Valley of Trials", plan.inn.x == -619.f && plan.inn.y == -4252.f);
    Check("four hearth", plan.hearth.size() == 4);
    Check("the leader hearths", Has(plan.hearth, "Zug"));
    Check("the member bound in Mulgore walks or flies", plan.travel.size() == 1 &&
                                                            Has(plan.travel, "Zork"));
    Check("nothing to say against it", plan.whyNot.empty());
}

void AStoneOnCooldownWalksAndTooFewStonesIsNoPlan()
{
    std::vector<HearthRegroupMember> family = MeasuredHordeFamily();
    family[1].onCooldown = true;  // Oz
    HearthRegroupPlan plan = PlanHearthRegroup(family, 1);
    Check("three stones of five still regroup", plan.ready);
    Check("Oz walks", Has(plan.travel, "Oz") && !Has(plan.hearth, "Oz"));

    family[2].onCooldown = true;  // Uzza
    plan = PlanHearthRegroup(family, 1);
    Check("two stones of five is not a family regroup", plan.ready, false);
    Check("and it says why", !plan.whyNot.empty());

    family = MeasuredHordeFamily();
    family[4].alive = false;       // Zrog, dead
    family[2].carriesStone = false;  // Uzza, no stone
    plan = PlanHearthRegroup(family, 1);
    Check("dead or stoneless members do not count as stones", plan.ready, false);
}

void NoMajorityInnNoWrongContinentNoFamilyOfOne()
{
    std::vector<HearthRegroupMember> split = {
        Member("Zug", VALLEY_OF_TRIALS, true), Member("Oz", VALLEY_OF_TRIALS),
        Member("Uzza", MULGORE), Member("Zork", MULGORE), Member("Zrog", Bind(1, 1000.f, 1000.f))};
    HearthRegroupPlan plan = PlanHearthRegroup(split, 1);
    Check("two of five is not most of the family", plan.ready, false);

    plan = PlanHearthRegroup(MeasuredHordeFamily(), 0);
    Check("an inn on another continent than the door is no inn", plan.ready, false);

    plan = PlanHearthRegroup({Member("Zug", VALLEY_OF_TRIALS, true)}, 1);
    Check("a family of one has nobody to regroup with", plan.ready, false);

    std::vector<HearthRegroupMember> unread = MeasuredHordeFamily();
    for (HearthRegroupMember& m : unread)
        m.bind.known = false;
    Check("unread binds are no inn", PlanHearthRegroup(unread, 1).ready, false);
}

void ATieGoesToTheLeadersInn()
{
    std::vector<HearthRegroupMember> pair = {Member("Oz", MULGORE),
                                             Member("Zug", VALLEY_OF_TRIALS, true)};
    // One and one: neither is most of two, but the inn chosen is still his.
    HearthRegroupPlan const plan = PlanHearthRegroup(pair, 1);
    Check("the tie names the leader's inn", plan.leaderBoundThere);
    Check("one of two is not a majority", plan.ready, false);
}

void TheMeasuredFailureRegroupsByHearthstone()
{
    RunFailureFacts f = MeasuredFailure();
    RunRecovery const without = RunRecoveryHeuristic(f);
    Check("without an inn, not a hearth regroup", without != RunRecovery::HearthRegroup);
    Check("without an inn, not offered",
          RunRecoveryOptions(f).find("hearth_regroup") == std::string::npos);
    Check("without an inn, not applicable",
          RunRecoveryApplicable(RunRecovery::HearthRegroup, f), false);

    f.hearthRegroupReady = true;
    CheckWord("the measured failure hearths to the shared inn", RunRecoveryHeuristic(f),
              RunRecovery::HearthRegroup);
    Check("and it is offered to the bridge",
          RunRecoveryOptions(f).find("hearth_regroup") != std::string::npos);
    Check("applicable", RunRecoveryApplicable(RunRecovery::HearthRegroup, f));
    Check("its why names the inn",
          RunRecoveryHeuristicWhy(f, RunRecovery::HearthRegroup).find("inn") !=
              std::string::npos);

    // A family together is not spread, however far the door.
    RunFailureFacts together = f;
    together.farthestMemberYards = 40.f;
    CheckWord("a family together restages", RunRecoveryHeuristic(together),
              RunRecovery::RestageNearer);

    // What cannot be walked around still comes first.
    RunFailureFacts bags = f;
    bags.bagsFull = true;
    CheckWord("bags outrank the inn", RunRecoveryHeuristic(bags), RunRecovery::TownForBags);
}

void NotAThirdTimeAndNeverWithoutAnInn()
{
    RunFailureFacts f = MeasuredFailure();
    f.hearthRegroupReady = true;
    f.tried = {RunRecovery::HearthRegroup, RunRecovery::HearthRegroup};
    Check("twice running is not chosen a third time",
          RunRecoveryHeuristic(f) != RunRecovery::HearthRegroup);

    // The ladder's least-tried fallback never lands on a hearth regroup the
    // family cannot carry out, whatever else has been tried.
    RunFailureFacts g = MeasuredFailure();
    g.tried = {RunRecovery::RestageNearer, RunRecovery::RestageNearer, RunRecovery::Regroup,
               RunRecovery::Replan,        RunRecovery::OneCopy,       RunRecovery::ResetInstance};
    Check("the fallback skips an unavailable hearth regroup",
          RunRecoveryHeuristic(g) != RunRecovery::HearthRegroup);
}

void TheWordRoundTrips()
{
    RunRecovery back = RunRecovery::Replan;
    Check("hearth_regroup parses", ParseRunRecovery("hearth_regroup", back));
    CheckWord("to itself", back, RunRecovery::HearthRegroup);
    Check("every word is still in the full list",
          RunRecoveryOptions().find("hearth_regroup") != std::string::npos);
}

void EachMemberStepsTowardTheInn()
{
    Check("at the inn is done",
          HearthRegroupStepFor(true, 30.f, true, false, false) == HearthRegroupStep::AtInn);
    Check("a walker at the inn is done too",
          HearthRegroupStepFor(false, 30.f, true, true, false) == HearthRegroupStep::AtInn);
    Check("a member of the hearth set hearths",
          HearthRegroupStepFor(true, 4289.f, true, false, false) == HearthRegroupStep::Hearth);
    Check("but not before the leader is coming",
          HearthRegroupStepFor(true, 4289.f, false, false, false) ==
              HearthRegroupStep::WaitForLeader);
    Check("a stone that cannot be used walks",
          HearthRegroupStepFor(true, 4289.f, true, true, false) == HearthRegroupStep::Travel);
    Check("a member bound elsewhere walks or flies",
          HearthRegroupStepFor(false, 3900.f, true, true, false) == HearthRegroupStep::Travel);
    Check("another map is not at the inn",
          HearthRegroupStepFor(false, -1.f, true, true, false) == HearthRegroupStep::Travel);
    Check("every step has a word",
          std::string(HearthRegroupStepWord(HearthRegroupStep::WaitForLeader)) != "unknown");
}

// THE MEASURED FLAP, 2026-09-24 11:10-11:20, campaign 13 (Alliance family,
// recovery hearth_regroup applied 11:09:42). Every member hearthed and landed
// 0 to 3 yards from the inn. Released to `follow`, each was walked toward a
// leader 7,700 yards away, crossed out of the 100-yard inn radius, was escorted
// back, read "at the inn" at 93, 64, 70, 76, 85 or 89 yards, and crossed out
// again at 101 to 131 yards. "hearth regroup done" never fired.
void AnArrivalIsNotUndoneByTheNextFewYards()
{
    // Grog: in at 93, then out at 131. Bork: in at 70, out at 103 and 104.
    // Ugga: in at 76 and 89, out at 110 and 121. Og: in at 85, out at 122.
    float const out[] = {131.f, 101.f, 103.f, 104.f, 110.f, 121.f, 122.f, 128.f};
    for (float yards : out)
    {
        Check("a member that has arrived is still at the inn",
              HearthRegroupStepFor(true, yards, true, true, true) == HearthRegroupStep::AtInn);
        Check("a walker bound elsewhere that has arrived is still at the inn",
              HearthRegroupStepFor(false, yards, true, true, true) ==
                  HearthRegroupStep::AtInn);
        Check("one that has not arrived is not there yet",
              HearthRegroupStepFor(true, yards, true, true, false) ==
                  HearthRegroupStep::Travel);
    }
    Check("arriving still takes the inn radius",
          HearthRegroupStepFor(false, 93.f, true, true, false) == HearthRegroupStep::AtInn);
    Check("past the leave radius an arrived member walks back",
          HearthRegroupStepFor(true, HEARTH_REGROUP_LEAVE_YARDS + 1.f, true, true, true) ==
              HearthRegroupStep::Travel);
    Check("and one whose stone is ready hearths back",
          HearthRegroupStepFor(true, 7700.f, true, false, true) == HearthRegroupStep::Hearth);
    Check("another map is never at the inn, arrived or not",
          HearthRegroupStepFor(false, -1.f, true, true, true) == HearthRegroupStep::Travel);
    Check("leaving takes more than arriving",
          HEARTH_REGROUP_LEAVE_YARDS > HEARTH_REGROUP_INN_YARDS);
}

void TheLeaderIsNotFetchedWhileTheFamilyMeetsAtItsInn()
{
    Check("a hearth regroup refuses the fetch",
          RunLetsTheLeaderFetch(RecoveringFetchPhase(true)) == FetchRunAnswer::Refuse);
    Check("any other recovery still lends the leader",
          RunLetsTheLeaderFetch(RecoveringFetchPhase(false)) == FetchRunAnswer::Fetch);
}

void ATalentResetWaitsForAnArmedCampaignUnlessTheTrainerIsInTown()
{
    HeadTravelFacts between;
    between.campaignBetweenAttempts = true;
    Check("unmeasured trainer: the reset waits",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between), false);
    between.trainerYards = 2375.f;  // the measured walk
    Check("2,375 yards away: the reset waits",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between), false);
    Check("and says why",
          std::string(HeadErrandWaitReason(HeadErrand::TrainerTrip, between)).find(
              "between attempts") != std::string::npos);
    between.trainerYards = 60.f;
    Check("a trainer in the town the head stands in: the reset goes",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between));

    HeadTravelFacts idle;
    Check("no campaign between attempts: the reset goes as before",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, idle));
    Check("the regroup wait is untouched between attempts",
          HeadErrandMayTravel(HeadErrand::Other, between));

    HeadTravelFacts hearth;
    hearth.campaignBetweenAttempts = true;
    hearth.hearthRegroup = true;
    Check("a hearth regroup stands the regroup wait down",
          HeadErrandMayTravel(HeadErrand::Other, hearth), false);

    RespecFacts r;
    r.specTab = 2;
    r.level = 28;
    r.pointsByTree[0] = 18;
    r.money = 10009;
    r.cost = 10000;
    r.available = true;
    r.columnFree = true;
    between.trainerYards = 2375.f;
    r.runOwnsTravel = !HeadErrandMayTravel(HeadErrand::TrainerTrip, between);
    Check("the measured head is not sent", JudgeRespec(r) == RespecStep::RunOwnsTravel);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("the failure facts carry the plan",
        "facts.hearthRegroupReady = inn.ready;");
    has("the request offers only what can be carried out",
        "OverseerDecisions::RunRecoveryOptions(facts),");
    has("a bridge answer is checked against the facts",
        "OverseerDecisions::RunRecoveryApplicable(parsed, coord.recoveryFacts)");
    has("the plan is read again when applied",
        "coord.hearthPlan = PlanFamilyHearthRegroup(leaderName, members, portal);");
    has("the cast is DoHearth's",
        "DoHearth(bot, \"use\", status, evidence, _pendingHearths, 0);");
    has("walkers go under the run's own escort",
        "EscortToward(name, innAim.str(), \"HEARTH REGROUP\", EscortPurpose::Assemble);");
    has("then the family walks to the door",
        "coord.recovery = OverseerDecisions::RunRecovery::RestageNearer;");
    has("the fetch reads the recovery",
        "OverseerDecisions::RecoveringFetchPhase(RecoveryBringsTheFamily(coord->second))");
    has("the head's facts know a campaign between attempts",
        "facts.campaignBetweenAttempts = true;");
    has("and a hearth regroup",
        "facts.hearthRegroup = _hearthRegroupMembers.count(name) > 0;");
    has("the talent reset measures its trainer between attempts",
        "head.trainerYards = bot->GetExactDist2d(trainerPos.GetPositionX(),");
    has("arrival is remembered per member",
        "coord.hearthArrived.insert(name);");
    has("and read back into the step",
        "hearths, yards, name == leaderName || leaderComing, impossible, arrivedBefore);");
    has("an arrived member is held at the inn",
        "HoldAtStagingPoint(bot, name, \"the hearth regroup holds it at the \"");
    has("and keeps the run's escort, so no catch-up walk starts",
        "EscortToward(name, innAim.str(), \"HEARTH REGROUP\", EscortPurpose::Assemble);\n"
        "                    break;\n"
        "\n"
        "                case HearthRegroupStep::WaitForLeader:");
    has("the barrier's sweep keeps the hold while the regroup is applied",
        "HearthRegroupHolds(coord->second)))");
    has("and the plan's reset forgets who arrived",
        "coord.hearthArrived.clear();");
    // Nothing here moves a character by any road but the stone and the walk.
    std::size_t const begin = source.find("    bool DriveHearthRegroup(");
    std::size_t const end = source.find("    void DriveRecovering(", begin);
    Check("DriveHearthRegroup exists", begin != std::string::npos && end != std::string::npos);
    if (begin != std::string::npos && end != std::string::npos)
        Check("and never teleports",
              source.substr(begin, end - begin).find("TeleportTo") == std::string::npos);
}

}  // namespace

int main()
{
    TheMeasuredFamilyHasAnInnToMeetAt();
    AStoneOnCooldownWalksAndTooFewStonesIsNoPlan();
    NoMajorityInnNoWrongContinentNoFamilyOfOne();
    ATieGoesToTheLeadersInn();
    TheMeasuredFailureRegroupsByHearthstone();
    NotAThirdTimeAndNeverWithoutAnInn();
    TheWordRoundTrips();
    EachMemberStepsTowardTheInn();
    AnArrivalIsNotUndoneByTheNextFewYards();
    TheLeaderIsNotFetchedWhileTheFamilyMeetsAtItsInn();
    ATalentResetWaitsForAnArmedCampaignUnlessTheTrainerIsInTown();
    TheAdapterIsWired();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
