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
using OverseerDecisions::InStratholmeExitYard;
using OverseerDecisions::StratholmeButtonInReach;
using OverseerDecisions::StratholmeHearthLeavesTheYard;
using OverseerDecisions::StratholmeYardMemberMayWalk;
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
    CheckBool("leader in yard may walk", StratholmeYardMemberMayWalk(true, true, true), true);
    CheckBool("follower in yard does not walk with leader present",
              StratholmeYardMemberMayWalk(true, true, false), false);
    CheckBool("follower does not walk when leader is outside",
              StratholmeYardMemberMayWalk(false, true, false), false);
    CheckBool("follower does not walk when leader is dead",
              StratholmeYardMemberMayWalk(true, false, false), false);
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
        facts.buttonInReach = true;
        CheckWay("door closed, button in reach means button",
                 DecideStratholmeExit(facts), StratholmeExitWay::Button);
    }

    // button out of reach means hearth
    {
        StratholmeExitFacts facts;
        facts.doorOpen = false;
        facts.buttonInReach = false;
        facts.hearthAvailable = true;
        CheckWay("button out of reach means hearth", DecideStratholmeExit(facts),
                 StratholmeExitWay::Hearth);
    }

    // the decision never orders a teleport other than the member's own hearthstone:
    // its only outcomes are walk, button (then walk) and hearth.
    {
        StratholmeExitFacts openF, closedF, farF;
        openF.doorOpen = true;
        closedF.buttonInReach = true;
        farF.buttonInReach = false;
        farF.hearthAvailable = true;
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

    // no door, no button, no hearth: nothing to order
    {
        StratholmeExitFacts facts;
        CheckWay("no way out is none", DecideStratholmeExit(facts), StratholmeExitWay::None);
    }

    // the yard: 2221's landing, the button and the door are in it; Elders'
    // Square west of the gateway, the main gate and another map are not
    {
        CheckBool("2221's landing is in the yard", InStratholmeExitYard(0, 3235.46f, -4050.6f),
                  true);
        CheckBool("the button is in the yard", InStratholmeExitYard(0, 3191.1f, -4044.29f), true);
        CheckBool("the door is in the yard", InStratholmeExitYard(0, 3185.48f, -4039.1f), true);
        CheckBool("past the door is out", InStratholmeExitYard(0, 3165.48f, -4039.1f), false);
        CheckBool("the main gate is out", InStratholmeExitYard(0, 3392.46f, -3396.77f), false);
        CheckBool("the instance map is not the yard", InStratholmeExitYard(329, 3235.46f, -4050.6f),
                  false);
    }

    // the button stops being in reach once pressed out or walked too long
    {
        CheckBool("spawned, unpressed, fresh walk is in reach", StratholmeButtonInReach(true, 0, 0),
                  true);
        CheckBool("not spawned is out of reach", StratholmeButtonInReach(false, 0, 0), false);
        CheckBool("pressed out is out of reach",
                  StratholmeButtonInReach(true, OverseerDecisions::STRATHOLME_BUTTON_PRESSES, 0),
                  false);
        CheckBool("walked too long is out of reach",
                  StratholmeButtonInReach(true, 0, OverseerDecisions::STRATHOLME_BUTTON_WALK_SECONDS),
                  false);
    }

    // a hearth counts only when the stone is ready and the bind is not the yard
    {
        CheckBool("ready stone bound in a town leaves",
                  StratholmeHearthLeavesTheYard(true, 0, -8833.38f, 628.62f), true);
        CheckBool("stone on cooldown does not leave",
                  StratholmeHearthLeavesTheYard(false, 0, -8833.38f, 628.62f), false);
        CheckBool("bound in the yard does not leave",
                  StratholmeHearthLeavesTheYard(true, 0, 3235.46f, -4050.6f), false);
    }

    // the module reads the door, asks the decision, says which way ran, and
    // opens the way out where a run leaves by 2221
    {
        std::string const source = ReadModule();
        CheckBool("the module asks DecideStratholmeExit",
                  source.find("OverseerDecisions::DecideStratholmeExit(") != std::string::npos, true);
        CheckBool("the module logs the way out",
                  source.find("\"overseer: stratholme exit: door {}, way out {}") !=
                      std::string::npos,
                  true);
        CheckBool("the module reads the Service Entrance's state",
                  source.find("STRATHOLME_SERVICE_ENTRANCE_ENTRY") != std::string::npos &&
                      source.find("GetGoState() == GO_STATE_ACTIVE") != std::string::npos,
                  true);
        CheckBool("the module uses the button the way a client does",
                  source.find("DriveGameObjectUse(walker->GetSession(), button->GetGUID())") !=
                      std::string::npos,
                  true);
        CheckBool("the hearth is the member's own, through DriveExitHearths",
                  source.find("DriveExitHearths(family, leaderName, hearthers, yard.hearth,") !=
                      std::string::npos,
                  true);
        CheckBool("an EXIT through 2221 opens the yard episode",
                  source.find("OpenStratholmeYard(family, *portal, coord)") != std::string::npos, true);
        std::size_t const yardStart = source.find("void OpenStratholmeYard(");
        std::size_t const yardEnd = source.find("static std::vector<Player*> MembersInStratholmeYard", yardStart);
        std::string const yardSource = yardStart == std::string::npos || yardEnd == std::string::npos
            ? std::string() : source.substr(yardStart, yardEnd - yardStart);
        CheckBool("yard hearth names the Stratholme map constant",
                  yardSource.find("yard.hearth.mapId = OverseerDecisions::STRATHOLME_EXIT_YARD_MAP_ID") !=
                      std::string::npos,
                  true);
        std::size_t const hearthStart = source.find("bool DriveExitHearths(");
        std::size_t const hearthEnd = source.find("// ---- a Stratholme run's way out", hearthStart);
        std::string const hearthSource = hearthStart == std::string::npos || hearthEnd == std::string::npos
            ? std::string() : source.substr(hearthStart, hearthEnd - hearthStart);
        CheckBool("hearth reason is supplied by caller",
                  hearthSource.find("char const* reason") != std::string::npos &&
                      hearthSource.find("because the walk to the exit door failed") == std::string::npos &&
                      hearthSource.find("after EXIT could not walk the party out") == std::string::npos,
                  true);
        std::size_t const yardCall = source.find(
            "DriveExitHearths(family, leaderName, hearthers, yard.hearth,");
        std::string const yardCallSource = yardCall == std::string::npos
            ? std::string() : source.substr(yardCall, source.find(';', yardCall) - yardCall);
        CheckBool("yard hearth names its own reason",
                  yardCallSource.find("leaving the Stratholme exit yard") != std::string::npos &&
                      yardCallSource.find("exit door") == std::string::npos &&
                      yardCallSource.find("EXIT could not walk") == std::string::npos,
                  true);
        CheckBool("exit-door hearth retains its original reason",
                  source.find("members, episode, \"EXIT\",\n") !=
                      std::string::npos &&
                      source.find("\"the walk to the exit door failed\"") != std::string::npos,
                  true);
        CheckBool("the coordinator drives the yard before its phases",
                  source.find("if (DriveStratholmeYard(family, leaderName, members))") !=
                      std::string::npos,
                  true);
        CheckBool("yard walker must be the living leader",
                  source.find("StratholmeYardMemberMayWalk(") != std::string::npos &&
                      source.find("Player* walker = inYard.front()") == std::string::npos,
                  true);
        CheckBool("missing leader holds or hearths the yard",
                  source.find("if (!walker)") != std::string::npos &&
                      source.find("if (!hearthers.empty())") != std::string::npos &&
                      source.find("return true;\n        }\n        GameObject* const door") !=
                          std::string::npos,
                  true);
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
