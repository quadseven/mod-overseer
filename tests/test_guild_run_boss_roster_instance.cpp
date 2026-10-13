#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return 1;
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

    std::string directory =
        (std::filesystem::temp_directory_path() / "overseer-roster-XXXXXX").string();
    if (!mkdtemp(directory.data()))
        return 1;
    std::string const path = std::string(directory) + "/probe";
    {
        std::ofstream probe(path + ".cpp");
        probe << R"CPP(
#include <cstdint>
#include <cstdio>
using uint32 = std::uint32_t;
struct Tank {
    bool inWorld;
    uint32 mapId;
    uint32 instanceId;
    bool IsInWorld() const { return inWorld; }
    uint32 GetMapId() const { return mapId; }
    uint32 GetInstanceId() const { return instanceId; }
};
struct Run { uint32 mapId; uint32 instanceId; };
uint32 DungeonBrainExpectedEncounterMask(Tank const*) { return 0x5a; }
uint32 Read(Tank const* tank, Run run)
{
)CPP" << code.substr(start, end - start + 1) << R"CPP(
    return brainMask;
}
int main()
{
    Tank tank{true, 7, 11};
    if (Read(nullptr, {7, 11}) != 0 ||
        Read(&tank, {8, 11}) != 0 ||
        Read(&tank, {7, 12}) != 0 ||
        Read(&tank, {7, 11}) != 0x5a ||
        Read(&tank, {7, 0}) != 0x5a)
        return 1;
    tank.inWorld = false;
    if (Read(&tank, {7, 11}) != 0)
        return 1;
    std::puts("ok");
}
)CPP";
    }
    int const compiled = std::system(("g++ -std=c++17 " + path + ".cpp -o " + path).c_str());
    int const tested = compiled == 0 ? std::system(path.c_str()) : 1;
    std::remove((path + ".cpp").c_str());
    std::remove(path.c_str());
    rmdir(directory.c_str());
    if (tested != 0)
        std::puts("FAIL the boss roster read must use the run's own instance");
    return tested == 0 ? 0 : 1;
}
