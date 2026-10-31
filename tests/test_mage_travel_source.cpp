/*
 * Source contract for the mage's way to a capital (#1161).
 *
 * The choice is proved in test_mage_travel.cpp. What only the adapter can get
 * wrong is whether it is asked: that the travel drive offers the errand to it
 * before anything refuses a point on another map, that the ids come from the
 * character and the world and not from this file, that the cast goes through
 * the existing verb, and that the stranded drive leaves the mage at its portal.
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

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find(to, start + 1);
    if (end == std::string::npos)
        return std::string();
    return text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (source.empty())
        return Fail("src/mod_overseer.cpp could not be read");

    std::string const section = Between(source, "MAGE TRAVEL SECTION BEGINS", "MAGE TRAVEL SECTION ENDS");
    if (section.empty())
        return Fail("the mage travel section is missing");

    // The ids are read, never written down.
    if (section.find("GetSpellMap()") == std::string::npos)
        return Fail("the spells must come from the character's own spell list");
    if (section.find("SPELLFAMILY_MAGE") == std::string::npos)
        return Fail("only a mage's spells are read as a way to a capital");
    if (section.find("MageTravelSpellsFrom(") == std::string::npos)
        return Fail("the spells must go through the tested reader");
    if (section.find("SpellLanding(") == std::string::npos)
        return Fail("the landing must come from spell_target_position");
    if (section.find("spellcaster.spellId") == std::string::npos)
        return Fail("a portal's landing is the landing of the spell its object casts");
    for (char const* id : {"3561", "3562", "3563", "3566", "3567", "10059", "11416", "17032"})
        if (section.find(id) != std::string::npos)
            return Fail("a spell or item id is written into the mage travel section");

    // The module's own cast path, and the click path, each with no row behind.
    if (section.find("DoCast(") == std::string::npos ||
        section.find("_pendingCasts, 0)") == std::string::npos)
        return Fail("the cast must go through the cast verb");
    if (section.find("BeginPortalUse(") == std::string::npos)
        return Fail("the party must go through the portal by the existing click");

    // Named lines, one for the cast and one for the refusal.
    if (section.find("overseer: MAGE TRAVEL CAST") == std::string::npos ||
        section.find("overseer: MAGE TRAVEL REFUSED") == std::string::npos ||
        section.find("overseer: MAGE TRAVEL RESULT") == std::string::npos)
        return Fail("each cast, refusal and result needs its own named line");

    // The result is read from the world.
    if (section.find("FindObjectThisCastMade(") == std::string::npos)
        return Fail("a portal is believed only when the object stands there");

    // Nobody is steered who is not this module's to steer.
    if (section.find("IsRealPlayer(") == std::string::npos ||
        section.find("IsTravelGuardAim(") == std::string::npos ||
        section.find("_dungeonEscorts.count(name)") == std::string::npos)
        return Fail("a person's character, a guard and a run's member are not steered");
    if (section.find("PortalUseStateWall(member)") == std::string::npos)
        return Fail("a click must ask the walls the use-portal row asks");
    if (section.find("MageTravelCalledOff(") == std::string::npos)
        return Fail("an errand called off must end the run");
    if (Between(source, "char const* DoPortalUse(", "LEADER FIRST").find("PortalUseStateWall(who)") ==
        std::string::npos)
        return Fail("the row and the mage's run must share one list of walls");

    // The drive asks, and asks before the resolve can refuse a far map.
    std::string const drive = Between(source, "void DriveTravel()", "ARRIVAL IS THE PRIMARY RELEASE");
    std::size_t const asked = drive.find("MageTravelHolds(");
    std::size_t const resolved = drive.find("!ResolveTravelTarget(bot, target");
    if (asked == std::string::npos || resolved == std::string::npos || asked > resolved)
        return Fail("DriveTravel must offer the errand before resolving it on this map");
    if (drive.find("StepMageTravels(aims)") == std::string::npos ||
        drive.find("StepMageTravels(aims)") > drive.find("if (aims.empty())"))
        return Fail("the steps must run even when no errand is left in the column");

    // Nobody hands the mage to its own portal while the party still needs it.
    std::string const stranded =
        Between(source, "case OverseerDecisions::StrandedWay::Portal:", "case OverseerDecisions::StrandedWay::Sail:");
    if (stranded.find("MageHoldsThePortal(name)") == std::string::npos)
        return Fail("the stranded drive must not take the mage through its open portal");

    return EXIT_SUCCESS;
}
