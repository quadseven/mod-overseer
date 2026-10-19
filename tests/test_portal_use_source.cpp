/*
 * Source contract for clicking the party's portal (#410).
 *
 * The decisions are proved in test_portal_use.cpp. What only the adapter can
 * get wrong is whether it ASKS them and reuses the pieces the issue named
 * rather than growing new ones: the row is routed on its first word ahead of
 * the quest-use verbs, the walk is the meeting stone's grounded walk, the click
 * is DriveGameObjectUse, the crossing is read back through ReadTeleportFlight
 * with the character looked up out of the world, the poll resolves it, the hold
 * sweep knows about it, and the stranded drive feeds its Portal rule from the
 * portal spell's own destination. Pinned as text.
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

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
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

    std::size_t const portalRoute =
        text.find("kind == \"quest\" && OverseerDecisions::IsPortalUseRow(command)");
    std::size_t const questUseRoute =
        text.find("kind == \"quest\" && OverseerDecisions::IsQuestUseRow(command)");
    if (portalRoute == std::string::npos || questUseRoute == std::string::npos ||
        !(portalRoute < questUseRoute))
        return Fail("a use-portal row must be routed on kind='quest' before the quest-use verbs");
    if (!Has(text, "detail = DoPortalUse(player, command, status, rowResult, id);"))
        return Fail("the use-portal route must reach DoPortalUse");

    std::string const row = Body(text, "char const* DoPortalUse(");
    if (row.empty())
        return Fail("cannot find DoPortalUse");
    for (char const* needed : {"OverseerDecisions::ParsePortalUseRequest(command)",
                               "OverseerDecisions::PickPortal(candidates, request.objectGuid, wall)",
                               "OverseerDecisions::PortalLeaderCrossesFirst(order)",
                               "Refusal::LeaderFirst", "BeginPortalUse(", "Cast::InCombat",
                               "Cast::NoBotAI"})
        if (!Has(row, needed))
        {
            std::cerr << "DoPortalUse must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::string const begin = Body(text, "char const* BeginPortalUse(");
    for (char const* needed : {"HoldStillAndReport(", "AtTheStone(who, portal)",
                               "DriveGameObjectUse(", "WalkTowardTheStone(who, portal)",
                               "_pendingPortalUses.push_back("})
        if (!Has(begin, needed))
        {
            std::cerr << "BeginPortalUse must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::string const resolve = Body(text, "void ResolvePortalUses(uint32 elapsedMs)");
    for (char const* needed :
         {"ObjectAccessor::FindPlayerByName(check.targetName, false)", "ReadTeleportFlight(",
          "CastPlaceChanged(check.from, check.now, CAST_MOVED_YARDS)", "JudgeCast(read)",
          "DriveGameObjectUse(", "WalkTowardTheStone(bot, portal)",
          "SUMMON_APPROACH_CEILING_MS"})
        if (!Has(resolve, needed))
        {
            std::cerr << "ResolvePortalUses must use " << needed << "\n";
            return EXIT_FAILURE;
        }

    std::size_t const casts = text.find("ResolveCastChecks(sincePollMs);");
    std::size_t const portals = text.find("ResolvePortalUses(sincePollMs);");
    if (casts == std::string::npos || portals == std::string::npos || !(casts < portals))
        return Fail("the command poll must resolve the portal clicks beside the casts");

    std::string const holds = Body(text, "bool ARowStillOwnsTheHold(std::string const& name) const");
    if (!Has(holds, "PortalUseUnderWay(name)"))
        return Fail("the hold backstop must leave a character a portal click still owns");

    std::string const stranded = Body(text, "bool DriveStrandedMember(");
    if (!Has(stranded, "OverseerDecisions::PickPortalTo(portalFacts, facts.leaderMap)") ||
        !Has(stranded, "facts.portalToLeaderMap = portalPick >= 0;"))
        return Fail("the stranded drive must read the portal to the leader's map into its facts");
    std::size_t const decide = stranded.find("OverseerDecisions::DecideStrandedWay(facts)");
    std::size_t const read = stranded.find("ReadPortals(p, portals, portalFacts);");
    if (decide == std::string::npos || read == std::string::npos || !(read < decide))
        return Fail("the portal must be read before the stranded way is decided");
    if (!Has(stranded, "case OverseerDecisions::StrandedWay::Portal:") ||
        !Has(stranded, "BeginPortalUse(p, portals[std::size_t(portalPick)]"))
        return Fail("the stranded Portal way must walk to the portal and click it");
    if (Has(stranded, "TeleportTo(") || Has(resolve, "TeleportTo(") || Has(begin, "TeleportTo("))
        return Fail("nothing on the way through a portal may teleport anybody by command");

    std::cout << "a use-portal row and a stranded member walk to the party's portal, click it, "
                 "and read the crossing back\n";
    return EXIT_SUCCESS;
}
