/*
 * What a level-up hands the roster, and the switch that stops it.
 *
 * TrainRoster ran PlayerbotFactory::InitSkills, InitClassSpells and
 * InitAvailableSpells on every level change of a roster character, with no
 * trainer and no gold. Measured on the operator's realm: a level 27 druid held
 * two-handed maces, daggers, polearms and fist weapons at 135/135 (27 x 5, the
 * factory's druid list), a warrior held all sixteen weapon skills at 300/300,
 * and every Alliance member held the three riding ranks up to 225.
 *
 * Overseer.Train.Factory = 0 turns all three grants off and leaves the
 * talent spend on. These checks pin the decision, and read src/mod_overseer.cpp
 * (run from the repo root) to pin that TrainRoster honours it and reads the key
 * with a default of ON.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::RosterTraining;
using OverseerDecisions::RosterTrainingFor;

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

void GrantsOnIsTheOldBehaviour()
{
    RosterTraining const on = RosterTrainingFor(true);
    Check("on: weapon skills and riding are granted", on.skills);
    Check("on: class spells are granted", on.classSpells);
    Check("on: trainer spells are granted", on.trainerSpells);
    Check("on: talents are spent", on.talents);
}

void GrantsOffGrantsNothing()
{
    RosterTraining const off = RosterTrainingFor(false);
    Check("off: no weapon skills or riding", !off.skills);
    Check("off: no class spells", !off.classSpells);
    Check("off: no trainer spells without a trainer", !off.trainerSpells);
    Check("off: earned talent points are still spent", off.talents);
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
    std::size_t const end = source.find("    void SpendTalents(Player* bot, uint32 tabpage)", begin);
    if (begin == std::string::npos || end == std::string::npos)
        return "";
    return source.substr(begin, end - begin);
}

void TheAdapterHonoursTheSwitch()
{
    std::string const source = ReadFile("src/mod_overseer.cpp");
    std::string const body = TrainRosterBody(source);
    Check("TrainRoster is found in src/mod_overseer.cpp (run from the repo root)", !body.empty());
    if (body.empty())
        return;

    Check("the key is read with a default of ON",
          source.find("GetOption<bool>(\"Overseer.Train.Factory\", true)") != std::string::npos);
    Check("TrainRoster asks the decision",
          body.find("OverseerDecisions::RosterTrainingFor(RosterFactoryGrants())") != std::string::npos);
    Check("InitSkills only when skills are granted",
          body.find("if (training.skills)\n                factory.InitSkills();") != std::string::npos);
    Check("InitClassSpells only when class spells are granted",
          body.find("if (training.classSpells)\n                factory.InitClassSpells();") !=
              std::string::npos);

    std::size_t const gate = body.find("if (training.trainerSpells)");
    std::size_t const sweep = body.find("factory.InitAvailableSpells();");
    Check("InitAvailableSpells only inside the trainer-spells gate",
          gate != std::string::npos && sweep != std::string::npos && gate < sweep);

    // Every factory call in TrainRoster sits behind one of the three flags.
    std::size_t calls = 0;
    for (std::size_t at = body.find("factory.Init"); at != std::string::npos;
         at = body.find("factory.Init", at + 1))
        ++calls;
    Check("TrainRoster makes exactly three factory grants, all gated", calls == 3);

    Check("the off path says so",
          body.find("factory grants are \"\n                         \"off") != std::string::npos);

    std::string const dist = ReadFile("conf/mod_overseer.conf.dist");
    Check("the dist declares the key, commented, at its default",
          dist.find("\n#Overseer.Train.Factory = 1\n") != std::string::npos);
}

}  // namespace

int main()
{
    GrantsOnIsTheOldBehaviour();
    GrantsOffGrantsNothing();
    TheAdapterHonoursTheSwitch();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_roster_training: all passed\n");
    return 0;
}
