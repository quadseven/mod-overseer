/*
 * Source contract for the stranded member's way home (#274).
 *
 * The decisions are proved in test_stranded_member.cpp. What only the adapter
 * can get wrong is whether it ASKS them: the party poll has to put a split
 * member to DecideStrandedWay before it gives up on it, the boat has to be read
 * on the coordinator's five second clock, the steps on and off a deck have to
 * be the family crossing's own, and nothing in it may teleport anybody. A
 * decision nothing feeds is the failure this repository has shipped before, so
 * the wiring is pinned as text.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (text.find("NOTHING IN THIS MODULE CAN REJOIN") != std::string::npos)
        return Fail("the split line must not claim nothing can rejoin a split member");

    std::string const catchUp =
        Body(text, "void DriveCatchUp(Player* p, Player* leader, std::string const& rosterLeaderName)");
    if (catchUp.empty())
        return Fail("cannot find DriveCatchUp");
    std::size_t const asks = catchUp.find("DriveStrandedMember(p, leader, name, split)");
    std::size_t const escorted = catchUp.find("auto const it = _dungeonEscorts.find(name);");
    if (asks == std::string::npos || escorted == std::string::npos || !(asks < escorted))
        return Fail("the party poll must ask the stranded way before any walk it starts");

    std::string const stranded = Body(text, "bool DriveStrandedMember(");
    if (stranded.find("OverseerDecisions::DecideStrandedWay(facts)") == std::string::npos)
        return Fail("the stranded member must be decided by DecideStrandedWay");
    if (stranded.find("PriceCrossingTransports(") == std::string::npos)
        return Fail("a boat home must be priced the way the family's crossing is");
    if (stranded.find("DoHearth(") == std::string::npos ||
        stranded.find("m_homebindMapId == facts.leaderMap") == std::string::npos)
        return Fail("a member bound on its leader's map must be able to hearth there");

    std::size_t const clock = text.find("if (_dungeonRunTimer >= DUNGEON_RUN_POLL_MS)");
    std::size_t const lone = text.find("DriveLoneCrossings();", clock);
    std::size_t const travel = text.find("uint32 const travelPoll", clock);
    if (clock == std::string::npos || lone == std::string::npos || !(lone < travel))
        return Fail("the lone crossing must run on the coordinator's five second clock");

    std::string const drive = Body(text, "void DriveLoneCrossing(std::string const& name)");
    if (drive.empty())
        return Fail("cannot find DriveLoneCrossing");
    for (char const* needed :
         {"ReadCrossingFromWorld(", "OverseerDecisions::ReadCrossing(", "DeckEdgeToward(",
          "StepOntoGround(", "generatePath*/ false", "CROSSING_HOLD_VERB"})
        if (drive.find(needed) == std::string::npos)
        {
            std::cerr << "the lone crossing must use " << needed << "\n";
            return EXIT_FAILURE;
        }
    if (drive.find("TeleportTo(") != std::string::npos ||
        stranded.find("TeleportTo(") != std::string::npos)
        return Fail("nothing on a stranded member's way home may teleport it");

    std::string const sweep = Body(text, "void SweepDungeonEscorts()");
    if (sweep.find("it->second.crossing") == std::string::npos)
        return Fail("the run's sweep must leave a walk to a boat home to its own drive");

    std::cout << "a stranded member is decided, priced, hearthed or sailed, and never teleported\n";
    return EXIT_SUCCESS;
}
