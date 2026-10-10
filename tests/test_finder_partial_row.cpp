/*
 * A guild run on one wing's finder row goes on to the whole dungeon, and every
 * Dire Maul door finds its wing's finder row.
 *
 * The defect this pins (dev realm world DB and client data, 2026-10-10). A
 * guild run counted as cleared the moment the core put its finder group in
 * LFG_STATE_FINISHED_DUNGEON. The core does that on the boss whose
 * instance_encounters row names the queued finder dungeon in
 * lastEncounterDungeon. On a map the finder splits into several rows of one
 * connected dungeon, that boss is an early one:
 *
 *   Blackrock Depths (map 230): the door resolves to row 30, the Prison,
 *     which finishes on High Interrogator Gerstahn, encounter 0 of 19. Row
 *     276, the Upper City, finishes on Emperor Dagran Thaurissan and shares
 *     row 30's start, and the tie goes to 30 (mod-overseer#903).
 *   Maraudon (map 349): the orange door is row 26 (Razorlash, encounter 1) and
 *     the purple door row 272 (Lord Vyletongue, encounter 2), of 8 up to
 *     Princess Theradras (row 273, Pristine Waters, which no door lands on).
 *
 * So every such run ended at its first finder boss and left most of the
 * dungeon standing. The run now goes on until every boss the dungeon brain is
 * armed for is down. Dire Maul and Scarlet Monastery are not this shape: each
 * finder row there is a wing of its own, finishing on that wing's last boss.
 *
 * Dire Maul's doors: `dire-maul-east-east` (areatrigger 3185) lands 683 yards
 * from row 34's start and `dire-maul-west-north` (3187) 95 yards from row 36's,
 * past FINDER_WING_MATCH_YARDS (60). The East wing's west door (3183) and the
 * West wing's south door (3186) land on those starts to the yard. A door is
 * matched by the landing of the door into the same wing that the finder starts
 * at.
 *
 * Compiles against the pure decision file; reads src/mod_overseer.cpp as text
 * (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ChooseFinderDungeon;
using OverseerDecisions::FinderDungeonCandidate;
using OverseerDecisions::FinderRowEndsDungeon;
using OverseerDecisions::FinderWingDoorFor;
using OverseerDecisions::GUILD_RUN_CEILING_SECONDS;
using OverseerDecisions::GUILD_RUN_SECONDS_PER_BOSS;
using OverseerDecisions::GuildRunCeilingSeconds;
using OverseerDecisions::GuildRunExpectedMask;
using OverseerDecisions::GuildRunNext;
using OverseerDecisions::GuildRunPoll;
using OverseerDecisions::GuildRunVerdict;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// DungeonEncounter.dbc, normal difficulty, read off the dev realm's client.
constexpr std::uint32_t DEPTHS_MAP_MASK = (1u << 19) - 1;  // 19 encounters, bits 0-18
constexpr std::uint32_t MARAUDON_MAP_MASK = (1u << 8) - 1;  // 8 encounters, bits 0-7
constexpr std::uint32_t GERSTAHN = 1u << 0;
constexpr std::uint32_t RING_OF_LAW = 1u << 3;
constexpr std::uint32_t EMPEROR = 1u << 18;
constexpr std::uint32_t ROTGRIP = 1u << 6;
constexpr std::uint32_t THERADRAS = 1u << 7;

GuildRunPoll Inside()
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 5;
    poll.secondsInside = 600;
    return poll;
}

void AWingRowOfAConnectedDungeonDoesNotEndIt()
{
    Check("Blackrock Depths' Prison row is one wing of a connected dungeon",
          !FinderRowEndsDungeon(230, 2));
    Check("Maraudon's orange row is one of three rows of one dungeon",
          !FinderRowEndsDungeon(349, 3));
    Check("a map with one finder row ends on its last boss (the Deadmines)",
          FinderRowEndsDungeon(36, 1));
    Check("a Scarlet Monastery row is a wing of its own", FinderRowEndsDungeon(189, 4));
    Check("a Dire Maul row is a wing of its own", FinderRowEndsDungeon(429, 3));
}

void TheFinderFlagOnAWingRowLeavesTheRunGoing()
{
    // Gerstahn down, the finder says the Prison is finished, 18 to go.
    GuildRunPoll p = Inside();
    p.finderFinished = true;
    p.finderEndsDungeon = false;
    p.expectedMask = DEPTHS_MAP_MASK & ~RING_OF_LAW;
    p.creditedMask = GERSTAHN;
    p.bossesTotal = 18;
    p.bossesDone = 1;
    Check("a Blackrock Depths run at Gerstahn is still running",
          GuildRunNext(p) == GuildRunVerdict::Running);

    p.creditedMask = p.expectedMask;
    p.bossesDone = 18;
    Check("and is cleared once every boss it is armed for is down",
          GuildRunNext(p) == GuildRunVerdict::Cleared);

    GuildRunPoll whole = Inside();
    whole.finderFinished = true;
    Check("a row that ends its dungeon still clears on the finder's word",
          GuildRunNext(whole) == GuildRunVerdict::Cleared);

    GuildRunPoll gone = Inside();
    gone.finderFinished = true;
    gone.finderEndsDungeon = false;
    gone.expectedMask = DEPTHS_MAP_MASK;
    gone.creditedMask = GERSTAHN;
    gone.groupGone = true;
    Check("a wing run whose group is gone is abandoned, not cleared",
          GuildRunNext(gone) == GuildRunVerdict::Abandoned);
}

void TheWholeDungeonIsWhatTheBrainIsArmedFor()
{
    // Maraudon: mod-dungeon-clear drops Rotgrip, who lives in open water, so
    // its roster never credits bit 6.
    std::uint32_t const brain = MARAUDON_MAP_MASK & ~ROTGRIP;
    Check("a Maraudon wing run expects what the brain will clear",
          GuildRunExpectedMask(349, MARAUDON_MAP_MASK, brain, false) == brain);
    Check("Princess Theradras is part of it",
          (GuildRunExpectedMask(349, MARAUDON_MAP_MASK, brain, false) & THERADRAS) != 0);
    Check("an unread brain expects the whole map",
          GuildRunExpectedMask(349, MARAUDON_MAP_MASK, 0, false) == MARAUDON_MAP_MASK);

    // Blackrock Depths: the brain's roster counts combat bosses only, so the
    // Ring of Law (an objective, credited to the announcer) is not expected.
    std::uint32_t const depthsBrain = DEPTHS_MAP_MASK & ~RING_OF_LAW;
    std::uint32_t const depths = GuildRunExpectedMask(230, DEPTHS_MAP_MASK, depthsBrain, false);
    Check("a Blackrock Depths wing run expects the Emperor", (depths & EMPEROR) != 0);
    Check("and Gerstahn", (depths & GERSTAHN) != 0);
    Check("and nothing the brain does not clear", (depths & RING_OF_LAW) == 0);

    Check("a row that ends its connected dungeon keeps the map's expectation",
          GuildRunExpectedMask(36, 0b1111111, 0b0000011, true) == 0b1111111);
    Check("an independent wing is still narrowed to the brain's wing",
          GuildRunExpectedMask(429, 0xFFFF, 0x000F, true) == 0x000F);
    Check("Gnomeregan's escort boss stays out of every expectation",
          (GuildRunExpectedMask(90, 0b111110, 0, true) & (1u << 2)) == 0);
}

void AWholeDungeonGetsTheTimeItsBossesTake()
{
    Check("a run on a row that ends its dungeon keeps the ceiling",
          GuildRunCeilingSeconds(true, 15) == GUILD_RUN_CEILING_SECONDS);
    Check("a whole Blackrock Depths gets a quarter hour a boss",
          GuildRunCeilingSeconds(false, 15) == 15 * GUILD_RUN_SECONDS_PER_BOSS);
    Check("and is past the ceiling", GuildRunCeilingSeconds(false, 15) > GUILD_RUN_CEILING_SECONDS);
    Check("a small whole dungeon never gets less than the ceiling",
          GuildRunCeilingSeconds(false, 4) == GUILD_RUN_CEILING_SECONDS);

    GuildRunPoll p = Inside();
    p.finderEndsDungeon = false;
    p.secondsInside = GUILD_RUN_CEILING_SECONDS + 60;
    p.ceilingSeconds = GuildRunCeilingSeconds(false, 15);
    Check("two hours into a whole Blackrock Depths is not out of time",
          GuildRunNext(p) == GuildRunVerdict::Running);
}

// lfg_dungeon_template starts and areatrigger_teleport landings on map 429,
// read off the dev realm.
struct Landing
{
    float x;
    float y;
};

std::map<std::string, Landing> const DIRE_MAUL_LANDINGS = {
    {"dire-maul-east-east", {9.31119f, -837.085f}},   // 3185
    {"dire-maul-east-west", {44.4499f, -154.822f}},   // 3183
    {"dire-maul-east-south", {-201.11f, -328.66f}},   // 3184
    {"dire-maul-west-north", {31.5609f, 159.45f}},    // 3187
    {"dire-maul-west-south", {-62.9658f, 159.867f}},  // 3186
    {"dire-maul-north", {255.249f, -16.0561f}},       // 3189
};

FinderDungeonCandidate Row(std::uint32_t id, float x, float y)
{
    FinderDungeonCandidate c;
    c.id = id;
    c.hasEntrance = true;
    c.x = x;
    c.y = y;
    return c;
}

std::uint32_t DireMaulRowFor(std::string const& door)
{
    std::vector<FinderDungeonCandidate> const rows = {
        Row(34, 44.4499f, -154.822f), Row(36, -62.9658f, 159.867f), Row(38, 255.249f, -16.0561f)};
    auto const at = DIRE_MAUL_LANDINGS.find(FinderWingDoorFor(door));
    if (at == DIRE_MAUL_LANDINGS.end())
        return 0;
    std::string why;
    return ChooseFinderDungeon(rows, at->second.x, at->second.y, why);
}

void EveryDireMaulDoorFindsItsWing()
{
    Check("the East wing's east door is the East row", DireMaulRowFor("dire-maul-east-east") == 34);
    Check("the East wing's west door is the East row", DireMaulRowFor("dire-maul-east-west") == 34);
    Check("the East wing's south door is the East row", DireMaulRowFor("dire-maul-east-south") == 34);
    Check("the West wing's north door is the West row", DireMaulRowFor("dire-maul-west-north") == 36);
    Check("the West wing's south door is the West row", DireMaulRowFor("dire-maul-west-south") == 36);
    Check("the North wing's door is the North row", DireMaulRowFor("dire-maul-north") == 38);
    Check("a door with no sibling is measured from its own landing",
          FinderWingDoorFor("blackrock-depths") == "blackrock-depths");
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("the door's finder row is matched from its wing's door",
        "FindDungeonPortal(OverseerDecisions::FinderWingDoorFor(portal.keyword))");
    has("the poll says whether the queued row ends its dungeon",
        "poll.finderEndsDungeon = run.finderEndsDungeon;");
    has("the run's expectation is the guild run's",
        "OverseerDecisions::GuildRunExpectedMask(");
    has("and so is its ceiling",
        "OverseerDecisions::GuildRunCeilingSeconds(run.finderEndsDungeon, run.bossesTotal);");
}

}  // namespace

int main()
{
    AWingRowOfAConnectedDungeonDoesNotEndIt();
    TheFinderFlagOnAWingRowLeavesTheRunGoing();
    TheWholeDungeonIsWhatTheBrainIsArmedFor();
    AWholeDungeonGetsTheTimeItsBossesTake();
    EveryDireMaulDoorFindsItsWing();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_finder_partial_row: ok\n");
    return 0;
}
