/*
 * A door that names no town takes the family's own inn on its continent
 * (mod-overseer#1142).
 *
 * WHAT THIS IS ABOUT. The Uldaman row names no town, so the campaign home drive
 * (#348) judged nobody. Read out of `character_homebind` on the dev realm,
 * 2026-11-03, for the family running Uldaman (door on map 0):
 *
 *     Bork  map 0 zone 1519  (-8867.8,  673.7)   Stormwind
 *     Grug  map 0 zone 1519  (-8867.8,  673.7)   Stormwind
 *     Grog  map 0 zone 1519  (-8865.0,  669.1)   Stormwind
 *     Og    map 0 zone 1519  (-8865.4,  670.3)   Stormwind
 *     Ugga  map 1 zone 392   (-1047.8, -3664.8)  Ratchet
 *
 * In the 30 hours after roll 89 every one of the 44 wrong-continent EXIT HEARTH
 * homes was Ugga going from map 70 to map 1: the family hearthed to map 0,
 * Ugga was let through to Ratchet (#1202, #1316), and the family stayed split
 * until it sailed back. The fixtures are those rows.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::CampaignHome;
using OverseerDecisions::CampaignHomeAnchor;
using OverseerDecisions::CampaignHomeName;
using OverseerDecisions::CampaignHomeNeedsRebinding;
using OverseerDecisions::FamilyInnOnDoorMap;
using OverseerDecisions::HomeBind;
using OverseerDecisions::ReadCampaignHome;

namespace
{

int failures = 0;
constexpr float TOWN = 250.0f;   // CAMPAIGN_HOME_TOWN_YARDS in the adapter
constexpr std::uint32_t ULDAMAN_DOOR_MAP = 0;
constexpr std::uint32_t RAZORFEN_DOWNS_DOOR_MAP = 1;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckVerdict(char const* what, CampaignHome got, CampaignHome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, CampaignHomeName(got),
                CampaignHomeName(want));
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

// Leader first, as the roster lists them.
std::vector<HomeBind> TheUldamanFamily()
{
    return {
        Bind(0, -8867.8f, 673.7f, 97.90f),      // Grug
        Bind(0, -8867.8f, 673.7f, 97.90f),      // Bork
        Bind(0, -8864.99f, 669.06f, 97.90f),    // Grog
        Bind(0, -8865.41f, 670.26f, 97.90f),    // Og
        Bind(1, -1047.78f, -3664.8f, 23.89f),   // Ugga
    };
}

void TheMemberBoundAcrossTheOceanIsOwedAHome()
{
    std::vector<HomeBind> const family = TheUldamanFamily();
    CampaignHomeAnchor const anchor = FamilyInnOnDoorMap(family, ULDAMAN_DOOR_MAP, TOWN);
    Check("the family has an inn on the Uldaman door's continent", anchor.known, true);
    Check("that inn is on map 0", anchor.mapId == ULDAMAN_DOOR_MAP, true);
    Check("that inn is Stormwind, where four of five are bound",
          anchor.x < -8800.f && anchor.y > 600.f, true);

    CampaignHome const ugga = ReadCampaignHome(family[4], anchor, TOWN);
    CheckVerdict("Ugga's Ratchet home is off the door's map", ugga,
                 CampaignHome::OffTheDungeonsMap);
    Check("so Ugga is walked to the family's inn", CampaignHomeNeedsRebinding(ugga), true);

    for (std::size_t i = 0; i < 4; ++i)
        Check("a member already bound in Stormwind is left alone",
              CampaignHomeNeedsRebinding(ReadCampaignHome(family[i], anchor, TOWN)), false);
}

void OnTheRightContinentAnyInnSuits()
{
    // The fallback asks about the continent only: a member at a different inn
    // on map 0 is not walked across the Eastern Kingdoms for it.
    std::vector<HomeBind> family = TheUldamanFamily();
    HomeBind const thelsamar = Bind(0, -5377.0f, -2973.0f, 323.0f);
    family.push_back(thelsamar);
    CampaignHomeAnchor const anchor = FamilyInnOnDoorMap(family, ULDAMAN_DOOR_MAP, TOWN);
    CheckVerdict("a home at another inn on the door's map suits",
                 ReadCampaignHome(thelsamar, anchor, TOWN), CampaignHome::Suits);
}

void TheSameFamilyAtARazorfenDownsDoorTurnsTheOtherWay()
{
    // Razorfen Downs is approached from map 1 and names no town either. Ugga's
    // Ratchet inn is the family's only one there, so the four Stormwind homes
    // are the ones owed a home.
    std::vector<HomeBind> const family = TheUldamanFamily();
    CampaignHomeAnchor const anchor =
        FamilyInnOnDoorMap(family, RAZORFEN_DOWNS_DOOR_MAP, TOWN);
    Check("Ratchet is the family's inn on map 1", anchor.known && anchor.mapId == 1, true);
    Check("Grug is owed a home on map 1",
          CampaignHomeNeedsRebinding(ReadCampaignHome(family[0], anchor, TOWN)), true);
    Check("Ugga is not",
          CampaignHomeNeedsRebinding(ReadCampaignHome(family[4], anchor, TOWN)), false);
}

void NoInnOnTheDoorsContinentJudgesNobody()
{
    // A family with nobody bound on the door's map has no inn there it is known
    // to be welcome at, so nobody is walked anywhere, as before.
    std::vector<HomeBind> const family = {Bind(1, 1634.0f, -4439.4f, 15.76f),
                                          Bind(1, -1050.0f, -3665.0f, 24.0f)};
    CampaignHomeAnchor const anchor = FamilyInnOnDoorMap(family, ULDAMAN_DOOR_MAP, TOWN);
    Check("no inn on map 0", anchor.known, false);
    CheckVerdict("so nothing is asked", ReadCampaignHome(family[0], anchor, TOWN),
                 CampaignHome::NotAsked);

    HomeBind unread;
    Check("an unreadable home is not an inn",
          FamilyInnOnDoorMap({unread}, ULDAMAN_DOOR_MAP, TOWN).known, false);
}

void TiesGoToTheLeader()
{
    std::vector<HomeBind> const family = {Bind(0, -5377.0f, -2973.0f, 323.0f),
                                          Bind(0, -8867.8f, 673.7f, 97.9f)};
    CampaignHomeAnchor const anchor = FamilyInnOnDoorMap(family, ULDAMAN_DOOR_MAP, TOWN);
    Check("one each: the first in the list wins", anchor.known && anchor.x > -5400.f,
          true);
}

void ADoorThatNamesATownStillJudgesTheDistance()
{
    // The named anchor is unchanged: mapOnly defaults off.
    CampaignHomeAnchor stockades;
    stockades.known = true;
    stockades.mapId = 0;
    stockades.x = -8867.8f;
    stockades.y = 673.7f;
    CheckVerdict("a home on the right map far from the named town is too far",
                 ReadCampaignHome(Bind(0, -5377.0f, -2973.0f, 323.0f), stockades, TOWN),
                 CampaignHome::TooFarFromTheTown);
}

}  // namespace

int main()
{
    TheMemberBoundAcrossTheOceanIsOwedAHome();
    OnTheRightContinentAnyInnSuits();
    TheSameFamilyAtARazorfenDownsDoorTurnsTheOtherWay();
    NoInnOnTheDoorsContinentJudgesNobody();
    TiesGoToTheLeader();
    ADoorThatNamesATownStillJudgesTheDistance();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a door that names no town takes the family's own inn on its "
                "continent, and only a home across the ocean is moved\n");
    return EXIT_SUCCESS;
}
