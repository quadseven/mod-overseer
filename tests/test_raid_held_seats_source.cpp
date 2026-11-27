#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::size_t const run = text.find("    void DriveRaidRun(");
    std::size_t const step = text.find("StepRaidRun(run.phase, facts)", run);
    if (run == std::string::npos || step == std::string::npos)
    {
        std::cerr << "FAIL cannot find the raid census and decision\n";
        return EXIT_FAILURE;
    }

    std::string const census = text.substr(run, step - run);
    if (census.find("++facts.awayOnOtherMap;\n"
                    "                    else if (member != head && member->IsAlive() &&\n"
                    "                             HeldWaitingForLeader(member->GetName()))\n"
                    "                        ++facts.heldTooFar;") == std::string::npos)
    {
        std::cerr << "FAIL the raid must count active far holds on living followers "
                     "outside staging on the door map\n";
        return EXIT_FAILURE;
    }
    if (census.find("facts.stoneSummonPending = !run.stoneRowSource.empty();") ==
        std::string::npos)
    {
        std::cerr << "FAIL the raid must read in-flight summons before deciding whether to wait\n";
        return EXIT_FAILURE;
    }
    std::cout << "the raid census reads held seats and pending summons\n";
    return EXIT_SUCCESS;
}
