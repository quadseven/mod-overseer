/*
 * A party for a quest that needs help: the three rows, each gate refusal, and
 * when a standing party ends.
 *
 * The control plane's class-quest ask picks the answerers and sends
 * `party-up <helper> ...` on the asker, then `party-walk creature:<spawn id>`
 * on the same leader, and `party-disband` when the objective is done. These
 * are the decisions the adapter makes from them. One check per gate row, so a
 * row that stops refusing fails by name.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace D = OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Is(char const* got, char const* want)
{
    return std::strcmp(got, want) == 0;
}

void TheThreeRowsAreRoutedOnTheirFirstWord()
{
    Check("party-up routed", D::IsPartyUpRow("party-up Aldo Bria"));
    Check("party-walk routed", D::IsPartyWalkRow("party-walk creature:123"));
    Check("party-disband routed", D::IsPartyDisbandRow("party-disband"));
    Check("party-up is not party-walk", !D::IsPartyWalkRow("party-up Aldo"));
    Check("finder-run is not party-up", !D::IsPartyUpRow("finder-run deadmines A B C D"));
    Check("walk-to-spawn is not party-walk", !D::IsPartyWalkRow("walk-to-spawn creature:5"));
    Check("empty is no row", !D::IsPartyUpRow("") && !D::IsPartyDisbandRow("  "));
}

void APartyUpRowNamesOneToFourHelpers()
{
    D::PartyUpRequest r = D::ParsePartyUpRequest("party-up Aldo", "Leader");
    Check("one helper parses", r.error == D::PartyRowError::None && r.helpers.size() == 1 &&
                                   r.helpers[0] == "Aldo");
    r = D::ParsePartyUpRequest("party-up Aldo Bria Cato Dane", "Leader");
    Check("four helpers parse", r.error == D::PartyRowError::None && r.helpers.size() == 4);
    r = D::ParsePartyUpRequest("party-up Aldo Bria Cato Dane Edda", "Leader");
    Check("five helpers refused", r.error == D::PartyRowError::TooManyHelpers);
    r = D::ParsePartyUpRequest("party-up", "Leader");
    Check("no helper is malformed", r.error == D::PartyRowError::Malformed);
    Check("malformed names the grammar",
          std::string(D::PartyRowErrorWord(D::PartyRowError::Malformed)).find("party-up <helper>") !=
              std::string::npos);
    r = D::ParsePartyUpRequest("party-up Al3do", "Leader");
    Check("a digit is no character name", r.error == D::PartyRowError::BadName);
    r = D::ParsePartyUpRequest("party-up Aldo aldo", "Leader");
    Check("one helper twice refused", r.error == D::PartyRowError::SameNameTwice);
    r = D::ParsePartyUpRequest("party-up LEADER", "Leader");
    Check("the leader as helper refused", r.error == D::PartyRowError::SameNameTwice);
    r = D::ParsePartyUpRequest("finder-run x", "Leader");
    Check("another verb is not this one", r.error == D::PartyRowError::NotThisVerb);
}

void TheLeaderGateRefusesEachWall()
{
    D::PartyLeaderFacts f;
    Check("a free leader may form a party", Is(D::PartyLeaderGate(f), ""));
    f = D::PartyLeaderFacts{}; f.hasBotAI = false;
    Check("leader not a bot", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderNotABot));
    f = D::PartyLeaderFacts{}; f.inWorld = false;
    Check("leader not in world", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderNotInWorld));
    f = D::PartyLeaderFacts{}; f.alive = false;
    Check("leader dead", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderDead));
    f = D::PartyLeaderFacts{}; f.inCombat = true;
    Check("leader in combat", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderInCombat));
    f = D::PartyLeaderFacts{}; f.inInstance = true;
    Check("leader in an instance", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderInInstance));
    f = D::PartyLeaderFacts{}; f.inGroup = true;
    Check("leader already in a party", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderInAGroup));
    f = D::PartyLeaderFacts{}; f.leadsQuestParty = true; f.inGroup = true;
    Check("leader already leads a quest party",
          Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderHasAParty));
    f = D::PartyLeaderFacts{}; f.leadsCampaign = true;
    Check("leader leads a campaign", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderLeadsCampaign));
    f = D::PartyLeaderFacts{}; f.onRoster = true;
    Check("leader is a family member", Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderOnRoster));
    f = D::PartyLeaderFacts{}; f.onRoster = true; f.leadsCampaign = true;
    Check("a campaign is named before the roster",
          Is(D::PartyLeaderGate(f), D::PartyRefusal::LeaderLeadsCampaign));
}

void TheHelperGateRefusesEachWall()
{
    D::PartyHelperFacts f;
    f.yardsFromLeader = 10.f;
    Check("a free helper in range may be seated", Is(D::PartyHelperGate(f), ""));
    D::PartyHelperFacts g = f; g.inWorld = false;
    Check("helper not in world", Is(D::PartyHelperGate(g), D::PartyRefusal::NotInWorld));
    g = f; g.hasBotAI = false;
    Check("helper not a bot", Is(D::PartyHelperGate(g), D::PartyRefusal::NotABot));
    g = f; g.alive = false;
    Check("helper dead", Is(D::PartyHelperGate(g), D::PartyRefusal::Dead));
    g = f; g.inCombat = true;
    Check("helper in combat", Is(D::PartyHelperGate(g), D::PartyRefusal::InCombat));
    g = f; g.inInstance = true;
    Check("helper in an instance", Is(D::PartyHelperGate(g), D::PartyRefusal::InInstance));
    g = f; g.group = D::PartyGroupState::OtherParty;
    Check("helper in another party", Is(D::PartyHelperGate(g), D::PartyRefusal::InAnotherParty));
    g = f; g.group = D::PartyGroupState::ThisParty;
    Check("helper already in this party is fine", Is(D::PartyHelperGate(g), ""));
    g = f; g.sameMapAsLeader = false;
    Check("helper on another map", Is(D::PartyHelperGate(g), D::PartyRefusal::OtherMap));
    g = f; g.yardsFromLeader = D::PARTY_RANGE_YARDS;
    Check("helper at exactly the range is near", Is(D::PartyHelperGate(g), ""));
    g = f; g.yardsFromLeader = D::PARTY_RANGE_YARDS + 0.5f;
    Check("helper past the range is too far", Is(D::PartyHelperGate(g), D::PartyRefusal::TooFar));
    g = f; g.yardsFromLeader = -1.f;
    Check("an unread distance is too far", Is(D::PartyHelperGate(g), D::PartyRefusal::TooFar));
    g = f; g.familyCampaign = true;
    Check("helper of a campaigning family", Is(D::PartyHelperGate(g), D::PartyRefusal::FamilyCampaign));
    g = f; g.familyCampaign = true; g.group = D::PartyGroupState::OtherParty;
    Check("a family member in its own party is refused as grouped elsewhere",
          Is(D::PartyHelperGate(g), D::PartyRefusal::InAnotherParty));
    Check("the range is a hundred yards", D::PARTY_RANGE_YARDS == 100.0f);
}

void APartyWalkRowIsAWalkToACreatureSpawn()
{
    D::PartyWalkRequest r = D::ParsePartyWalkRequest("party-walk creature:4711");
    Check("a creature spawn parses", !*r.error && r.spawnWalkCommand == "walk-to-spawn creature:4711");
    r = D::ParsePartyWalkRequest("party-walk creature:4711 max:900");
    Check("max is carried", !*r.error && r.spawnWalkCommand == "walk-to-spawn creature:4711 max:900");
    r = D::ParsePartyWalkRequest("party-walk gameobject:4711");
    Check("a gameobject is refused", Is(r.error, D::PartyRefusal::NotAnObjective));
    r = D::ParsePartyWalkRequest("party-walk");
    Check("no spawn is malformed", Is(r.error, D::PartyRefusal::MalformedWalk));
    r = D::ParsePartyWalkRequest("party-walk creature:0");
    Check("spawn zero is malformed", Is(r.error, D::PartyRefusal::MalformedWalk));
    r = D::ParsePartyWalkRequest("party-walk creature:1 max:5 extra");
    Check("a stray word is malformed", Is(r.error, D::PartyRefusal::MalformedWalk));
}

void ThePartyWalkGateRefusesEachWall()
{
    D::PartyWalkFacts f;
    f.leadsQuestParty = true;
    f.farthestYards = 30.f;
    Check("a gathered party may walk", Is(D::PartyWalkGate(f), ""));
    D::PartyWalkFacts g = f; g.leadsQuestParty = false;
    Check("no party to walk", Is(D::PartyWalkGate(g), D::PartyRefusal::NoParty));
    g = f; g.anyoneDead = true;
    Check("a dead member holds the walk", Is(D::PartyWalkGate(g), D::PartyRefusal::Dead));
    g = f; g.anyoneOtherMap = true;
    Check("a member on another map holds the walk", Is(D::PartyWalkGate(g), D::PartyRefusal::OtherMap));
    g = f; g.farthestYards = D::PARTY_RANGE_YARDS + 1.f;
    Check("a straggler holds the walk", Is(D::PartyWalkGate(g), D::PartyRefusal::TooFar));
    g = f; g.farthestYards = -1.f;
    Check("an unread distance holds the walk", Is(D::PartyWalkGate(g), D::PartyRefusal::TooFar));
}

void AStandingPartyEndsOnDeathDepartureInstanceOrTheClock()
{
    D::PartyPollFacts f;
    Check("a healthy party stands", D::PartyNext(f) == D::PartyEnds::Standing);
    f.secondsUp = D::PARTY_CEILING_SECONDS - 1;
    Check("one second short of the ceiling stands", D::PartyNext(f) == D::PartyEnds::Standing);
    f.secondsUp = D::PARTY_CEILING_SECONDS;
    Check("the ceiling ends it", D::PartyNext(f) == D::PartyEnds::TimedOut);
    Check("the ceiling is thirty minutes", D::PARTY_CEILING_SECONDS == 1800);
    D::PartyPollFacts g;
    g.anyoneDead = true;
    Check("a death ends it", D::PartyNext(g) == D::PartyEnds::MemberDied);
    g = D::PartyPollFacts{}; g.anyoneGone = true;
    Check("a member gone ends it", D::PartyNext(g) == D::PartyEnds::MemberGone);
    g = D::PartyPollFacts{}; g.anyoneInInstance = true;
    Check("an instance ends it", D::PartyNext(g) == D::PartyEnds::InstanceEntered);
    g = D::PartyPollFacts{}; g.anyoneDead = true; g.anyoneGone = true; g.secondsUp = 99999;
    Check("death is named before the rest", D::PartyNext(g) == D::PartyEnds::MemberDied);
    Check("a fight is not in the facts, so it ends nothing",
          std::string(D::PartyEndsWord(D::PartyEnds::Standing)) == "standing");
}

}  // namespace

int main()
{
    TheThreeRowsAreRoutedOnTheirFirstWord();
    APartyUpRowNamesOneToFourHelpers();
    TheLeaderGateRefusesEachWall();
    TheHelperGateRefusesEachWall();
    APartyWalkRowIsAWalkToACreatureSpawn();
    ThePartyWalkGateRefusesEachWall();
    AStandingPartyEndsOnDeathDepartureInstanceOrTheClock();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_party_walk: ok\n");
    return 0;
}
