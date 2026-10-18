/*
 * What a level-up hands the roster: its own talent points, and nothing else.
 *
 * TrainRoster ran PlayerbotFactory::InitSkills, InitClassSpells and
 * InitAvailableSpells on every level change of a roster character, with no
 * trainer and no gold, behind Overseer.Train.Factory (default on). Measured on
 * the operator's realm: a level 27 druid held two-handed maces, daggers,
 * polearms and fist weapons at 135/135 (27 x 5, the factory's druid list), a
 * warrior held all sixteen weapon skills at 300/300, and every Alliance member
 * held the three riding ranks up to 225.
 *
 * Natural only (the operator, 2026-10-12): the grants and the key are gone.
 * These checks read src/mod_overseer.cpp (run from the repo root) to pin that
 * TrainRoster makes no factory call, reads no key that could turn one back on,
 * and still spends the talent points the character earned by levelling.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
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

std::string ReadFile(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

// The body of TrainRoster, from its signature to SpendTalents' definition.
std::string TrainRosterBody(std::string const& source)
{
    std::size_t const begin = source.find("    void TrainRoster()\n");
    std::size_t const end = source.find("    static void SpendTalents(Player* bot, uint32 tabpage)", begin);
    if (begin == std::string::npos || end == std::string::npos)
        return "";
    return source.substr(begin, end - begin);
}

void TrainRosterGrantsNothing()
{
    std::string const source = ReadFile("src/mod_overseer.cpp");
    std::string const body = TrainRosterBody(source);
    Check("TrainRoster is found in src/mod_overseer.cpp (run from the repo root)", !body.empty());
    if (body.empty())
        return;

    Check("TrainRoster builds no bot factory", body.find("PlayerbotFactory factory") == std::string::npos);
    Check("TrainRoster makes no factory grant", body.find("factory.Init") == std::string::npos);
    Check("the factory key is read nowhere",
          source.find("GetOption<bool>(\"Overseer.Train.Factory\"") == std::string::npos);
    Check("TrainRoster still spends the earned talent points",
          body.find("SpendTalents(bot, static_cast<uint32>(specTab));") != std::string::npos);

    std::string const dist = ReadFile("conf/mod_overseer.conf.dist");
    Check("the dist no longer offers the key", dist.find("\n#Overseer.Train.Factory = ") == std::string::npos);
    Check("the dist says the key is retired",
          dist.find("Overseer.Train.Factory is retired") != std::string::npos);
}

}  // namespace

int main()
{
    TrainRosterGrantsNothing();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_roster_training: all passed\n");
    return 0;
}
