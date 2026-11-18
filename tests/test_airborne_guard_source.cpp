/* Source contract for the airborne guard's adapter (wow-overseer issue 2937).
 *
 * The rule is tested in test_airborne_guard.cpp; this pins what the adapter
 * does with it: asked for every roster character on the per-second terrain
 * loop, one line naming the movement owner and where the spline ends, and the
 * remedy in the order that makes it safe (clear, fall, then rebase the fall
 * start that MoveFall writes, then release the aim).
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

bool InOrder(std::string const& text, std::initializer_list<char const*> needles)
{
    std::size_t at = 0;
    for (char const* needle : needles)
    {
        at = text.find(needle, at);
        if (at == std::string::npos)
            return false;
        ++at;
    }
    return true;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const start = text.find("    bool GuardAirborne(std::string const& name, Player* bot)");
    std::size_t const end = text.find("    void DriveBelowTerrainRecovery()", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("GuardAirborne must sit just before DriveBelowTerrainRecovery");
    std::string const guard = text.substr(start, end - start);

    if (!InOrder(guard, {"facts.onTaxi = bot->IsInFlight()", "facts.onTransport",
                         "facts.inVehicle", "facts.inWater", "HasFeatherFallAura()",
                         "GetWaterOrGroundLevel(",
                         "OverseerDecisions::AirborneGuardStep(", "AIRBORNE_GUARD_LIMITS"}))
        return Fail("the guard must read every exemption and the floor before asking the rule");
    if (!InOrder(guard, {"GetCurrentMovementGeneratorType()", "movespline->FinalDestination()",
                         "IS AIRBORNE", "Movement owner: {} generator"}))
        return Fail("the line must name the generator and where the spline in flight ends");
    if (!InOrder(guard, {"bot->StopMoving()", "motion->Clear()", "motion->MoveFall()",
                         "SetFallInformation(GameTime::GetGameTime().count(), facts.groundZ)",
                         "rpgInfo.ChangeToIdle()", "_travelAims.Refuse(", "_travelAims.Release("}))
        return Fail("the remedy must clear, fall, rebase the landing after MoveFall, idle and release");
    if (guard.find("TeleportTo") != std::string::npos)
        return Fail("the guard must never teleport");

    std::size_t const loop = text.find("    void DriveBelowTerrainRecovery()");
    std::size_t const onRoster = text.find("            if (!onRoster)\n                continue;", loop);
    std::size_t const call = text.find("if (GuardAirborne(name, bot))", loop);
    std::size_t const terrain = text.find("OverseerDecisions::NearTheVoidPlane(", loop);
    if (onRoster == std::string::npos || call == std::string::npos ||
        terrain == std::string::npos || !(onRoster < call && call < terrain))
        return Fail("the guard must run for roster characters, before the below-the-world reading");

    if (text.find("constexpr OverseerDecisions::AirborneGuardLimits AIRBORNE_GUARD_LIMITS{10.f, 5, 30};") ==
        std::string::npos)
        return Fail("the bound is ten yards for five seconds, repeated after thirty");

    std::cout << "airborne guard source: ok\n";
    return EXIT_SUCCESS;
}
