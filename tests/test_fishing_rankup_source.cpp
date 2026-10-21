#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

static std::string Read(std::string const& path)
{
    std::ifstream input(path);
    if (!input)
        return {};
    return std::string((std::istreambuf_iterator<char>(input)), {});
}

int main()
{
    std::string const source = Read("src/mod_overseer.cpp");
    std::size_t const start = source.find("void TrainFishingOnArrival(");
    std::size_t const end = source.find("// FLIGHT POINT DISCOVERY ON ARRIVAL", start);
    if (start == std::string::npos || end == std::string::npos)
    {
        std::printf("FAIL cannot find TrainFishingOnArrival\n");
        return EXIT_FAILURE;
    }

    std::string const function = source.substr(start, end - start);
    std::string const docs = Read("docs/design/fishing-drive.md");
    bool const rankupsAllowed = function.find("bool const alreadyHasSkill = bot->HasSkill(SKILL_FISHING);") != std::string::npos &&
                                function.find("if (!alreadyHasSkill && !LoadLearnFishingFlag(name))") != std::string::npos;
    bool const trainerRechecked = source.find("static uint32 TrainerSpellForFishing(") != std::string::npos &&
                                  source.find("trainer->CanTeachSpell(bot, &spell)") != std::string::npos;
    bool const capReadback = function.find("GetPureMaxSkillValue(SKILL_FISHING)") != std::string::npos &&
                             function.find("GetPureMaxSkillValue(SKILL_FISHING)) > maxBefore") != std::string::npos;
    bool const liveGapDocumented = docs.find("have not been proven live") != std::string::npos &&
                                   docs.find("validation environment has") != std::string::npos &&
                                   docs.find("live claim remains open") != std::string::npos;
    bool const artisanAndMasterGateDocumented = docs.find("Item 16083") != std::string::npos &&
                                                docs.find("quest 6607") != std::string::npos &&
                                                docs.find("track its four fish objectives") != std::string::npos;
    if (!rankupsAllowed || !trainerRechecked || !capReadback || !liveGapDocumented || !artisanAndMasterGateDocumented)
    {
        std::printf("FAIL fishing rank-up code and limitation documentation must stay aligned\n");
        return EXIT_FAILURE;
    }
    std::printf("test_fishing_rankup_source: tier-up path and verification limits are documented\n");
    return EXIT_SUCCESS;
}
