/*
 * Source contract for a family mage's way to a capital (#1161).
 *
 * The decisions are proved in test_mage_travel.cpp. What only the adapter can
 * get wrong is whether it ASKS them and reuses the paths the issue named:
 * every cast goes through the existing `cast` row and every portal click
 * through the existing `use-portal` row, the spells and their reagents are read
 * from this world rather than a table, the row's verdict is read back before
 * the walk resumes, and neither the mage nor the traveller is walked off the
 * errand while a row still owns it.
 *
 * Pinned as text, the same way test_portal_use_source.cpp pins #410.
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

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const driveCall = text.find("DriveMageTravel(aims);");
    std::size_t const walkLoop =
        text.find("for (auto const& [name, columnAim] : aims)", driveCall);
    if (driveCall == std::string::npos || walkLoop == std::string::npos || !(driveCall < walkLoop))
        return Fail("DriveTravel must ask the mage before it walks anybody");

    std::string const drive = Body(text, "void DriveMageTravel(");
    if (drive.empty())
        return Fail("cannot find DriveMageTravel");
    for (char const* needed :
         {"OverseerDecisions::IsTravelGuardAim(columnAim)",
          "OverseerDecisions::ParsePlaceAim(columnAim", "LoadRosterFamilies()",
          "ReadMageTravelFacts(traveller, name, familyIt->second, families, aimMap",
          "OverseerDecisions::PlanMageTravel(facts)", "BeginMageTravel(name, columnAim, mage",
          "_mageTravel.erase(runIt)"})
        if (!Has(drive, needed))
        {
            std::cerr << "DriveMageTravel must use " << needed << "\n";
            return EXIT_FAILURE;
        }
    if (Has(drive, "TeleportTo(") || Has(drive, "WorldPacket") ||
        Has(drive, "HandleCastSpellOpcode"))
        return Fail("DriveMageTravel must reuse the cast and use-portal rows, not teleport or cast "
                    "by hand");

    std::string const facts = Body(text, "static bool ReadMageTravelFacts(");
    if (facts.empty())
        return Fail("cannot find ReadMageTravelFacts");
    for (char const* needed :
         {"FamilyMage(traveller, family, families)",
          "FamilyStandingWith(mage, family, families, name",
          "ReadMageTravelSpells(mage, aimMap, aimX, aimY, aimZ, spells)",
          "traveller->GetGroup() != mage->GetGroup()",
          "facts.travellerIsMage = travellerIsMage", "facts.travellerGrouped",
          "facts.travellerLeadsGroup", "facts.travellerWithMage", "facts.partyWithMage = party",
          "facts.spells = spells",
          "facts.walkYards", "facts.destinationMap = aimMap"})
        if (!Has(facts, needed))
        {
            std::cerr << "ReadMageTravelFacts must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::string const begin = Body(text, "void BeginMageTravel(");
    if (begin.empty())
        return Fail("cannot find BeginMageTravel");
    for (char const* needed :
         {"OverseerDecisions::MageTravelLine(facts, plan)", "run.refused = true",
          "run.refusedAt = now", "QueueMageTravelCast("})
        if (!Has(begin, needed))
        {
            std::cerr << "BeginMageTravel must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::string const spells = Body(text, "static bool ReadMageTravelSpells(");
    if (spells.empty())
        return Fail("cannot find ReadMageTravelSpells");
    for (char const* needed :
         {"mage->GetSpellMap()", "sSpellMgr->GetSpellInfo(spellId)", "info->Reagent[i]",
          "mage->GetItemCount(spell.reagentEntry, false)", "PortalDestination(spellId",
          "OverseerDecisions::MAGE_CAPITAL_ARRIVE_YARDS"})
        if (!Has(spells, needed))
        {
            std::cerr << "ReadMageTravelSpells must use " << needed << "\n";
            return EXIT_FAILURE;
        }
    std::string const target = Body(text, "static bool PortalDestination(");
    if (!Has(target, "sSpellMgr->GetSpellTargetPosition(") || !Has(target, "to->target_X") ||
        !Has(target, "to->target_Y") || !Has(target, "to->target_Z"))
        return Fail("a teleport's destination must be the spell's own target position");
    std::string const portalTarget = Body(text, "static bool MagePortalTarget(");
    if (!Has(portalTarget, "SPELL_EFFECT_TRANS_DOOR") ||
        !Has(portalTarget, "GAMEOBJECT_TYPE_SPELLCASTER") ||
        !Has(portalTarget, "object->spellcaster.spellId"))
        return Fail("a portal's destination must be the object's own spellcaster spell");
    for (char const* id : {"3561", "3567", "10059", "11417"})
        if (Has(spells, id) || Has(drive, id))
            return Fail("the adapter must not remember a spell id; the world's data names it");

    std::string const castRow = Body(text, "static void QueueMageTravelCast(");
    if (!Has(castRow, "'cast'") || !Has(castRow, "std::to_string(spellId)"))
        return Fail("a mage teleport must be the existing kind='cast' row");
    std::string const clickRow = Body(text, "static void QueueMageTravelClick(");
    if (!Has(clickRow, "'use-portal'") || !Has(clickRow, "'quest'"))
        return Fail("a portal must be clicked by the existing kind='quest' use-portal row");

    std::string const finished = Body(text, "static bool MageTravelRowFinished(");
    if (!Has(finished, "\"applied\"") || !Has(finished, "\"unchanged\"") ||
        !Has(finished, "\"error\"") || Has(finished, "\"delivered\""))
        return Fail("the verdict is applied, unchanged or error - never delivery");

    std::string const poll = Body(text, "bool PollMageTravelRun(");
    if (poll.empty())
        return Fail("cannot find PollMageTravelRun");
    for (char const* needed :
         {"ReadMageTravelRow(source, status, detail)", "MageTravelRowFinished(status)",
          "status != \"applied\"", "EndMageTravel(run)", "QueueMageTravelClick(traveller",
          "crosses first"})
        if (!Has(poll, needed))
        {
            std::cerr << "PollMageTravelRun must use " << needed << "\n";
            return EXIT_FAILURE;
        }
    if (Has(poll, "delivered"))
        return Fail("the mage drive must never read a row's delivery as a result");

    std::string const hold = Body(text, "bool MageTravelHolds(");
    if (!Has(hold, "holds") || !Has(hold, "aim"))
        return Fail("MageTravelHolds must hold only the run's own aim");

    std::string const familyMage = Body(text, "static Player* FamilyMage(");
    if (!Has(familyMage, "CLASS_MAGE"))
        return Fail("the family mage is found by class, not by name");
    std::string const standing = Body(text, "static unsigned FamilyStandingWith(");
    for (char const* needed :
         {"OverseerDecisions::MAGE_PORTAL_GATHER_YARDS", "IsInWorld()", "GetMapId()"})
        if (!Has(standing, needed))
        {
            std::cerr << "FamilyStandingWith must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::size_t const travelState = text.find("_travelAims.StateFor(name)");
    std::size_t const mageHold = text.find("MageTravelHolds(name, columnAim)");
    if (mageHold == std::string::npos || travelState == std::string::npos ||
        !(mageHold < travelState))
        return Fail("DriveTravel must leave a mage's cast or portal click alone before it takes new "
                    "state");

    std::size_t const driveCalls = text.find("DriveMageTravel(aims);");
    std::size_t const bindAim = text.find("HomeBindAim(name)");
    if (driveCalls == std::string::npos || bindAim == std::string::npos)
        return Fail("cannot find the mage drive and the bind aim");

    return EXIT_SUCCESS;
}
