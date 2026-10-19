/*
 * Source contract for the warrior tank pursuit gate (mod-overseer#1055).
 *
 * The decision is proved in test_tank_pursuit_gate.cpp. What only the adapter
 * can get wrong is whether DriveEngagementSafety ASKS it before the MoveChase,
 * feeds it the instance map and the holds, and says so once per fight rather
 * than every poll. #1049 shipped the chase with the bare out-of-reach check
 * and no gate, so the wiring is pinned as text.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const start = text.find("    void DriveEngagementSafety()\n");
    if (start == std::string::npos)
        return Fail("cannot find DriveEngagementSafety");
    std::size_t const end = text.find("\n    }\n", start);
    if (end == std::string::npos)
        return Fail("cannot find the end of DriveEngagementSafety");
    std::string const body = text.substr(start, end - start);

    std::size_t const chase = body.find("MoveChase(combatVictim)");
    if (chase == std::string::npos)
        return Fail("DriveEngagementSafety no longer chases with MoveChase(combatVictim); "
                    "update this contract alongside it");
    if (body.find("MoveChase(", chase + 1) != std::string::npos)
        return Fail("DriveEngagementSafety has a second MoveChase; each must ask TankMayPursue");

    std::size_t const asks = body.find("OverseerDecisions::TankMayPursue(");
    if (asks == std::string::npos || asks > chase)
        return Fail("DriveEngagementSafety must ask TankMayPursue before its MoveChase (#1055)");

    if (body.find("WarriorNeedsMeleePursuit(") != std::string::npos)
        return Fail("DriveEngagementSafety calls WarriorNeedsMeleePursuit directly, past the "
                    "#1055 gate; ask TankMayPursue instead");

    std::string const before = body.substr(0, asks);
    char const* const fed[] = {"pursuit.onInstanceMap =", "Instanceable()",
                               "pursuit.heldStill =",     "HeldStill(name)",
                               "HeldAfterRevival(name)",  "pursuit.mounted =",
                               "pursuit.alive ="};
    for (char const* fact : fed)
    {
        if (before.find(fact) == std::string::npos)
        {
            std::cerr << "TankMayPursue is not fed before it is asked: " << fact << "\n";
            return EXIT_FAILURE;
        }
    }

    // One line per fight: the said-set is cleared when combat ends, and the
    // log sits behind its insert, after the decision and before the chase.
    if (body.find("if (!bot->IsInCombat())\n                _tankPursuitSaid.erase(name);") ==
        std::string::npos)
        return Fail("the pursuit's said-set is not cleared when combat ends (#1055)");
    std::size_t const said = body.find("_tankPursuitSaid.insert(name).second");
    std::size_t const line = body.find("gated by #1055", asks);
    if (said == std::string::npos || line == std::string::npos || said < asks ||
        line < said || line > chase)
        return Fail("the pursuit must log once per fight, behind the said-set, before it "
                    "chases (#1055)");

    std::cout << "tank pursuit gate source: all checks passed\n";
    return EXIT_SUCCESS;
}
