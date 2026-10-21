#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream input("src/mod_overseer.cpp");
    if (!input)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const source((std::istreambuf_iterator<char>(input)), {});
    std::size_t const start = source.find("void TrainFishingOnArrival(");
    std::size_t const end = source.find("// FLIGHT POINT DISCOVERY ON ARRIVAL", start);
    if (start == std::string::npos || end == std::string::npos)
    {
        std::printf("FAIL cannot find TrainFishingOnArrival\n");
        return EXIT_FAILURE;
    }
    std::string const function = source.substr(start, end - start);
    bool const rankupsAllowed = function.find("if (bot->HasSkill(SKILL_FISHING))\n            return") == std::string::npos;
    bool const existingSkillNeedsNoFlag = function.find("if (!LoadLearnFishingFlag(name))\n            return") == std::string::npos;
    bool const capReadback = function.find("GetPureMaxSkillValue(SKILL_FISHING)") != std::string::npos;
    if (!rankupsAllowed || !existingSkillNeedsNoFlag || !capReadback)
    {
        std::printf("FAIL rank-up arrival must handle existing Fishing without learn flag and read back its cap\n");
        return EXIT_FAILURE;
    }
    std::printf("test_fishing_rankup_source: tier-up path is present\n");
    return EXIT_SUCCESS;
}
