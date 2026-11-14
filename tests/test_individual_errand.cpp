/*
 * A follower walks its own errand while its family is not linked (#1517,
 * part of wow-overseer#2395).
 *
 * Outside a dungeon or a raid, a family member may walk to its own trainer,
 * vendor or banker. Before this, a follower's own aim answered
 * RefuseInFormation and walked nobody. This file proves the pure halves:
 *
 *  - which jobs link a family (only a dungeon or a raid job);
 *  - when a follower steers itself for its own errand;
 *  - that the family does not hold its leader for a member on its own errand,
 *    and the leader is never sent back to fetch one.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}

OverseerDecisions::IndividualErrandFacts Walking()
{
    OverseerDecisions::IndividualErrandFacts facts;
    facts.enabled = true;
    facts.leadsItsParty = false;
    facts.familyLinked = false;
    facts.inARun = false;
    facts.sameMapAsLeader = true;
    facts.ownAim = "trainer";
    facts.claimedByAnotherOwner = false;
    return facts;
}
}  // namespace

int main()
{
    using namespace OverseerDecisions;

    // --- which jobs link a family ---------------------------------------------
    Check(IsLinkingJob("dungeon"), "a bare dungeon job links");
    Check(IsLinkingJob("dungeon:wailing"), "a named dungeon job links");
    Check(IsLinkingJob("raid:moltencore"), "a raid job links");
    Check(!IsLinkingJob("town run"), "town run does not link");
    Check(!IsLinkingJob("raid prep"), "raid prep does not link");
    Check(!IsLinkingJob("craft"), "craft does not link");
    Check(!IsLinkingJob("quest"), "quest does not link");
    Check(!IsLinkingJob(""), "an empty job does not link");
    Check(!IsLinkingJob("dungeonx"), "a word that only starts like dungeon does not link");

    Check(FamilyLinked({"craft", "dungeon:wailing"}), "one dungeon job links the family");
    Check(!FamilyLinked({"craft", "quest", "town run"}), "trade jobs leave it unlinked");
    Check(!FamilyLinked({}), "no jobs read is not linked");

    // --- when a follower steers itself for its own errand ----------------------
    Check(IndividualErrandSteers(Walking()), "a follower with its own errand walks it");
    {
        auto f = Walking();
        f.enabled = false;
        Check(!IndividualErrandSteers(f), "the switch off moves nobody (the module ships dark)");
    }
    {
        auto f = Walking();
        f.leadsItsParty = true;
        Check(!IndividualErrandSteers(f), "the leader is not this rule's: it walks as leader");
    }
    {
        auto f = Walking();
        f.familyLinked = true;
        Check(!IndividualErrandSteers(f), "a family on a dungeon or raid job stays linked");
    }
    {
        auto f = Walking();
        f.inARun = true;
        Check(!IndividualErrandSteers(f), "a member inside a run stays the run's");
    }
    {
        auto f = Walking();
        f.sameMapAsLeader = false;
        Check(!IndividualErrandSteers(f), "an errand off the leader's map is not this rule's");
    }
    {
        auto f = Walking();
        f.ownAim.clear();
        Check(!IndividualErrandSteers(f), "no errand, no walk: it follows");
    }
    {
        auto f = Walking();
        f.ownAim = "guard:0:1,2,3";
        Check(!IndividualErrandSteers(f), "a guard is the operator's and steers by its own rule");
    }
    {
        auto f = Walking();
        f.claimedByAnotherOwner = true;
        Check(!IndividualErrandSteers(f), "a catch-up, run or escort claim is not its own errand");
    }

    // --- the family does not hold its leader for a member on its own errand ---
    RegroupLimits limits;
    limits.splitYards = 60.f;
    limits.rejoinYards = 20.f;
    limits.maxWaitYards = 0.f;

    RegroupMember away;
    away.name = "Og";
    away.seen = true;
    away.sameMap = true;
    away.alive = true;
    away.yards = 300.f;
    Check(ReadRegroupClaim(away, limits, false) == RegroupClaim::Rejoining,
          "on main a far follower is waited for");
    away.onItsOwnErrand = true;
    Check(ReadRegroupClaim(away, limits, false) == RegroupClaim::OwnErrand,
          "a follower on its own errand is its own claim");
    Check(!RegroupClaimIsWaitedFor(RegroupClaim::OwnErrand),
          "the leader is never held for a member on its own errand");
    Check(!ReadFamilyRegroup({away}, limits, false).wait,
          "the family does not wait for a member on its own errand");
    Check(std::string(RegroupClaimName(RegroupClaim::OwnErrand)) == "on an errand of its own",
          "the claim has a name the log can say");

    RegroupMember near = away;
    near.yards = 10.f;
    Check(ReadRegroupClaim(near, limits, false) == RegroupClaim::InFormation,
          "a member beside the leader is in formation whatever it is doing");

    // --- and the leader is never sent back for one ------------------------------
    FetchLimits fetch;
    fetch.footLimitYards = 100.f;
    fetch.gatheredYards = 40.f;

    FetchCandidate held;
    held.name = "Og";
    held.seen = true;
    held.sameMap = true;
    held.alive = true;
    held.heldTooFar = true;
    held.yards = 400.f;
    Check(PickFetchTarget({held}, fetch, false) == "Og", "on main a held member is fetched");
    held.onItsOwnErrand = true;
    Check(PickFetchTarget({held}, fetch, false).empty(),
          "a member on its own errand is never fetched");

    FetchCandidate walking;
    walking.name = "Bork";
    walking.seen = true;
    walking.sameMap = true;
    walking.alive = true;
    walking.yards = 300.f;
    FetchCandidate other = held;
    other.name = "Grog";
    other.onItsOwnErrand = false;
    Check(PickFetchTarget({walking, other}, fetch, false).empty(),
          "on main a member still walking back holds every fetch");
    walking.onItsOwnErrand = true;
    Check(PickFetchTarget({walking, other}, fetch, false) == "Grog",
          "a member on its own errand is not one still walking back");

    // --- jobs are read the way the bridge reads them ---------------------------
    Check(IsLinkingJob(" Dungeon:Wailing "), "case and spaces do not hide a dungeon job");
    Check(IsLinkingJob("RAID:moltencore"), "case does not hide a raid job");

    // --- the destination must be on the leader's map, and walked on foot -----
    {
        auto f = Walking();
        f.leaderMapId = 0;
        f.ownAim = "at:0:-8815.2,652.9,94.9";
        Check(IndividualErrandSteers(f), "an at: aim on the leader's map is walked");
        f.ownAim = "at:1:1782.0,-1627.8,61.5";
        Check(!IndividualErrandSteers(f), "an at: aim on another map is not");
        f.ownAim = "at:garbage";
        Check(!IndividualErrandSteers(f), "an unreadable at: aim is not");
        f.ownAim = "trigger:324";
        Check(!IndividualErrandSteers(f), "an areatrigger leads off the map and is not walked alone");
        f.ownAim = "flight master";
        Check(!IndividualErrandSteers(f), "a flight is not an errand walked alone");
    }

    // --- an errand ends when it lands ------------------------------------------
    {
        auto f = Walking();
        f.landedOnIt = true;
        Check(!IndividualErrandSteers(f),
              "a member that landed on its aim follows again, whoever wrote the aim");
    }

    // --- the switch gates the whole set ----------------------------------------
    std::vector<std::pair<std::string, IndividualErrandFacts>> rows{{"Og", Walking()},
                                                                    {"Bork", Walking()}};
    rows[1].second.familyLinked = true;
    Check(IndividualErrandSet(false, rows).empty(), "the switch off leaves the set empty");
    Check(IndividualErrandSet(true, rows) == std::set<std::string>{"Og"},
          "the switch on picks only the members the rule picks");

    // --- the swap says who started and who stopped -----------------------------
    IndividualErrandChange const change =
        IndividualErrandChanges({"Og", "Ugga"}, {"Og", "Bork"});
    Check(change.started == std::vector<std::string>{"Bork"}, "Bork started");
    Check(change.stopped == std::vector<std::string>{"Ugga"}, "Ugga stopped");

    // --- deaths: each walker answers for its own ------------------------------
    Check(ErrandPartyDeaths(true, 1, 9) == 1,
          "a member on its own errand counts only its own deaths");
    Check(ErrandPartyDeaths(false, 1, 9) == 9, "anyone else counts its party's");
    Check(!PartyTollCounts(true),
          "a member away on its own errand is not in another walker's party toll");
    Check(PartyTollCounts(false), "a member with the family is");

    // --- a dungeon job, a run or a summon re-links the family (#1519) ----------
    {
        FamilyReLinkFacts facts;
        facts.memberJobs = {"town run", "town run"};
        Check(!FamilyReLinks(facts), "town run, no run, no summon: stays individual");
        facts.memberJobs = {"town run", "dungeon:wailing"};
        Check(FamilyReLinks(facts), "a dungeon job on any member re-links");
        facts.memberJobs = {"quest", " RAID:moltencore "};
        Check(FamilyReLinks(facts), "a raid job on any member re-links");
        facts.memberJobs = {"quest", "craft"};
        facts.anyMemberInRun = true;
        Check(FamilyReLinks(facts), "a run started by any member re-links the family");
        facts.anyMemberInRun = false;
        facts.summonAsked = true;
        Check(FamilyReLinks(facts), "a meeting stone summon asked for re-links");
    }
    Check(ReleasesItsOwnErrand(true, true, false),
          "a re-linked member's own errand is released");
    Check(!ReleasesItsOwnErrand(true, true, true),
          "a catch-up, escort or run claim is not its own errand and is never released here");
    Check(!ReleasesItsOwnErrand(false, true, false),
          "a member that walked no errand of its own has nothing to release");
    Check(!ReleasesItsOwnErrand(true, false, false),
          "an individual family keeps its errands");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "individual errand: ok\n";
    return EXIT_SUCCESS;
}
