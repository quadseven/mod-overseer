#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream input("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(input)), {});
    auto const drive = source.find("    void DriveTravel()");
    auto const refusal = source.find("if (state.inertFollowerAim)", drive);
    auto const walk = source.find("AssertTravelFocus(name, botAI, target)", refusal);
    if (drive == std::string::npos || refusal == std::string::npos || walk == std::string::npos)
        return 1;

    std::string const block = source.substr(refusal, walk - refusal);
    if (block.find("columnAim == target") == std::string::npos ||
        block.find("!_travelAims.RunOwns(name, columnAim)") == std::string::npos ||
        block.find("state.inertPoll != previousTravelPoll") == std::string::npos ||
        block.find("InertFollowerAimMayBeCleared(target, state.learn, inertSeconds)") == std::string::npos ||
        block.find("_travelAims.ReleaseInertFollowerAim(name, columnAim)") == std::string::npos)
    {
        std::cerr << "FAIL the travel drive must retire the exact column aim after repeated follower refusals\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
