/*
 * A family bound wholly on the other continent is given an inn on the door's
 * continent, and the EXIT hearth never sends a member across the ocean
 * (mod-overseer#1388).
 *
 * WHAT THIS IS ABOUT. Read out of `character_homebind` on the dev realm,
 * 2026-11-07. The Horde family running Uldaman (door on map 0) was bound
 * wholly on map 1 (Orgrimmar zone 1637 and Ratchet zone 392); the Alliance
 * family running Maraudon (door on map 1) wholly on map 0 (Stormwind zone 1519
 * and Elwynn zone 12). FamilyInnOnDoorMap found no inn on the door's
 * continent for either, so nobody was walked to one, and the EXIT let-through
 * hearthed members home across the ocean after every failed walk out: 17
 * `went home: map 70 to map 1` and 6 `went home: map 349 to map 0` in 48 hours.
 * The fixtures are those rows.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_door_inns.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::CampaignHome;
using OverseerDecisions::CampaignHomeAnchor;
using OverseerDecisions::CampaignHomeNeedsRebinding;
using OverseerDecisions::ExitFailureHearthStep;
using OverseerDecisions::ExitHearthFacts;
using OverseerDecisions::ExitHearthImpossibleReason;
using OverseerDecisions::ExitHearthStep;
using OverseerDecisions::FactionInnForDoor;
using OverseerDecisions::FamilyInnOnDoorMap;
using OverseerDecisions::HomeBind;
using OverseerDecisions::OFF_CONTINENT_HEARTH_AFTER_SECONDS;
using OverseerDecisions::OffContinentHearthLetThrough;
using OverseerDecisions::ReadCampaignHome;
using OverseerDecisions::UnnamedDoorHomeAnchor;

namespace
{

int failures = 0;

constexpr std::uint32_t ULDAMAN_MAP = 70;
constexpr std::uint32_t MARAUDON_MAP = 349;
constexpr float TOWN = 150.f;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

HomeBind Bind(std::uint32_t map, float x, float y, float z)
{
    HomeBind b;
    b.known = true;
    b.mapId = map;
    b.x = x;
    b.y = y;
    b.z = z;
    return b;
}

// The Horde family, as the dev realm had it bound: all five on map 1.
std::vector<HomeBind> TheHordeFamily()
{
    return {
        Bind(1, 1630.48f, -4436.13f, 15.6f),   // Orgrimmar
        Bind(1, 1634.0f, -4439.4f, 15.76f),    // Orgrimmar
        Bind(1, -1050.0f, -3664.8f, 23.9f),    // Ratchet
        Bind(1, 1630.48f, -4437.37f, 15.6f),   // Orgrimmar
        Bind(1, -1050.0f, -3664.8f, 23.9f),    // Ratchet
    };
}

// The Alliance family: all five on map 0.
std::vector<HomeBind> TheAllianceFamily()
{
    return {
        Bind(0, -8867.8f, 673.7f, 97.9f),       // Stormwind
        Bind(0, -8867.8f, 673.7f, 97.9f),       // Stormwind
        Bind(0, -8865.41f, 670.257f, 97.9f),    // Stormwind
        Bind(0, -8866.32f, 671.174f, 97.9f),    // Stormwind
        Bind(0, -8949.95f, -132.493f, 83.5f),   // Goldshire
    };
}

void TheHordeFamilyAtUldamanIsGivenKargath()
{
    std::vector<HomeBind> const family = TheHordeFamily();
    Check("the family itself has no inn on map 0",
          !FamilyInnOnDoorMap(family, 0, TOWN).known);

    CampaignHomeAnchor const anchor = UnnamedDoorHomeAnchor(family, ULDAMAN_MAP, 0, true, TOWN);
    Check("the Uldaman door names the Horde an inn", anchor.known);
    Check("on the door's continent", anchor.mapId == 0);
    Check("at Kargath, Innkeeper Shul'kar's spawn",
          anchor.x == -6650.01f && anchor.y == -2149.09f);
    Check("judged by continent only", anchor.mapOnly);

    for (HomeBind const& home : family)
    {
        CampaignHome const verdict = ReadCampaignHome(home, anchor, TOWN);
        Check("every member is off the door's map", verdict == CampaignHome::OffTheDungeonsMap);
        Check("so every member is walked to the inn to rebind",
              CampaignHomeNeedsRebinding(verdict));
    }

    Check("once rebound at Kargath, a member suits",
          !CampaignHomeNeedsRebinding(
              ReadCampaignHome(Bind(0, -6650.01f, -2149.09f, 245.4f), anchor, TOWN)));
}

void TheAllianceFamilyAtMaraudonIsGivenNijelsPoint()
{
    std::vector<HomeBind> const family = TheAllianceFamily();
    CampaignHomeAnchor const anchor =
        UnnamedDoorHomeAnchor(family, MARAUDON_MAP, 1, false, TOWN);
    Check("the Maraudon door names the Alliance an inn", anchor.known);
    Check("on map 1", anchor.mapId == 1);
    Check("at Nijel's Point, Innkeeper Lyshaerya's spawn",
          anchor.x == 255.615f && anchor.y == 1253.76f);
    for (HomeBind const& home : family)
        Check("every member is walked there to rebind",
              CampaignHomeNeedsRebinding(ReadCampaignHome(home, anchor, TOWN)));
}

void TheFactionPicksTheInn()
{
    CampaignHomeAnchor const alliance = FactionInnForDoor(ULDAMAN_MAP, 0, false);
    Check("an Alliance family at Uldaman gets Thelsamar, not a Horde town",
          alliance.known && alliance.x == -5377.91f);
    CampaignHomeAnchor const horde = FactionInnForDoor(MARAUDON_MAP, 1, true);
    Check("a Horde family at Maraudon gets Shadowprey Village",
          horde.known && horde.x == -1592.84f);
}

void TheFamilysOwnInnStillComesFirst()
{
    std::vector<HomeBind> family = TheHordeFamily();
    family[2] = Bind(0, -10000.f, -2000.f, 50.f);   // one already bound on map 0
    CampaignHomeAnchor const anchor = UnnamedDoorHomeAnchor(family, ULDAMAN_MAP, 0, true, TOWN);
    Check("an inn a member already uses on the door's map wins",
          anchor.known && anchor.x == -10000.f);
}

void ADoorTheTableDoesNotNameStaysUnknown()
{
    Check("no inn invented for a door the table does not name",
          !UnnamedDoorHomeAnchor(TheHordeFamily(), 36, 0, true, TOWN).known);
    Check("nor for a door map that does not match",
          !FactionInnForDoor(ULDAMAN_MAP, 1, true).known);
}

ExitHearthFacts BoundAcrossTheOcean(std::uint32_t doorMap, std::uint32_t bindMap)
{
    ExitHearthFacts facts;
    facts.inWorld = true;
    facts.onInsideMap = true;
    facts.alive = true;
    facts.carriesStone = true;
    facts.doorMapKnown = true;
    facts.doorMapId = doorMap;
    facts.bindMapId = bindMap;
    facts.failedWalkSeconds = OFF_CONTINENT_HEARTH_AFTER_SECONDS * 6;
    return facts;
}

void TheExitHearthHoldsInBothDirections()
{
    // Uldaman: door on map 0, member bound on map 1.
    ExitHearthFacts uldaman = BoundAcrossTheOcean(0, 1);
    Check("Uldaman: an hour of failed walking does not let the stone across",
          !OffContinentHearthLetThrough(uldaman));
    Check("Uldaman: the member is held, not hearthed",
          ExitFailureHearthStep(uldaman) == ExitHearthStep::Impossible);
    uldaman.familyHearthedOut = true;
    Check("Uldaman: nor once the family has hearthed out",
          ExitFailureHearthStep(uldaman) == ExitHearthStep::Impossible);
    Check("Uldaman: and the log says why",
          ExitHearthImpossibleReason(uldaman).find("held for the walk out") != std::string::npos);

    // Maraudon: door on map 1, member bound on map 0.
    ExitHearthFacts maraudon = BoundAcrossTheOcean(1, 0);
    Check("Maraudon: the member is held, not hearthed",
          ExitFailureHearthStep(maraudon) == ExitHearthStep::Impossible);

    // Bound on the door's continent the stone is still the way out.
    ExitHearthFacts home = BoundAcrossTheOcean(0, 0);
    Check("bound on the door's continent, the stone is cast",
          ExitFailureHearthStep(home) == ExitHearthStep::Cast);
}

}  // namespace

int main()
{
    TheHordeFamilyAtUldamanIsGivenKargath();
    TheAllianceFamilyAtMaraudonIsGivenNijelsPoint();
    TheFactionPicksTheInn();
    TheFamilysOwnInnStillComesFirst();
    ADoorTheTableDoesNotNameStaysUnknown();
    TheExitHearthHoldsInBothDirections();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a family bound across the ocean is rebound at the door's continent\n");
    return EXIT_SUCCESS;
}
