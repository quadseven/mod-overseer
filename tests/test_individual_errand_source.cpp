/*
 * Source contract for individual errands (#1517).
 *
 * The rule is proved in test_individual_errand.cpp. What only the adapter can
 * get wrong, and what this file pins:
 *
 *  - Overseer.Individual.Enable is read with a default of false, so the module
 *    ships dark, and the conf template documents it with Default 0.
 *  - The travel poll refreshes the set before it returns on an empty column.
 *  - MaySteerItself asks WalksItsOwnErrand, so the travel grant and the
 *    follow sweep's strip agree.
 *  - DriveCatchUp returns for a member on its own errand before any walk back.
 *  - The regroup wait and the fetch both read it, so the leader is never held
 *    for, or sent back for, a member on its own errand.
 *  - The errand death breaker counts only that member's own deaths, through
 *    the pure ErrandPartyDeaths, and a member on its own errand is left out of
 *    every other walker's party toll through PartyTollCounts.
 *  - A failed job read fails closed; a landed aim ends the errand; a flight
 *    or a Mage's portal is never taken on one; and a released column ends it
 *    at once (WalksItsOwnErrand asks the column too).
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

std::string Read(char const* path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

bool Before(std::string const& text, char const* first, char const* second)
{
    std::size_t const a = text.find(first);
    std::size_t const b = text.find(second);
    return a != std::string::npos && b != std::string::npos && a < b;
}
}  // namespace

int main()
{
    std::string const text = Read("src/mod_overseer.cpp");
    if (text.empty())
    {
        std::cerr << "FAIL cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }

    std::string const refresh = Body(text, "    void RefreshIndividualErrands(");
    Check(!refresh.empty(), "RefreshIndividualErrands exists");
    Check(refresh.find("GetOption<bool>(\"Overseer.Individual.Enable\", false, false)") !=
              std::string::npos,
          "the switch is read with a default of false");
    Check(refresh.find("OverseerDecisions::IndividualErrandSet(enabled, rows)") !=
              std::string::npos,
          "the set is decided by the pure rule, gated by the switch");
    Check(refresh.find("OverseerDecisions::IndividualErrandChanges(_individualErrands, next)") !=
              std::string::npos,
          "each start and stop is logged from the pure swap");
    Check(refresh.find("LoadAllJobs(jobsRead)") != std::string::npos &&
              refresh.find("facts.familyLinked = !jobsRead ||") != std::string::npos,
          "a failed job read fails closed");
    Check(refresh.find("_travelAims.HoldsLanding(name, aim)") != std::string::npos &&
              refresh.find("facts.landedOnIt = _individualLanded.count(name) != 0;") !=
                  std::string::npos,
          "a landed aim ends the errand");
    Check(refresh.find("facts.leaderMapId = leader ? leader->GetMapId() : 0;") !=
              std::string::npos,
          "the aim's map is asked against the leader's");
    Check(refresh.find("OverseerDecisions::FamilyLinked(") != std::string::npos,
          "a family on a dungeon or raid job is linked");
    Check(refresh.find("family.empty() ||") != std::string::npos,
          "a member whose family the roster does not name fails closed");
    Check(refresh.find("InAnyDungeonRun(bot)") != std::string::npos, "a run re-links");
    Check(refresh.find("IsCatchingUp(name)") != std::string::npos &&
              refresh.find("_travelAims.RunOwns(name, aim)") != std::string::npos,
          "a catch-up or run claim is not the member's own errand");

    std::string const travel = Body(text, "    void DriveTravel()");
    Check(Before(travel, "RefreshIndividualErrands(aims);", "if (aims.empty())"),
          "the travel poll refreshes the set before it returns on an empty column");

    std::string const steer = Body(text, "    bool MaySteerItself(std::string const& name) const");
    Check(steer.find("WalksItsOwnErrand(name)") != std::string::npos,
          "MaySteerItself asks WalksItsOwnErrand");

    std::string const catchUp = Body(text, "    void DriveCatchUp(");
    Check(Before(catchUp, "if (WalksItsOwnErrand(name))", "DriveStrandedMember("),
          "DriveCatchUp returns for a member on its own errand before any walk back");

    std::string const together = Body(text, "    void KeepTheFamilyTogether(");
    Check(together.find("member.onItsOwnErrand = WalksItsOwnErrand(member.name);") !=
              std::string::npos,
          "the regroup wait reads it");

    std::string const fetch = Body(text, "    void DriveFetch(");
    Check(fetch.find("member.onItsOwnErrand = WalksItsOwnErrand(member.name);") !=
              std::string::npos,
          "the fetch reads it");

    // The guard and the assignment are one statement, so deleting the guard
    // cannot leave the assignment behind; and it follows the party read.
    Check(Before(text, "toll.partyDeaths = RecentPartyDeathCount(bot, window);",
                 "toll.partyDeaths = OverseerDecisions::ErrandPartyDeaths(\n"
                 "                    WalksItsOwnErrand(name), toll.deaths, toll.partyDeaths);"),
          "the walker's own toll goes through ErrandPartyDeaths with its own errand");
    std::string const party = Body(text, "    uint32 RecentPartyDeathCount(");
    Check(party.find("OverseerDecisions::PartyTollCounts(WalksItsOwnErrand(member->GetName()))") !=
              std::string::npos,
          "a member on its own errand is left out of another walker's party toll");

    std::string const walks = Body(text, "    bool WalksItsOwnErrand(std::string const& name) const");
    Check(walks.find("!_travelAims.TargetFor(name).empty()") != std::string::npos,
          "a released column ends the errand at once");
    std::string const flight = Body(text, "    bool ConsiderFlight(");
    Check(flight.find("if (WalksItsOwnErrand(name))") != std::string::npos,
          "no flight is taken on an errand of its own");
    Check(travel.find("!WalksItsOwnErrand(name) &&\n                MageTravelHolds(") !=
              std::string::npos,
          "no Mage portal is taken on an errand of its own");

    std::string const conf = Read("conf/mod_overseer.conf.dist");
    Check(conf.find("#Overseer.Individual.Enable = 0") != std::string::npos,
          "the conf template documents the switch off by default");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "individual errand source: ok\n";
    return EXIT_SUCCESS;
}
