/*
 * Source contract: the quest drive's ask for the family leader is gated on the
 * leader's own job. The decision is proved in test_quest_drive_decisions.cpp;
 * this pins that the adapter asks it before AskForLeader(QuestDrive).
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream in("src/mod_overseer.cpp", std::ios::binary);
    std::string const src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string const gate = "QuestDriveHasWorkForLeader(LeaderJobOf(leaderName))";
    std::string const ask = "AskForLeader(leaderName, OverseerDecisions::LeaderIntentKind::QuestDrive";
    auto const g = src.find(gate);
    auto const a = src.find(ask);
    if (g == std::string::npos || a == std::string::npos || g > a || a - g > 400)
    {
        std::cerr << "the quest drive asks for the leader without checking his job\n";
        return EXIT_FAILURE;
    }
    std::cout << "quest drive leader ask is gated on the job\n";
    return EXIT_SUCCESS;
}
