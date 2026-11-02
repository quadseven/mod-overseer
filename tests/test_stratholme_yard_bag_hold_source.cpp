/*
 * The bag-pressure hold drives an open Stratholme yard episode before it
 * returns (#1221).
 *
 * DungeonBagPressure::HoldOut ends the coordinator poll with a `return`. The
 * yard drive after the bag-pressure switch was never reached for a family
 * with full bags, so its exit-yard episode ran to the 15-minute ceiling
 * unpolled. This pins the order inside the HoldOut case: the episode is asked
 * about and driven first, and only then is the coordinator reset and held.
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

    std::string const hold =
        Between(source, "case OverseerDecisions::DungeonBagPressure::HoldOut:",
                "// A FAMILY IN STRATHOLME'S EXIT YARD IS GOT OUT OF IT BEFORE ANYTHING");
    Check("the HoldOut case is found", !hold.empty());

    std::string::size_type const asked = hold.find("StratholmeYardDrivesBeforeBagHold(");
    std::string::size_type const drive = hold.find("DriveStratholmeYard(family, leaderName, members)");
    std::string::size_type const firstReturn = hold.find("return;");
    std::string::size_type const reset = hold.find("coord = DungeonRunCoordinatorState();");
    Check("the HoldOut case asks whether an open yard episode drives first",
          asked != std::string::npos);
    Check("the HoldOut case drives the yard episode",
          drive != std::string::npos);
    Check("the yard is driven before the hold returns",
          drive != std::string::npos && firstReturn != std::string::npos &&
              drive < firstReturn);
    Check("the yard is driven before the coordinator is reset",
          drive != std::string::npos && reset != std::string::npos && drive < reset);

    // The replay in test_stratholme_yard_bag_hold.cpp asks the yard drive's
    // first question through StratholmeYardPollStep; the drive must too.
    std::string const yardDrive =
        Between(source, "bool DriveStratholmeYard(std::string const& family,",
                "Player* walker = nullptr;");
    Check("DriveStratholmeYard is found", !yardDrive.empty());
    Check("DriveStratholmeYard asks StratholmeYardPollStep whether to close or drive",
          yardDrive.find("OverseerDecisions::StratholmeYardPollStep(") != std::string::npos);
    Check("DriveStratholmeYard closes on EveryoneOut",
          yardDrive.find("step == OverseerDecisions::StratholmeYardPoll::EveryoneOut") !=
              std::string::npos);
    Check("DriveStratholmeYard gives up on Ceiling",
          yardDrive.find("step == OverseerDecisions::StratholmeYardPoll::Ceiling") !=
              std::string::npos);
    return failures ? 1 : 0;
}
