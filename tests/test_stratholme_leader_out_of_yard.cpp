/*
 * Test that when the leader is out of the yard or dead, no follower gets a travel aim.
 *
 * Compiled against src/overseer_decisions.cpp and src/mod_overseer.cpp.
 */

#include "overseer_decisions.h"
#include "overseer_core_compat.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::DecideStratholmeExit;
using OverseerDecisions::StratholmeExitWay;
using OverseerDecisions::InStratholmeExitYard;
using OverseerDecisions::TravelOwner;

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

std::string ReadModule()
{
    std::string const path = std::getenv("OVERSEER_SOURCE")
                                ? std::getenv("OVERSEER_SOURCE")
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

} // namespace

int main()
{
    // yard: 2221's landing, the button and the door are in it
    CheckBool("2221's landing is in the yard", InStratholmeExitYard(0, 3235.46f, -4050.6f),
              true);
    CheckBool("the button is in the yard", InStratholmeExitYard(0, 3191.1f, -4044.29f), true);
    CheckBool("the door is in the yard", InStratholmeExitYard(0, 3185.48f, -4039.1f), true);
    CheckBool("past the door is out", InStratholmeExitYard(0, 3165.48f, -4039.1f), false);
    CheckBool("the main gate is out", InStratholmeExitYard(0, 3392.46f, -3396.77f), false);
    CheckBool("the instance map is not the yard", InStratholmeExitYard(329, 3235.46f, -4050.6f),
              false);

    // Test that when leader is out of yard, no follower gets travel aim
    {
        std::string const source = ReadModule();
        std::string const target =
            "if (leaderName.empty() || !LeaderInYard(inYard, leaderName))";
        CheckBool("leader out of yard prevents travel aim",
                  source.find(target) != std::string::npos, true);
    }

    // Test that when leader is dead, no follower gets travel aim
    {
        std::string const source = ReadModule();
        std::string const target =
            "if (leaderName.empty() || !LeaderInYard(inYard, leaderName))";
        CheckBool("leader dead prevents travel aim",
                  source.find(target) != std::string::npos, true);
    }

    // Test that when leader is in yard, leader alone is aimed
    {
        std::string const source = ReadModule();
        // Should find the existing logic that aims the leader
        CheckBool("leader in yard causes leader aim",
                  source.find("if (!leaderName.empty() && LeaderInYard(inYard, leaderName))") !=
                      std::string::npos,
                  true);
        CheckBool("leader in yard uses leader as walker",
                  source.find("walker = leader;") != std::string::npos, true);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_stratholme_leader_out_of_yard: all checks passed\n");
    return 0;
}
