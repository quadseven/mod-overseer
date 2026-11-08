/*
 * Source contract for the bounded walk-out over a lost upgrade (bridge #2062).
 *
 * The rule is proved in test_run_recovery.cpp. What only the adapter can get
 * wrong is whether it asks it: a bag check that hands the raw
 * UpgradeStrandedOnTheFloor answer straight to DungeonRunBagAnswer walks an
 * 87 minute Blackrock Depths run out over one Black Diamond, and the next run
 * opens a fresh instance.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const answer = text.find("OverseerDecisions::DungeonRunBagAnswer(");
    if (answer == std::string::npos)
        return Fail("cannot find the bag answer");
    std::size_t const answerEnd = text.find("case OverseerDecisions::DungeonBagPressure::MakeRoom", answer);
    if (answerEnd == std::string::npos)
        return Fail("cannot find the end of the bag answer");
    std::string const call = text.substr(answer, answerEnd - answer);
    if (call.find("UpgradeStrandBoundReached(") == std::string::npos)
        return Fail("the bag answer must be handed the bounded strand, not the raw one");
    if (call.find("UpgradeStrandedOnTheFloor(") != std::string::npos)
        return Fail("the raw strand must not reach DungeonRunBagAnswer directly");

    std::size_t const before = text.rfind("UpgradeStrandedSinceAfterPoll(", answer);
    if (before == std::string::npos || answer - before > 1500)
        return Fail("the strand clock must be advanced in the same poll, before the answer");

    if (text.find("std::time_t upgradeStrandedSince{0};") == std::string::npos)
        return Fail("the coordinator must carry the strand clock");

    std::cout << "bag upgrade strand source: all checks passed\n";
    return EXIT_SUCCESS;
}
