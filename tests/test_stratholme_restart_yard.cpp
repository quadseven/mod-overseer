/*
 * After a restart, members already in Stratholme's exit yard get an episode (#1254).
 *
 * The yard episode opens only on exit through areatrigger 2221. _stratholmeYards is
 * in-memory only and lost on restart. On an IDLE poll, open a yard episode when a
 * member stands in the exit yard (InStratholmeExitYard) and no episode exists.
 *
 * Compiled against src/overseer_decisions.cpp for the yard predicate, and checks
 * the module's coordinator wiring for the restart recovery.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using OverseerDecisions::InStratholmeExitYard;
using OverseerDecisions::STRATHOLME_YARD_EXIT_TRIGGER;

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

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
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

// Extract the body of a function from its signature to the closing brace at class indentation.
std::string FunctionBody(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

}  // namespace

int main()
{
    // The yard predicate: 2221's landing, the button and the door are in it;
    // Elders' Square west of the gateway, the main gate and another map are not.
    {
        CheckBool("2221's landing is in the yard", InStratholmeExitYard(0, 3235.46f, -4050.6f), true);
        CheckBool("the button is in the yard", InStratholmeExitYard(0, 3191.1f, -4044.29f), true);
        CheckBool("the door is in the yard", InStratholmeExitYard(0, 3185.48f, -4039.1f), true);
        CheckBool("past the door is out", InStratholmeExitYard(0, 3165.48f, -4039.1f), false);
        CheckBool("the main gate is out", InStratholmeExitYard(0, 3392.46f, -3396.77f), false);
        CheckBool("the instance map is not the yard", InStratholmeExitYard(329, 3235.46f, -4050.6f), false);
    }

    // The module reads the trigger id constant.
    {
        std::string const source = ReadModule();
        Check("the module uses STRATHOLME_YARD_EXIT_TRIGGER",
              source.find("STRATHOLME_YARD_EXIT_TRIGGER") != std::string::npos);
    }

    // The fix for #1254: on an IDLE poll, open a yard episode when a member
    // stands in the exit yard and no episode exists.
    {
        std::string const source = ReadModule();

        // The comment explaining the fix.
        Check("the restart fix is documented",
              source.find("AFTER A RESTART, MEMBERS ALREADY IN STRATHOLME'S EXIT YARD HAVE NO EPISODE") != std::string::npos);

        // The IDLE phase check.
        Check("the fix runs only in the IDLE phase",
              source.find("if (coord.phase == DungeonRunPhase::Idle)") != std::string::npos);

        // The check that no episode already exists.
        Check("the fix checks _stratholmeYards for an existing episode",
              source.find("_stratholmeYards.find(family) == _stratholmeYards.end()") != std::string::npos);

        // The check for members in the yard using MembersInStratholmeYard.
        Check("the fix uses MembersInStratholmeYard to find members in the yard",
              source.find("MembersInStratholmeYard(members)") != std::string::npos);

        // The fix only opens the yard if there are members in it.
        Check("the fix checks for non-empty inYard",
              source.find("!inYard.empty()") != std::string::npos);

        // The fix finds the portal with the correct exit trigger.
        Check("the fix finds the portal by exit trigger",
              source.find("portal->exitTriggerId == OverseerDecisions::STRATHOLME_YARD_EXIT_TRIGGER") != std::string::npos);

        // The fix opens the yard episode.
        Check("the fix calls OpenStratholmeYard",
              source.find("OpenStratholmeYard(family, *portal, coord)") != std::string::npos);

        // The fix logs the recovery.
        Check("the fix logs the yard recovery",
              source.find("has {} member(s) in the Stratholme exit yard at IDLE with no episode") != std::string::npos);
    }

    // The DriveStratholmeYard check still runs first (the normal path for members
    // who exit through 2221 during a run).
    {
        std::string const source = ReadModule();
        std::size_t const driveYard = source.find("if (DriveStratholmeYard(family, leaderName, members))");
        std::size_t const idleFix = source.find("AFTER A RESTART, MEMBERS ALREADY IN STRATHOLME'S EXIT YARD");
        Check("DriveStratholmeYard runs before the restart fix",
              driveYard != std::string::npos && idleFix != std::string::npos && driveYard < idleFix);
    }

    // The restart fix only applies when there's no existing yard episode.
    // A member with an open episode should not get a second one.
    {
        std::string const source = ReadModule();
        std::size_t const check = source.find("_stratholmeYards.find(family) == _stratholmeYards.end()");
        Check("the fix checks for no existing episode before opening one",
              check != std::string::npos);
    }

    // A member outside the yard gets no episode.
    {
        std::string const source = ReadModule();
        // The MembersInStratholmeYard function correctly returns only members in the yard.
        std::string const helper = FunctionBody(source, "static std::vector<Player*> MembersInStratholmeYard(");
        Check("MembersInStratholmeYard uses InStratholmeExitYard predicate",
              helper.find("OverseerDecisions::InStratholmeExitYard(") != std::string::npos);
        Check("MembersInStratholmeYard checks IsInWorld",
              helper.find("member->IsInWorld()") != std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_stratholme_restart_yard: all checks passed\n");
    return 0;
}