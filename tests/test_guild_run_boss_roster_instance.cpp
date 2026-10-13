#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    std::string const code = text.str();
    std::size_t const start = code.find("uint32 const brainMask =");
    std::size_t const end = start == std::string::npos ? start : code.find(';', start);
    if (start == std::string::npos || end == std::string::npos)
    {
        std::puts("FAIL could not find the guild run boss roster read");
        return 1;
    }

    std::string const read = code.substr(start, end - start);
    if (read.find("tank->GetMapId() == run.mapId") == std::string::npos ||
        read.find("(!run.instanceId || tank->GetInstanceId() == run.instanceId)") ==
            std::string::npos ||
        read.find("DungeonBrainExpectedEncounterMask(tank)") == std::string::npos)
    {
        std::puts("FAIL the boss roster read must use the run's own instance");
        return 1;
    }
    std::puts("ok");
    return 0;
}
