/*
 * A family in its own finder group is in its family's party (#1045).
 *
 * #1025 kept a family member lent to a guild finder run out of the family's
 * dungeon run (#1021) by dropping every finder group from the family's party.
 * A run the dungeon finder takes the family into (Overseer.DungeonFinder.
 * Default = 1) leaves the family in a finder group of its own, so the next
 * STAGED_INSIDE census counted nobody inside and the run ended 'emptied' with
 * the family still in the dungeon, and IDLE adoption could not adopt it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace OverseerDecisions;

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

FamilyPartyFacts FamilyFinderGroup()
{
    FamilyPartyFacts f;
    f.inGroup = true;
    f.finderGroup = true;
    f.leaderOfFamily = true;
    return f;
}

void TheFamilysOwnFinderGroupIsItsParty()
{
    Check("a family in its own finder group counts as the family's party",
          CountsInFamilyParty(FamilyFinderGroup()));

    FamilyPartyFacts plain = FamilyFinderGroup();
    plain.finderGroup = false;
    Check("a family in an ordinary party it leads counts", CountsInFamilyParty(plain));
}

void AGuildRunsGroupIsNotTheFamilys()
{
    // The #1021 shape: a family member lent to a guild finder run as its
    // healer, the run's tank not of the family.
    FamilyPartyFacts lent = FamilyFinderGroup();
    lent.leaderOfFamily = false;
    lent.heldByGuildRun = true;
    Check("a member lent to a guild finder run is not in the family's party",
          !CountsInFamilyParty(lent));

    // The same, with the guild run's tank a member of the same family.
    FamilyPartyFacts kinTank = FamilyFinderGroup();
    kinTank.heldByGuildRun = true;
    Check("a guild run's group led by the same family is still not the family's",
          !CountsInFamilyParty(kinTank));

    FamilyPartyFacts queued = kinTank;
    queued.finderGroup = false;
    Check("a guild run still queued is not the family's party", !CountsInFamilyParty(queued));
}

void EverythingElseStaysOut()
{
    Check("no group is no party", !CountsInFamilyParty(FamilyPartyFacts{}));

    FamilyPartyFacts other = FamilyFinderGroup();
    other.leaderOfFamily = false;
    Check("a finder group another family or a stranger leads is not the family's",
          !CountsInFamilyParty(other));

    FamilyPartyFacts bg = FamilyFinderGroup();
    bg.finderGroup = false;
    bg.coreOwnedGroup = true;
    Check("a battleground or battlefield group is never the family's",
          !CountsInFamilyParty(bg));
}

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}

void TheCoordinatorAsksTheDecision()
{
    std::string const source = ReadSource();
    Check("source is readable", !source.empty());

    std::string const adapter = Between(source, "bool InTheFamilyRunParty(Player* p",
                                        "std::vector<std::string> FamilyPartyMembers(");
    Check("the adapter asks CountsInFamilyParty",
          adapter.find("OverseerDecisions::CountsInFamilyParty(facts)") != std::string::npos);
    Check("the adapter reads guild runs, adoptions and strays as holding a group",
          adapter.find("NamedByAGuildRun(") != std::string::npos &&
              adapter.find("run.groupGuid") != std::string::npos);

    std::string const named = Between(source, "bool NamedByAGuildRun(std::string const& name)",
                                      "bool InTheFamilyRunParty(");
    Check("a guild run, a waiting adoption and a stray all name their members",
          named.find("InAGuildRun(name)") != std::string::npos &&
              named.find("_guildRunAdoptions") != std::string::npos &&
              named.find("_guildRunStrays") != std::string::npos);

    std::string const party = Between(source, "std::vector<std::string> FamilyPartyMembers(",
                                      "return party;");
    Check("FamilyPartyMembers keeps the family's run party, finder group included",
          party.find("InTheFamilyRunParty(member, family)") != std::string::npos &&
              party.find("InItsFamilyParty(") == std::string::npos);

    std::string const idle = Between(source, "Player* currentLeader = ObjectAccessor::FindPlayerByName(leaderName);",
                                     "if (!activeInside)");
    Check("IDLE adoption counts a leader in the family's finder group",
          idle.find("InTheFamilyRunParty(currentLeader, family)") != std::string::npos);
}

}  // namespace

int main()
{
    TheFamilysOwnFinderGroupIsItsParty();
    AGuildRunsGroupIsNotTheFamilys();
    EverythingElseStaysOut();
    TheCoordinatorAsksTheDecision();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
