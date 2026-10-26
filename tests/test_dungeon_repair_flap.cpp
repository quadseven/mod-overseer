/*
 * The repair leg must not re-grant a repair errand that the travel drive
 * has already refused for a character. When the travel drive refuses a
 * repair errand (e.g. "no such spawn on map N" - no repairer in range),
 * it records the refusal in TravelAimBook::_refused. The dungeon run's
 * DriveRepairLeg must check this before re-claiming the "repair" aim on
 * its next poll.
 *
 * This test verifies the source contains the fix.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

namespace
{
int failures = 0;

std::string ReadModule()
{
    std::ifstream file("src/mod_overseer.cpp");
    if (!file.is_open())
        return std::string();
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void Check(char const* what, bool condition)
{
    if (condition)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void TheRepairLegChecksRefusalBeforeReclaiming()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        Check("src/mod_overseer.cpp is readable from the working directory", false);
        return;
    }

    // Find DriveRepairLeg function
    std::size_t const leg = source.find("void DriveRepairLeg(DungeonRunCoordinatorState& coord,");
    if (leg == std::string::npos)
    {
        Check("DriveRepairLeg function exists", false);
        return;
    }

    // Find the Walk case in the switch
    std::size_t const walkCase = source.find("case OverseerDecisions::RepairLegStep::Walk:", leg);
    if (walkCase == std::string::npos)
    {
        Check("Walk case exists in DriveRepairLeg", false);
        return;
    }

    // The fix: before EscortToward, check SecondsSinceRefused for "repair"
    std::size_t const checkRefusal = source.find("_travelAims.SecondsSinceRefused(name, \"repair\")", walkCase);
    if (checkRefusal == std::string::npos)
    {
        Check("DriveRepairLeg checks SecondsSinceRefused for repair before EscortToward", false);
        return;
    }

    // Should not call EscortToward if refused
    std::size_t const escortToward = source.find("EscortToward(name, \"repair\"", walkCase);
    if (escortToward == std::string::npos)
    {
        Check("EscortToward for repair exists in Walk case", false);
        return;
    }

    // The check should be before the EscortToward call
    Check("Refusal check is before EscortToward call", checkRefusal < escortToward);

    // Should log once using SayRepairLegOnce with a reason indicating refusal
    std::size_t const sayOnce = source.find("SayRepairLegOnce(coord, name,", walkCase);
    if (sayOnce == std::string::npos)
    {
        Check("SayRepairLegOnce is used to log the refusal", false);
        return;
    }

    // Should add to repairSettled so we don't keep trying
    std::size_t const settled = source.find("repairSettled.insert(name)", walkCase);
    if (settled == std::string::npos)
    {
        // Alternative: coord.repairSettled.insert(name) or SettleRepair(coord, name)
        std::size_t const settleRepair = source.find("SettleRepair(coord, name)", walkCase);
        if (settleRepair == std::string::npos)
        {
            Check("Member is marked settled when repair is refused", false);
            return;
        }
    }
}

} // namespace

int main()
{
    TheRepairLegChecksRefusalBeforeReclaiming();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_dungeon_repair_flap: all passed\n");
    return EXIT_SUCCESS;
}