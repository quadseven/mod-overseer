/*
 * The fallback ladder: once walking has failed Overseer.Recovery.
 * SummonAfterFailures times in a row, run recovery summons the stragglers at
 * the door's meeting stone, and once that has had its turn it puts the family
 * inside through the dungeon finder (behind its own switch).
 *
 * This compiles against the pure decision file and nothing from AzerothCore,
 * and reads src/mod_overseer.cpp to pin the wiring (run from the repo root).
 *
 * WHAT WAS MEASURED ON THE DEV REALM, 2026-09-24. The Horde family's Ragefire
 * Chasm campaign closed 21 of its 25 attempts 'staging_failed' and the
 * Alliance family's Zul'Farrak campaign 12 of 26. The recovery rows of that
 * morning show the same walks and waits chosen again and again: the Horde
 * campaign 12 applied regroup, replan, restage_nearer, replan, restage_nearer,
 * regroup, restage_nearer, regroup, regroup and one_copy across attempts 1 to
 * 7, and the Alliance campaign 13 applied restage_nearer, hearth_regroup and
 * regroup across attempts 1 to 3, the last with 'Og (3361y out and 3y above
 * it)' still not at the door. Both doors have a meeting stone within the
 * search: Zul'Farrak's 58 yards from its trigger, Ragefire Chasm's 25.
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

void CheckStr(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

bool Offered(RunFailureFacts const& f, char const* word)
{
    std::string const options = "," + RunRecoveryOptions(f) + ",";
    return options.find("," + std::string(word) + ",") != std::string::npos;
}

// The Alliance campaign 13's fourth failure: BARRIER held for twelve minutes
// with Og 3,361 yards out, after restage_nearer, hearth_regroup and regroup.
RunFailureFacts AllianceFourthFailure()
{
    RunFailureFacts f;
    f.outcome = "staging_failed";
    f.reason = "BARRIER held for more than 12 minutes and never opened - Bork (124y out), "
               "Og (3361y out and 3y above it)";
    f.leaderYardsFromStaging = 20.f;
    f.farthestMemberYards = 3361.f;
    f.tried = {RunRecovery::RestageNearer, RunRecovery::HearthRegroup, RunRecovery::Regroup};
    f.streak = 4;
    f.summonAfter = RUN_RECOVERY_SUMMON_AFTER_DEFAULT;
    f.summonReady = true;
    f.summonerReady = true;
    return f;
}

void TheSummonRungOpensAtTheStreak()
{
    RunFailureFacts f = AllianceFourthFailure();
    Check("the default opens at three", RUN_RECOVERY_SUMMON_AFTER_DEFAULT == 3);
    Check("four in a row with a stone: summon is open", RunRecoverySummonOpen(f));
    Check("and offered to Jev", Offered(f, "summon"));
    CheckWord("and the heuristic escalates to it before the regroup the facts point at",
              RunRecoveryHeuristic(f), RunRecovery::Summon);

    f.streak = 2;
    Check("two in a row: shut", RunRecoverySummonOpen(f), false);
    Check("not offered", Offered(f, "summon"), false);
    CheckWord("the facts decide as before", RunRecoveryHeuristic(f), RunRecovery::Regroup);

    f.streak = 3;
    Check("exactly at the streak: open", RunRecoverySummonOpen(f));

    f = AllianceFourthFailure();
    f.summonReady = false;
    Check("no stone at the door: shut", RunRecoverySummonOpen(f), false);
    Check("and never offered", Offered(f, "summon"), false);

    f = AllianceFourthFailure();
    f.summonAfter = 0;
    Check("SummonAfterFailures = 0 shuts it", RunRecoverySummonOpen(f), false);

    f = AllianceFourthFailure();
    f.summonerReady = false;
    Check("stone without a qualifying summoner is not offered", Offered(f, "summon"), false);
    CheckWord("without a summoner the chooser falls to regroup", RunRecoveryHeuristic(f),
              RunRecovery::Regroup);
    Check("and applicability agrees", RunRecoveryApplicable(RunRecovery::Summon, f), false);
}

void WhatCannotBeSummonedAroundStillComesFirst()
{
    RunFailureFacts f = AllianceFourthFailure();
    f.bagsFull = true;
    CheckWord("full bags keep the door shut whatever else is done",
              RunRecoveryHeuristic(f), RunRecovery::TownForBags);
    f = AllianceFourthFailure();
    f.everyoneSteerable = false;
    CheckWord("a member with no client cannot click or be summoned",
              RunRecoveryHeuristic(f), RunRecovery::WaitForClient);
}

void ASummonTriedTwiceRunningHandsBackToTheLadder()
{
    RunFailureFacts f = AllianceFourthFailure();
    f.streak = 6;
    f.tried = {RunRecovery::RestageNearer, RunRecovery::HearthRegroup, RunRecovery::Regroup,
               RunRecovery::Summon, RunRecovery::Summon};
    Check("summon is still open", RunRecoverySummonOpen(f));
    Check("the heuristic does not choose it a third time running",
          RunRecoveryHeuristic(f) != RunRecovery::Summon);
    Check("but Jev may still be offered it", Offered(f, "summon"));
}

void TheFinderComesOnlyAfterTheSummon()
{
    RunFailureFacts f = AllianceFourthFailure();
    f.dungeonFinderReady = true;
    Check("the finder waits for the summon rung", RunRecoveryFinderOpen(f), false);
    Check("not offered yet", Offered(f, "dungeon_finder"), false);
    CheckWord("summon first", RunRecoveryHeuristic(f), RunRecovery::Summon);

    // Attempt 4 was answered by a summon, and attempt 5 failed anyway.
    f.streak = 5;
    f.tried.push_back(RunRecovery::Summon);
    Check("one summon this streak", SummonsTriedThisStreak(f) == 1);
    Check("then the finder opens", RunRecoveryFinderOpen(f));
    Check("and is offered", Offered(f, "dungeon_finder"));
    CheckWord("and the heuristic takes the last resort", RunRecoveryHeuristic(f),
              RunRecovery::DungeonFinder);

    f.dungeonFinderReady = false;
    Check("switched off or unable to queue: never", RunRecoveryFinderOpen(f), false);
    Check("never offered", Offered(f, "dungeon_finder"), false);
    CheckWord("the summon comes round again", RunRecoveryHeuristic(f), RunRecovery::Summon);
}

void ASummonFromAnEarlierStreakDoesNotCount()
{
    // A summon two streaks ago, then a run that got inside, then four
    // failures answered by walks: the summon rung has not had its turn in
    // THIS streak.
    RunFailureFacts f = AllianceFourthFailure();
    f.dungeonFinderReady = true;
    f.tried = {RunRecovery::Summon, RunRecovery::RestageNearer, RunRecovery::HearthRegroup,
               RunRecovery::Regroup};
    f.streak = 4;
    Check("only the newest streak-1 rows are this streak", SummonsTriedThisStreak(f) == 0);
    Check("so the finder waits", RunRecoveryFinderOpen(f), false);
}

void ADoorWithNoStoneGoesStraightToTheFinder()
{
    RunFailureFacts f = AllianceFourthFailure();
    f.summonReady = false;
    f.dungeonFinderReady = true;
    Check("no summon rung to wait for", RunRecoveryFinderOpen(f));
    CheckWord("the finder", RunRecoveryHeuristic(f), RunRecovery::DungeonFinder);
    f.streak = 2;
    Check("but never before the streak", RunRecoveryFinderOpen(f), false);
}

void TheWordsRoundTrip()
{
    RunRecovery back{};
    Check("summon parses", ParseRunRecovery("summon", back));
    CheckWord("to Summon", back, RunRecovery::Summon);
    Check("dungeon_finder parses", ParseRunRecovery("dungeon_finder", back));
    CheckWord("to DungeonFinder", back, RunRecovery::DungeonFinder);
    Check("both are in the full list",
          RunRecoveryOptions().find("summon") != std::string::npos &&
              RunRecoveryOptions().find("dungeon_finder") != std::string::npos);
    Check("the request row's options column (200) still holds every word",
          RunRecoveryOptions().size() < 200);
    RunFailureFacts f = AllianceFourthFailure();
    f.streak = 5;
    f.tried.push_back(RunRecovery::Summon);
    f.dungeonFinderReady = true;
    Check("the why names the streak",
          RunRecoveryHeuristicWhy(f, RunRecovery::DungeonFinder).find("5 attempts") !=
              std::string::npos);
}

SummonRungMember Member(char const* name, bool atStone, bool leader = false)
{
    SummonRungMember m;
    m.name = name;
    m.leader = leader;
    m.inWorld = true;
    m.alive = true;
    m.atStone = atStone;
    return m;
}

void TheRungWalksWaitsAndSummons()
{
    // The leader is 20 yards from the staging point and not at the stone.
    std::vector<SummonRungMember> family = {Member("Grug", false, true), Member("Grog", false),
                                            Member("Ugga", false), Member("Bork", false),
                                            Member("Og", false)};
    SummonRungPlan plan = PlanSummonRung(family);
    Check("a leader away from the stone walks to it",
          plan.step == SummonRungStep::WalkToStone);

    family[0].atStone = true;
    plan = PlanSummonRung(family);
    Check("alone at the stone, he waits for a second clicker",
          plan.step == SummonRungStep::WaitForClickers);

    family[1].atStone = true;
    family[1].inCombat = true;
    plan = PlanSummonRung(family);
    Check("a member in a fight cannot click", plan.step == SummonRungStep::WaitForClickers);

    family[1].inCombat = false;
    family[2].atStone = true;
    family[3].atStone = true;
    plan = PlanSummonRung(family);
    Check("four at the stone and Og 3,361 yards out: summon",
          plan.step == SummonRungStep::Summon);
    CheckStr("the leader clicks", plan.summoner, "Grug");
    CheckStr("the next member helps", plan.helper, "Grog");
    CheckStr("Og is summoned", plan.target, "Og");

    family[4].tries = SUMMON_RUNG_TRIES_PER_MEMBER;
    plan = PlanSummonRung(family);
    Check("tried out: nobody left", plan.step == SummonRungStep::NobodyLeft);
    Check("and it says why", plan.why.find("Og has had its tries") != std::string::npos);

    family[4].tries = 0;
    family[4].alive = false;
    plan = PlanSummonRung(family);
    Check("a dead straggler is not summoned", plan.step == SummonRungStep::NobodyLeft);

    family[4].alive = true;
    family[4].summonable = false;
    plan = PlanSummonRung(family);
    Check("nor one the verb cannot move", plan.step == SummonRungStep::NobodyLeft);

    family[4].summonable = true;
    family[4].atStone = true;
    plan = PlanSummonRung(family);
    Check("everybody at the stone: done", plan.step == SummonRungStep::Done);
}

void TheRowIsFoundAgainByItsSource()
{
    std::string const first = SummonRungRowSource(13, 4, "Og", 1);
    std::string const second = SummonRungRowSource(13, 4, "Og", 2);
    CheckStr("the source names campaign, attempt, member and try", first,
             "run_recovery summon c13 a4 Og t1");
    Check("a second try is never the first row", first != second);
    Check("inside the column",
          SummonRungRowSource(4294967295u, 4294967295u, "Abcdefghijkl", 4294967295u).size() <=
              64);
    Check("pending waits", SummonRowFinished("pending"), false);
    Check("claimed waits", SummonRowFinished("claimed"), false);
    Check("verifying waits", SummonRowFinished("verifying"), false);
    Check("applied is a verdict", SummonRowFinished("applied"));
    Check("unchanged is a verdict", SummonRowFinished("unchanged"));
    Check("error is a verdict", SummonRowFinished("error"));
}

void TheFamilyOffersTheRolesItsClassesCanFill()
{
    // The Alliance family: Grug warrior (1), Grog paladin (2), Bork rogue (4),
    // Og mage (8), Ugga priest (5).
    Check("a warrior leads, tanks and hits",
          FinderRoleMask(1, true) == (FINDER_ROLE_LEADER | FINDER_ROLE_TANK | FINDER_ROLE_DAMAGE));
    Check("a paladin can take every role",
          FinderRoleMask(2, false) == (FINDER_ROLE_TANK | FINDER_ROLE_HEALER | FINDER_ROLE_DAMAGE));
    Check("a rogue hits", FinderRoleMask(4, false) == FINDER_ROLE_DAMAGE);
    Check("a mage hits", FinderRoleMask(8, false) == FINDER_ROLE_DAMAGE);
    Check("a priest heals or hits",
          FinderRoleMask(5, false) == (FINDER_ROLE_HEALER | FINDER_ROLE_DAMAGE));
}

FinderMember FM(char const* name, unsigned classId, bool leader = false)
{
    FinderMember m;
    m.name = name;
    m.leader = leader;
    m.inWorld = true;
    m.alive = true;
    m.inHeadsGroup = true;
    m.classId = classId;
    return m;
}

FinderFacts AllianceAtZulFarrak()
{
    FinderFacts f;
    f.enabled = true;
    f.finderOn = true;
    f.dungeonId = 24;  // Zul'Farrak in LFGDungeons.dbc
    f.dbcMinLevel = 41;
    f.dbcMaxLevel = 51;
    f.groupExists = true;
    f.groupSize = 5;
    f.headLeads = true;
    f.family = {FM("Grug", 1, true), FM("Grog", 2), FM("Bork", 4), FM("Og", 8), FM("Ugga", 5)};
    return f;
}

void TheFinderReadsTheGroupAndTheCoresLocks()
{
    FinderFacts f = AllianceAtZulFarrak();
    Check("five in one party under the head: ready", ReadFinderReadiness(f, true).ready);

    f.enabled = false;
    Check("the switch is honoured", ReadFinderReadiness(f, false).ready, false);

    // MEASURED: the level-60 family is above Zul'Farrak's 41-51 range, and the
    // realm had DungeonAccessRequirements.LFGLevelDBCOverride = 0.
    f = AllianceAtZulFarrak();
    for (FinderMember& m : f.family)
        m.lock = 3;
    FinderReadiness const locked = ReadFinderReadiness(f, false);
    Check("locked too high: not ready", locked.ready, false);
    Check("and the reason names the range and the key",
          locked.whyNot.find("too high level") != std::string::npos &&
              locked.whyNot.find("41-51") != std::string::npos &&
              locked.whyNot.find("LFGLevelDBCOverride") != std::string::npos);

    f = AllianceAtZulFarrak();
    f.groupIsRaid = true;
    Check("a raid is not queued as a party", ReadFinderReadiness(f, false).ready, false);
    f = AllianceAtZulFarrak();
    f.groupIsFinders = true;
    Check("an existing finder group is not queued again", ReadFinderReadiness(f, false).ready,
          false);
    f = AllianceAtZulFarrak();
    f.groupSize = 4;
    Check("four in the group is not the family", ReadFinderReadiness(f, false).ready, false);
    f = AllianceAtZulFarrak();
    f.family.pop_back();
    Check("a family of four is not queued", ReadFinderReadiness(f, false).ready, false);
    f = AllianceAtZulFarrak();
    f.dungeonId = 0;
    Check("a dungeon with no finder entry", ReadFinderReadiness(f, false).ready, false);

    f = AllianceAtZulFarrak();
    f.family[3].inCombat = true;
    Check("a fight is a passing state when a failure is recorded",
          ReadFinderReadiness(f, false).ready);
    Check("and a wall when the rung is about to join", ReadFinderReadiness(f, true).ready,
          false);
}

void TheFinderPicksTheDoorsOwnWing()
{
    std::string why;
    // Ragefire Chasm: one row, whatever its entrance.
    Check("one candidate is the dungeon",
          ChooseFinderDungeon({{4, false, 0.f, 0.f}}, 3.f, -11.f, why) == 4);
    Check("none is no dungeon", ChooseFinderDungeon({}, 0.f, 0.f, why) == 0);
    // Maraudon, read from lfg_dungeon_template: Orange 26 at (1019.69,
    // -458.31), Purple 272 at (752.91, -616.53), Pristine 273 at (495.702,
    // 17.3372). A door landing beside the purple start is the purple wing.
    std::vector<FinderDungeonCandidate> const maraudon = {
        {26, true, 1019.69f, -458.31f}, {272, true, 752.91f, -616.53f},
        {273, true, 495.702f, 17.3372f}};
    Check("a door landing at the purple start is Purple Crystals",
          ChooseFinderDungeon(maraudon, 750.f, -620.f, why) == 272);
    Check("a door landing nowhere near any start is refused",
          ChooseFinderDungeon(maraudon, 0.f, 0.f, why) == 0);
    Check("and says the wing cannot be told", why.find("wrong one") != std::string::npos);
    // Scarlet Monastery: only the Graveyard has a row there; the Library,
    // Armory and Cathedral land on the map's shared entrance.
    std::vector<FinderDungeonCandidate> const scarlet = {
        {18, true, 1688.99f, 1053.48f}, {165, false, 0.f, 0.f}, {163, false, 0.f, 0.f}};
    Check("a Library door is not sent to the Graveyard",
          ChooseFinderDungeon(scarlet, 255.f, -209.f, why) == 0);
}

void TheFamilyMustMakeAFinderGroup()
{
    std::vector<std::uint8_t> alliance = {FinderRoleMask(1, true), FinderRoleMask(2, false),
                                          FinderRoleMask(4, false), FinderRoleMask(8, false),
                                          FinderRoleMask(5, false)};
    Check("warrior, paladin, rogue, mage, priest fit", FinderRolesFit(alliance));
    std::vector<std::uint8_t> horde = {FinderRoleMask(1, true), FinderRoleMask(8, false),
                                       FinderRoleMask(5, false), FinderRoleMask(11, false),
                                       FinderRoleMask(7, false)};
    Check("warrior, mage, priest, druid, shaman fit", FinderRolesFit(horde));
    std::vector<std::uint8_t> noHealer = {FinderRoleMask(1, true), FinderRoleMask(4, false),
                                          FinderRoleMask(4, false), FinderRoleMask(8, false),
                                          FinderRoleMask(9, false)};
    Check("no healer class: no fit", FinderRolesFit(noHealer), false);
    FinderFacts f = AllianceAtZulFarrak();
    f.family = {FM("Grug", 1, true), FM("A", 4), FM("B", 4), FM("C", 8), FM("D", 9)};
    Check("and readiness says so before the rung is spent",
          ReadFinderReadiness(f, false).whyNot.find("one healer") != std::string::npos);
}

void TheFinderPollSteps()
{
    FinderPollFacts p;
    p.familySize = 5;
    Check("not joined: join", FinderNext(p) == FinderStep::Join);
    p.anyoneNotReady = true;
    Check("a member in a fight: wait before joining", FinderNext(p) == FinderStep::Wait);
    p.anyoneNotReady = false;
    p.joined = true;
    p.state = FinderState::RoleCheck;
    Check("the role check runs: wait", FinderNext(p) == FinderStep::Wait);
    p.state = FinderState::Proposal;
    p.proposalSeen = true;
    Check("a proposal: accept", FinderNext(p) == FinderStep::Accept);
    p.anyoneNotReady = true;
    Check("but not into a fight", FinderNext(p) == FinderStep::Wait);
    p.anyoneNotReady = false;
    p.state = FinderState::None;
    p.proposalSeen = false;
    Check("the core let the family go: give up", FinderNext(p) == FinderStep::GiveUp);
    p.state = FinderState::Dungeon;
    p.inside = 5;
    Check("all five inside: inside", FinderNext(p) == FinderStep::Inside);
    p.waitedSeconds = 10000;
    Check("inside beats the clock", FinderNext(p) == FinderStep::Inside);
    p.inside = 4;
    Check("a finder group with four inside at the ceiling is staged inside: the door "
          "brings the fifth",
          FinderNext(p) == FinderStep::Inside);
    p.inside = 0;
    Check("nobody inside at the ceiling: give up", FinderNext(p) == FinderStep::GiveUp);
    p.state = FinderState::Queued;
    p.inside = 0;
    Check("still queued at the ceiling: give up", FinderNext(p) == FinderStep::GiveUp);
    p.waitedSeconds = 30;
    p.state = FinderState::Dungeon;
    p.inside = 3;
    Check("the group is made and two are outside: the finder's own teleport in",
          FinderNext(p) == FinderStep::Teleport);
    p.anyoneNotReady = true;
    Check("but not while one is in a fight", FinderNext(p) == FinderStep::Wait);
    Check("the refusal words are the core's",
          std::string(FinderJoinResultWord(6)).find("party member") != std::string::npos);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheRungsHoldTheLeader()
{
    HeadTravelFacts between;
    between.campaignBetweenAttempts = true;
    between.trainerYards = 100.f;
    Check("between attempts a trainer in town may take the head",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between));
    between.recoveryHoldsTheLeader = true;
    Check("but not off the stone or out of the finder's queue",
          HeadErrandMayTravel(HeadErrand::TrainerTrip, between), false);
    Check("nor anything else below the run",
          HeadErrandMayTravel(HeadErrand::Other, between), false);
    Check("the run's own walk still goes",
          HeadErrandMayTravel(HeadErrand::ActiveRun, between));
    Check("and the wait says why", *HeadErrandWaitReason(HeadErrand::TrainerTrip, between) != '\0');
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
    has("the streak reaches the facts", "facts.streak = attempt;");
    has("the summon threshold is a config key", "\"Overseer.Recovery.SummonAfterFailures\"");
    has("the finder is behind a config key", "\"Overseer.Recovery.DungeonFinder\"");
    has("the stone is a spawn of the core's tables",
        "tmpl->type != GAMEOBJECT_TYPE_MEETINGSTONE");
    has("the summon is kind='summon', asked through the queue",
        "VALUES ('{}', 'use {}', 'summon', '{}', '{}')");
    has("the family joins through the core's own LFG join",
        "sLFGMgr->JoinLfg(leader, OverseerDecisions::FinderRoleMask(leader->getClass(), true),");
    has("members answer the role check through the core's handler",
        "p->GetSession()->HandleLfgSetRolesOpcode(roles);");
    has("and accept through the core's handler",
        "p->GetSession()->HandleLfgProposalResultOpcode(answer);");
    has("the fall height is re-anchored the way upstream #2754 does",
        "if (p && p->IsInWorld() && !p->Unit::IsFalling())");
    has("a member the core did not teleport presses the finder's own teleport in",
        "p->GetSession()->HandleLfgTeleportOpcode(in);");
    has("the door's own wing, not the map's first",
        "OverseerDecisions::ChooseFinderDungeon(candidates, landingX, landingY, why);");
    has("the proposal id is read off the core's packet",
        "OverseerWorldScript::NoteFinderProposal(name, packet->read<uint32>(5),");
    has("a finder group is let go before the reset", "group->Disband();");
    has("the reset asks it", "if (LeaveTheFinderGroup(leaderName, coord.runNumber, portal->insideMapId))");
    has("the family inside is staged inside", "next.phase = DungeonRunPhase::StagedInside;");
    has("every use is said at ERROR with the day's count", "Used {} time(s) today for");
    has("the rungs hold the leader against other errands",
        "facts.recoveryHoldsTheLeader = _recoveryHoldMembers.count(name) > 0;");
    has("the recovery that brings the family keeps the leader",
        "r == OverseerDecisions::RunRecovery::Summon ||");

    // Neither rung moves anybody by a road of its own.
    std::size_t const begin = source.find("    bool DriveSummonRung(");
    std::size_t const end = source.find("    unsigned FinderUsesToday(", begin);
    Check("the rungs exist", begin != std::string::npos && end != std::string::npos);
    if (begin != std::string::npos && end != std::string::npos)
    {
        std::string const rungs = source.substr(begin, end - begin);
        Check("and never teleport", rungs.find("TeleportTo") == std::string::npos);
        Check("and never grant a spell", rungs.find("learnSpell") == std::string::npos);
        Check("or an item", rungs.find("AddItem") == std::string::npos &&
                                rungs.find("StoreNewItem") == std::string::npos);
    }
}

}  // namespace

int main()
{
    TheSummonRungOpensAtTheStreak();
    WhatCannotBeSummonedAroundStillComesFirst();
    ASummonTriedTwiceRunningHandsBackToTheLadder();
    TheFinderComesOnlyAfterTheSummon();
    ASummonFromAnEarlierStreakDoesNotCount();
    ADoorWithNoStoneGoesStraightToTheFinder();
    TheWordsRoundTrip();
    TheRungWalksWaitsAndSummons();
    TheRowIsFoundAgainByItsSource();
    TheFamilyOffersTheRolesItsClassesCanFill();
    TheFinderReadsTheGroupAndTheCoresLocks();
    TheFinderPicksTheDoorsOwnWing();
    TheFamilyMustMakeAFinderGroup();
    TheFinderPollSteps();
    TheRungsHoldTheLeader();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("the fallback ladder escalates from the walk to the stone to the finder\n");
    return 0;
}
