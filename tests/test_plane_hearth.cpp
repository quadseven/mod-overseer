/*
 * A character the plane rung gives up on uses its own hearthstone, on its own
 * map only.
 *
 * On 2026-10-05, after a worldserver restart, four of one family's five
 * logged in on the placeholder plane under Stormwind (map 0 around
 * (-8044, 170, 59.5)). A restart forgets the last ground each one stood on, so
 * the plane rung had nothing to return them to and logged its give-up for
 * ever. These cases pin the way off that keeps #188's guarantee: the
 * hearthstone, cast as a player would, only when its homebind is on the map
 * the character stands on, once per forget window, and a give-up that names
 * which of the refusals it was.
 *
 * The last case reads the adapter's source, so the decision cannot exist
 * without being wired. Compiled against src/overseer_decisions.cpp only.
 */

#include "overseer_decisions.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using OverseerDecisions::DecidePlaneHearth;
using OverseerDecisions::PlaneHearthAction;
using OverseerDecisions::PlaneHearthFacts;
using OverseerDecisions::PlaneHearthVerdict;

namespace
{

int failures = 0;

char const* Name(PlaneHearthAction a)
{
    switch (a)
    {
        case PlaneHearthAction::NotOnPlane: return "NotOnPlane";
        case PlaneHearthAction::Hearth:     return "Hearth";
        case PlaneHearthAction::GiveUp:     return "GiveUp";
    }
    return "?";
}

void Expect(char const* what, PlaneHearthVerdict const& v, PlaneHearthAction action,
            char const* reasonHas)
{
    bool const ok = v.action == action && v.reason.find(reasonHas) != std::string::npos;
    if (!ok)
    {
        ++failures;
        std::cerr << "FAIL " << what << ": got " << Name(v.action) << " '" << v.reason
                  << "', wanted " << Name(action) << " with '" << reasonHas << "'\n";
    }
    else
        std::cout << "ok   " << what << "\n";
}

// The 2026-10-05 reading: on the plane under Stormwind, nothing on record,
// stone in the bag and ready, bound in the same city.
PlaneHearthFacts Stuck()
{
    PlaneHearthFacts f;
    f.onPlane = true;
    f.groundOnRecord = false;
    f.hasStone = true;
    f.stoneReady = true;
    f.bindMap = 0;
    f.currentMap = 0;
    f.alreadyHearthedThisWindow = false;
    return f;
}

bool AdapterWiresIt()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::cerr << "FAIL cannot read src/mod_overseer.cpp\n";
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const text = buf.str();
    std::size_t const helper = text.find("OverseerDecisions::DecidePlaneHearth(");
    if (helper == std::string::npos)
    {
        std::cerr << "FAIL the adapter never asks DecidePlaneHearth\n";
        return false;
    }
    char const* const needles[] = {
        "facts.bindMap = bot->m_homebindMapId",
        "facts.currentMap = bot->GetMapId()",
        "DoHearth(bot, \"use\", status, evidence, _pendingHearths, 0)",
    };
    for (char const* n : needles)
        if (text.find(n, helper > 4000 ? helper - 4000 : 0) == std::string::npos)
        {
            std::cerr << "FAIL the plane hearth helper is missing: " << n << "\n";
            return false;
        }
    std::size_t const giveUp = text.find("IS STILL UNDER THE WORLD at map");
    std::size_t const asked = text.rfind("HearthOffThePlane(", giveUp);
    if (giveUp == std::string::npos || asked == std::string::npos ||
        giveUp - asked > 3000)
    {
        std::cerr << "FAIL the plane give-up must try HearthOffThePlane first\n";
        return false;
    }
    return true;
}

}  // namespace

int main()
{
    Expect("restart: no ground on record, stone ready, bound here -> hearth",
           DecidePlaneHearth(Stuck()), PlaneHearthAction::Hearth, "no ground on record");

    PlaneHearthFacts returned = Stuck();
    returned.groundOnRecord = true;
    Expect("returns did not hold, stone ready, bound here -> hearth",
           DecidePlaneHearth(returned), PlaneHearthAction::Hearth, "did not hold");

    PlaneHearthFacts crossMap = Stuck();
    crossMap.bindMap = 1;
    Expect("bound on another continent -> give up, never cross a map (#188)",
           DecidePlaneHearth(crossMap), PlaneHearthAction::GiveUp, "bound on map 1");

    PlaneHearthFacts noStone = Stuck();
    noStone.hasStone = false;
    noStone.stoneReady = false;
    Expect("no stone -> give up and say so", DecidePlaneHearth(noStone),
           PlaneHearthAction::GiveUp, "carries no hearthstone");

    PlaneHearthFacts cooling = Stuck();
    cooling.stoneReady = false;
    Expect("stone on cooldown -> give up and say so", DecidePlaneHearth(cooling),
           PlaneHearthAction::GiveUp, "on cooldown");

    PlaneHearthFacts again = Stuck();
    again.alreadyHearthedThisWindow = true;
    Expect("already hearthed this window -> give up, once per window",
           DecidePlaneHearth(again), PlaneHearthAction::GiveUp, "already used");

    PlaneHearthFacts ground = Stuck();
    ground.onPlane = false;
    Expect("not on the plane -> this rule says nothing", DecidePlaneHearth(ground),
           PlaneHearthAction::NotOnPlane, "not on the hidden plane");

    // The cross-map refusal outranks a ready stone, and a missing stone
    // outranks a cross-map bind, so the reason is the first thing to fix.
    PlaneHearthFacts both = Stuck();
    both.hasStone = false;
    both.bindMap = 1;
    Expect("no stone and bound elsewhere -> names the missing stone",
           DecidePlaneHearth(both), PlaneHearthAction::GiveUp, "carries no hearthstone");

    if (AdapterWiresIt())
        std::cout << "ok   adapter asks the decision at the plane give-up\n";
    else
        ++failures;

    if (failures)
    {
        std::cerr << failures << " plane hearth case(s) failed\n";
        return 1;
    }
    std::cout << "plane hearth: all cases pass\n";
    return 0;
}
