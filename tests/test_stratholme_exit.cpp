/*
 * A Stratholme run's way out of the exit yard (#645).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::DecideStratholmeExit;
using OverseerDecisions::StratholmeExitFacts;
using OverseerDecisions::StratholmeExitWay;
using OverseerDecisions::StratholmeExitWayWord;

namespace
{

int failures = 0;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckWay(char const* what, StratholmeExitWay got, StratholmeExitWay want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, StratholmeExitWayWord(got),
                StratholmeExitWayWord(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

std::string ReadModule()
{
    std::string const path = std::getenv("OVERSEER_SOURCE") ? std::getenv("OVERSEER_SOURCE")
                                                             : "src/mod_overseer.cpp";
    std::ifstream source(path);
    if (!source)
    {
        std::printf("FAIL ReadModule: could not open %s\n", path.c_str());
        ++failures;
        return std::string();
    }
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

}  // namespace

int main()
{
    // door open means walk
    {
        StratholmeExitFacts facts;
        facts.doorOpen = true;
        CheckWay("door open means walk", DecideStratholmeExit(facts), StratholmeExitWay::Walk);
    }

    // door closed with the button in reach means button then walk
    {
        StratholmeExitFacts facts;
        facts.doorOpen = false;
        facts.doorClosed = true;
        facts.buttonInReach = true;
        CheckWay("door closed, button in reach means button",
                 DecideStratholmeExit(facts), StratholmeExitWay::Button);
    }

    // button out of reach means hearth
    {
        StratholmeExitFacts facts;
        facts.doorOpen = false;
        facts.doorClosed = true;
        facts.buttonInReach = false;
        CheckWay("button out of reach means hearth", DecideStratholmeExit(facts),
                 StratholmeExitWay::Hearth);
    }

    // the decision never orders a teleport other than the member's own hearthstone:
    // its only outcomes are walk, button (then walk) and hearth.
    {
        StratholmeExitFacts openF, closedF, farF;
        openF.doorOpen = true;
        closedF.doorClosed = true;
        closedF.buttonInReach = true;
        farF.doorClosed = true;
        farF.buttonInReach = false;
        StratholmeExitWay ways[3] = {DecideStratholmeExit(openF), DecideStratholmeExit(closedF),
                                     DecideStratholmeExit(farF)};
        for (int i = 0; i < 3; ++i)
        {
            CheckBool("no outcome is a teleport",
                      ways[i] == StratholmeExitWay::Walk || ways[i] == StratholmeExitWay::Button ||
                          ways[i] == StratholmeExitWay::Hearth,
                      true);
        }
        CheckText("hearth is the only teleporting outcome's word",
                  StratholmeExitWayWord(StratholmeExitWay::Hearth), "hearth");
    }

    // both Stratholme rows stay withheld
    {
        std::string const source = ReadModule();
        CheckBool("stratholme-live is still withheld",
                  source.find("\"stratholme-live\"") != std::string::npos &&
                      source.find("lands in a yard the navmesh closes") != std::string::npos,
                  true);
        CheckBool("stratholme-undead is still withheld",
                  source.find("\"stratholme-undead\"") != std::string::npos, true);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_stratholme_exit: all checks passed\n");
    return 0;
}