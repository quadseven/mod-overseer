/*
 * A family dungeon run only owns members in the family's party.
 *
 * This pins the adapter wiring that prevents a guild finder party from being
 * adopted and prevents its members from starting a family client loss hold.
 * The worldserver records for the historical Shadowfang runs are not part of
 * this checkout, so this test guards the shared code path directly.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

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
}

int main()
{
    std::string const source = ReadSource();
    Check("source is readable", !source.empty());

    std::string const adoption = Between(source, "if (coord.phase == DungeonRunPhase::Idle)",
                                         "// HOW MANY RUNS ARE LEFT");
    // #1045: the family's own finder group is its party; a guild run's
    // group is what is excluded (test_family_finder_party.cpp).
    Check("guild run groups are excluded from the family party",
          source.find("facts.heldByGuildRun = NamedByAGuildRun(") != std::string::npos &&
              source.find("FamilyPartyMembers(") != std::string::npos);
    Check("outside roster members are not considered for adoption",
          adoption.find("for (std::string const& member : familyParty)") != std::string::npos);
    Check("adoption census only counts the family party",
          adoption.find("DungeonRunCensus(familyParty,") != std::string::npos);

    std::string const clientGate = Between(source, "bool hasInsideMember = false;",
                                           "OverseerDecisions::LeaderClientGate const leaderClientGate");
    Check("only family party members can trigger the client loss gate",
          clientGate.find("InTheFamilyRunParty(candidate, family)") != std::string::npos);

    std::string const census = Between(source, "DungeonRunPhase::StagedInside and",
                                      "// THE RUN JOINS ITS CAMPAIGN");
    Check("staged inside census only includes the family party",
          census.find("FamilyPartyMembers(members, family)") != std::string::npos);

    return failures ? 1 : 0;
}
