#ifndef OVERSEER_FACTION_HOME_H
#define OVERSEER_FACTION_HOME_H

// WHOSE INN A DUNGEON CAMPAIGN'S NAMED HOME IS (#1400).
//
// A portal row in DungeonPortals() may name one town as its campaign home, and
// the campaign home drive walks every member who is not bound there to rebind
// at it; the restage walk aims the leader at it first. An innkeeper serves only
// its own faction (Player::GetNPCIfCanInteractWith refuses the rest) and the
// town's guards attack the other one. The `blackrock-depths` row names
// Innkeeper Shul'kar at Kargath, a Horde inn, and on 2026-11-07 an Alliance
// family was queued into that door by operator order. Nothing stopped the drive
// from walking that family into Kargath to bind.
//
// So the faction of each named home is written down here, and a named home
// serves a family only when it is open to that family's faction. A family it
// does not serve is judged as if the door named no town, which is the existing
// fallback: the family's own inn on the door's map (#1142).
//
// No AzerothCore include: the decisions runner compiles this on its own and
// tests/test_faction_home.cpp drives it. The adapter reads a member's faction
// off the live Player and hands it here.

#include <cstring>
#include <vector>

namespace OverseerFactionHome
{

enum class Faction
{
    // Nothing could be read, or the readings disagree. Never served by a named
    // home that belongs to one faction: walking somebody into the wrong town is
    // worse than falling back to the family's own inn.
    Unknown,
    Alliance,
    Horde,
    // A neutral town (Ratchet, Booty Bay, Gadgetzan, Everlook): any faction binds.
    Any,
};

struct NamedHome
{
    char const* keyword;
    Faction faction;
};

// EVERY PORTAL ROW THAT NAMES A HOME HAS AN ENTRY HERE, and
// tests/test_faction_home.cpp fails when a row names a home this table does not
// list, so a new home row has to say whose town it is.
inline constexpr NamedHome NAMED_HOMES[] = {
    {"stockades", Faction::Alliance},     // Stormwind
    {"wailing", Faction::Any},            // Ratchet, a goblin town
    {"blackrock-depths", Faction::Horde}, // Kargath, Innkeeper Shul'kar
    {"ragefire", Faction::Horde},         // Orgrimmar
};

// The faction of the town a portal row names as its home. Unknown for a keyword
// the table does not list, which a named home never serves.
inline Faction HomeFactionOf(char const* keyword)
{
    if (!keyword)
        return Faction::Unknown;
    for (NamedHome const& home : NAMED_HOMES)
        if (std::strcmp(home.keyword, keyword) == 0)
            return home.faction;
    return Faction::Unknown;
}

// The family's faction from its members' readings. Unreadable members are
// skipped; two members of different factions make it Unknown.
inline Faction FamilyFaction(std::vector<Faction> const& members)
{
    Faction family = Faction::Unknown;
    for (Faction member : members)
    {
        if (member != Faction::Alliance && member != Faction::Horde)
            continue;
        if (family == Faction::Unknown)
            family = member;
        else if (family != member)
            return Faction::Unknown;
    }
    return family;
}

// Does a home of this faction serve a family of that faction?
inline bool NamedHomeServes(Faction home, Faction family)
{
    if (home == Faction::Any)
        return true;
    if (home == Faction::Unknown || family == Faction::Unknown || family == Faction::Any)
        return false;
    return home == family;
}

inline char const* FactionName(Faction faction)
{
    switch (faction)
    {
        case Faction::Alliance: return "Alliance";
        case Faction::Horde: return "Horde";
        case Faction::Any: return "neutral";
        case Faction::Unknown: break;
    }
    return "unknown";
}

}  // namespace OverseerFactionHome

#endif  // OVERSEER_FACTION_HOME_H
