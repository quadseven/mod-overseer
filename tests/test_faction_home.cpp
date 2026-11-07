/*
 * Whose inn a dungeon campaign's named home is (#1400).
 *
 * The `blackrock-depths` portal row names Innkeeper Shul'kar at Kargath, a
 * Horde inn, as its campaign home. Before #1400 the campaign home drive and
 * the restage walk used it for any family, so an Alliance family queued into
 * Blackrock Depths would have been walked into a Horde town to bind. These
 * checks pin the rule (a named home serves only its own faction) and that the
 * adapter's two callers hand it the family's faction.
 *
 * Compiles against the pure headers only; the source checks read
 * src/mod_overseer.cpp from the repository root, as test_staging_point does.
 */

#include "overseer_faction_home.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerFactionHome::Faction;
using OverseerFactionHome::FamilyFaction;
using OverseerFactionHome::HomeFactionOf;
using OverseerFactionHome::NamedHomeServes;

namespace
{

int failures = 0;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

std::string ReadSource()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

// The body of the first function whose signature contains `signature`, from
// its first `{` to the matching `}`. Empty when it is not found.
std::string FunctionBody(std::string const& all, std::string const& signature)
{
    std::size_t const at = all.find(signature);
    if (at == std::string::npos)
        return {};
    std::size_t const open = all.find('{', at);
    if (open == std::string::npos)
        return {};
    int depth = 0;
    for (std::size_t i = open; i < all.size(); ++i)
    {
        if (all[i] == '{')
            ++depth;
        else if (all[i] == '}' && --depth == 0)
            return all.substr(open, i - open + 1);
    }
    return {};
}

void AnAllianceFamilyIsNotServedByKargath()
{
    Faction const home = HomeFactionOf("blackrock-depths");
    CheckBool("blackrock-depths names a Horde home", home == Faction::Horde, true);
    CheckBool("Kargath does not serve an Alliance family",
              NamedHomeServes(home, Faction::Alliance), false);
    std::vector<Faction> const grug(5, Faction::Alliance);
    CheckBool("an Alliance family reads as Alliance", FamilyFaction(grug) == Faction::Alliance,
              true);
    CheckBool("Kargath does not serve that family", NamedHomeServes(home, FamilyFaction(grug)),
              false);
}

void AHordeFamilyKeepsKargath()
{
    std::vector<Faction> const zug(5, Faction::Horde);
    CheckBool("Kargath serves a Horde family",
              NamedHomeServes(HomeFactionOf("blackrock-depths"), FamilyFaction(zug)), true);
    CheckBool("Stormwind serves an Alliance family",
              NamedHomeServes(HomeFactionOf("stockades"), Faction::Alliance), true);
    CheckBool("Orgrimmar does not serve an Alliance family",
              NamedHomeServes(HomeFactionOf("ragefire"), Faction::Alliance), false);
    CheckBool("Ratchet serves either faction",
              NamedHomeServes(HomeFactionOf("wailing"), Faction::Alliance) &&
                  NamedHomeServes(HomeFactionOf("wailing"), Faction::Horde),
              true);
}

void AnUnreadableFamilyIsNeverWalkedIntoAFactionTown()
{
    CheckBool("no readings is unknown", FamilyFaction({}) == Faction::Unknown, true);
    CheckBool("mixed readings are unknown",
              FamilyFaction({Faction::Horde, Faction::Alliance}) == Faction::Unknown, true);
    CheckBool("unreadable members are skipped",
              FamilyFaction({Faction::Unknown, Faction::Horde}) == Faction::Horde, true);
    CheckBool("an unknown family is not served by Kargath",
              NamedHomeServes(Faction::Horde, Faction::Unknown), false);
    CheckBool("a home missing from the table serves nobody",
              NamedHomeServes(HomeFactionOf("no-such-door"), Faction::Horde), false);
}

// The adapter: the anchor reads the faction, and both callers pass one. The
// restage walk aims the leader at the anchor's town, so it must ask with the
// leader's faction; without that an Alliance leader was aimed at Kargath.
void TheAdapterAsksWithTheFamilysFaction()
{
    std::string const all = ReadSource();
    std::string const anchor = FunctionBody(
        all, "CampaignHomeAnchor DungeonHomeAnchor(\n        DungeonPortal const& portal, "
             "OverseerFactionHome::Faction family)");
    CheckBool("DungeonHomeAnchor takes the family's faction", !anchor.empty(), true);
    CheckBool("DungeonHomeAnchor refuses a home that does not serve it",
              anchor.find("NamedHomeServes(") != std::string::npos, true);

    std::string const restage = FunctionBody(all, "bool RestageWalkPoll(");
    CheckBool("the restage walk asks with the leader's faction",
              restage.find("DungeonHomeAnchor(portal, FactionOf(leader))") != std::string::npos,
              true);

    std::string const campaign = FunctionBody(all, "CampaignHomeAnchor CampaignHomeAnchorFor(");
    CheckBool("the campaign home asks with the family's faction",
              campaign.find("DungeonHomeAnchor(portal, OverseerFactionHome::FamilyFaction(") !=
                  std::string::npos,
              true);
}

// Every portal row that names a home has a faction in NAMED_HOMES, so a new
// home row cannot be served to the wrong faction by default. A row names a
// home when its top-level floats after the approach triple are not all zero.
void EveryNamedHomeHasAFaction()
{
    std::string const all = ReadSource();
    std::size_t at = all.find("{\"deadmines\", 0, 78, 36, 119,");
    CheckBool("the portal table is found", at != std::string::npos, true);
    if (at == std::string::npos)
        return;
    int rows = 0;
    int homes = 0;
    while (true)
    {
        // Row openings look like {"keyword", <map>, <trigger>, <map>, <trigger>,
        std::size_t const open = all.find("{\"", at);
        if (open == std::string::npos)
            break;
        std::size_t const quote = all.find('"', open + 2);
        std::string const keyword = all.substr(open + 2, quote - open - 2);
        std::size_t const comma = all.find(',', quote);
        // The table ends at the first opening that is not a row.
        if (keyword.empty() || keyword.find(' ') != std::string::npos ||
            comma != quote + 1 || all.compare(quote + 1, 2, ", ") != 0 ||
            !(all[quote + 3] >= '0' && all[quote + 3] <= '9'))
            break;
        ++rows;
        // Collect the row's top-level text, skipping comments and nested braces.
        int depth = 0;
        std::string top;
        std::size_t i = open;
        for (; i < all.size(); ++i)
        {
            char const c = all[i];
            if (c == '/' && i + 1 < all.size() && all[i + 1] == '/')
            {
                i = all.find('\n', i);
                continue;
            }
            if (c == '{')
            {
                ++depth;
                continue;
            }
            if (c == '}')
            {
                if (--depth == 0)
                    break;
                continue;
            }
            if (depth == 1)
                top += c;
        }
        at = i;
        std::vector<std::string> fields;
        std::stringstream split(top);
        for (std::string field; std::getline(split, field, ',');)
        {
            std::size_t const b = field.find_first_not_of(" \n\t");
            if (b == std::string::npos)
                continue;
            fields.push_back(field.substr(b, field.find_last_not_of(" \n\t") - b + 1));
        }
        // keyword, map, trigger, map, trigger, ax, ay, az, [hx, hy, hz, ...]
        if (fields.size() < 11)
            continue;
        bool named = false;
        for (int f = 8; f < 11; ++f)
            if (fields[f] != "0.f" && fields[f] != "0.0f")
                named = true;
        if (!named)
            continue;
        ++homes;
        std::string const what = "row '" + keyword + "' names a home with a faction";
        CheckBool(what.c_str(), HomeFactionOf(keyword.c_str()) != Faction::Unknown, true);
    }
    CheckBool("the whole portal table was read", rows >= 30, true);
    CheckBool("the four named homes were found", homes == 4, true);
}

}  // namespace

int main()
{
    AnAllianceFamilyIsNotServedByKargath();
    AHordeFamilyKeepsKargath();
    AnUnreadableFamilyIsNeverWalkedIntoAFactionTown();
    TheAdapterAsksWithTheFamilysFaction();
    EveryNamedHomeHasAFaction();
    return failures ? 1 : 0;
}
