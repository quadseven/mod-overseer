/* Source contract for the off-mesh leader hold (mod-overseer#1698): the travel
 * drive asks StallRecoveryHoldsErrand before it asks for the leader, and the
 * recovery reading asks for the quick rule. */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return std::cerr << "cannot read src/mod_overseer.cpp\n", EXIT_FAILURE;
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    auto const hold = text.find("OverseerDecisions::StallRecoveryHoldsErrand(");
    auto const ask = text.find("if (!AskForLeader(name, kind, owner, about,");
    if (hold == std::string::npos || ask == std::string::npos || hold > ask)
        return std::cerr << "the hold is not asked before the leader claim\n", EXIT_FAILURE;
    if (text.find("facts.stillRule = true;") == std::string::npos)
        return std::cerr << "the recovery reading does not ask for the quick rule\n", EXIT_FAILURE;
    // The hold block asks alive and not-in-combat before it reads footing,
    // as RecoverIdleOffMesh does (mod-overseer#1705).
    std::regex const guard(
        R"(\bif\s*\(\s*OverseerDecisions::StallRecoveryMayRead\(\s*bot->IsAlive\(\)\s*,)"
        R"(\s*bot->IsInCombat\(\)\s*\)\s*&&\s*OverseerDecisions::StallRecoveryIdleDue\()"
        R"([^;{}]*,\s*STALL_IDLE_READ_SECONDS\s*\)\s*\)\s*(?:\{\s*)?$)");
    std::regex const readCall(R"(\bReadStalledFooting\s*\()");
    auto const holdStart = text.cbegin() + hold;
    auto const holdEnd = text.cbegin() + ask;
    std::smatch read;
    // AI-REVIEW 2026-11-27 [nemotron-3.5-super-vl] missing-test-coverage (Elder, PR #1729):
    // A second footing read could bypass the alive and combat guard.
    if (!std::regex_search(holdStart, holdEnd, read, readCall) ||
        std::regex_search(read[0].second, holdEnd, readCall))
        return std::cerr << "the hold block needs exactly one footing read\n", EXIT_FAILURE;
    if (!std::regex_search(holdStart, read[0].first, guard))
        return std::cerr << "the hold block does not check alive and combat before reading\n",
               EXIT_FAILURE;
    auto const idle = text.find("void RecoverIdleOffMesh(");
    auto const idleEnd = text.find("ReadStalledFooting(", idle);
    auto const idleGuard = text.find("!bot->IsAlive() || bot->IsInCombat()", idle);
    if (idle == std::string::npos || idleGuard == std::string::npos || idleGuard > idleEnd)
        return std::cerr << "RecoverIdleOffMesh lost its alive and combat guard\n", EXIT_FAILURE;
    return EXIT_SUCCESS;
}
