#ifndef OVERSEER_DOOR_INNS_H
#define OVERSEER_DOOR_INNS_H

// THE FACTION'S OWN INN NEAR A DOOR, FOR A FAMILY BOUND WHOLLY ON THE OTHER
// CONTINENT (#1388).
//
// FamilyInnOnDoorMap (#1142, #1333) keeps a family's hearth on the door's
// continent by walking members to an inn somebody in the family is already
// bound at on that continent. When the whole family is bound on the other
// continent there is no such inn, the anchor is unknown, nobody is walked
// anywhere, and every failed walk out of the dungeon hearthed members across
// the ocean. Measured on the dev realm 2026-11-03 to 2026-11-05: the Horde
// family running Uldaman (door on map 0) was bound on map 1 and logged
// `went home: map 70 to map 1` 17 times in 48 hours; the Alliance family
// running Maraudon (door on map 1) was bound on map 0 and logged
// `went home: map 349 to map 0` 6 times.
//
// So a door that names no town also names, per faction, the nearest inn its
// faction can bind at on the door's continent. Read from the world database,
// innkeepers (npcflag 65536) on the door's map by straight line from the
// entrance trigger, faction template in brackets:
//
//   Uldaman, trigger 286 (map 0, -6053.73, -2954.63):
//      6734 Innkeeper Hearthstove   693 yards  Thelsamar      (55, Alliance)
//      9356 Innkeeper Shul'kar      993 yards  Kargath        (29, Horde)
//   Maraudon, trigger 3133 (map 1, -1484.07, 2617.57):
//     11106 Innkeeper Sikewa        544 yards  Shadowprey     (104, Horde)
//     11103 Innkeeper Lyshaerya    2211 yards  Nijel's Point  (80, Alliance)
//
// The point is the innkeeper's spawn, which is where the home bind drive walks
// a member and binds it. The anchor is map-only, like the family inn's: any
// home already on the door's continent suits, so only a home a hearthstone
// would carry across an ocean is moved.
//
// No AzerothCore include: tests/test_door_inns.cpp compiles this alone.

#include "overseer_decisions.h"

#include <cstdint>

namespace OverseerDecisions
{

struct DoorInn
{
    std::uint32_t insideMapId;
    std::uint32_t doorMapId;
    bool horde;
    float x;
    float y;
    float z;
};

constexpr DoorInn DOOR_INNS[] = {
    // Uldaman (both doors let out on map 0).
    {70, 0, true, -6650.01f, -2149.09f, 245.434f},    // Shul'kar, Kargath
    {70, 0, false, -5377.91f, -2973.91f, 323.252f},   // Hearthstove, Thelsamar
    // Maraudon (both wings let out on map 1).
    {349, 1, true, -1592.84f, 3150.27f, 46.5777f},    // Sikewa, Shadowprey Village
    {349, 1, false, 255.615f, 1253.76f, 192.224f},    // Lyshaerya, Nijel's Point
};

// The faction's inn for the dungeon on `insideMapId` whose door lets out on
// `doorMapId`, or an unknown anchor for a door this table does not name.
inline CampaignHomeAnchor FactionInnForDoor(std::uint32_t insideMapId, std::uint32_t doorMapId,
                                            bool horde)
{
    CampaignHomeAnchor anchor;
    for (DoorInn const& inn : DOOR_INNS)
    {
        if (inn.insideMapId != insideMapId || inn.doorMapId != doorMapId || inn.horde != horde)
            continue;
        anchor.known = true;
        anchor.mapId = inn.doorMapId;
        anchor.x = inn.x;
        anchor.y = inn.y;
        anchor.z = inn.z;
        anchor.mapOnly = true;
        break;
    }
    return anchor;
}

// The home a campaign at a door that names no town judges against: the
// family's own inn on the door's continent when somebody is bound there
// (#1142), else the faction's inn for this door (#1388), else unknown.
inline CampaignHomeAnchor UnnamedDoorHomeAnchor(std::vector<HomeBind> const& binds,
                                                std::uint32_t insideMapId,
                                                std::uint32_t doorMapId, bool horde,
                                                float townYards)
{
    CampaignHomeAnchor const family = FamilyInnOnDoorMap(binds, doorMapId, townYards);
    if (family.known)
        return family;
    return FactionInnForDoor(insideMapId, doorMapId, horde);
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_DOOR_INNS_H
